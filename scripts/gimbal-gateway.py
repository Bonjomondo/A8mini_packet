#!/usr/bin/env python3
"""A8 mini SIYI TCP/UDP relay with one controller and a motion watchdog.

No protocol translation: discrete commands remain TCP all the way to the camera.
Only continuous speed/zoom and telemetry are accepted over UDP. No dependencies.
"""
import argparse
import asyncio
import binascii
import ipaddress
import logging
import signal
import struct
import time

LOG = logging.getLogger("a8mini-control")
MAX_DATA = 256


def encode(command, data=b"", sequence=0):
    packet = struct.pack("<2sBHHB", b"\x55\x66", 1, len(data), sequence, command) + data
    return packet + struct.pack("<H", binascii.crc_hqx(packet, 0))


def frames(buffer):
    """Extract verified frames; retain partial TCP data and discard bad CRCs."""
    result = []
    while len(buffer) >= 2:
        start = buffer.find(b"\x55\x66")
        if start < 0:
            buffer[:] = b"\x55" if buffer[-1] == 0x55 else b""
            break
        del buffer[:start]
        if len(buffer) < 8:
            break
        length = struct.unpack_from("<H", buffer, 3)[0]
        if length > MAX_DATA or buffer[2] & 0xFC:
            del buffer[:1]
            continue
        size = length + 10
        if len(buffer) < size:
            break
        packet = bytes(buffer[:size])
        if binascii.crc_hqx(packet[:-2], 0) != struct.unpack_from("<H", packet, size - 2)[0]:
            del buffer[:1]
            continue
        result.append(packet)
        del buffer[:size]
    return result


def allowed(packet, udp=False):
    if packet[2] & 2:
        return False
    command, data = packet[7], packet[8:-2]
    if command == 0x07 and len(data) == 2:
        yaw, pitch = struct.unpack("bb", data)
        return -100 <= yaw <= 100 and -100 <= pitch <= 100 and (udp or data == b"\0\0")
    if command == 0x05 and len(data) == 1:
        return data in (b"\xff", b"\0", b"\1") and (udp or data == b"\0")
    if command in (0x0A, 0x0D) and not data:
        return True
    if udp:
        return False
    if command == 0x00:
        return data == b"\0"
    if command == 0x08:
        return data == b"\1"
    if command == 0x0C:
        return data in (b"\0", b"\2", b"\3", b"\4", b"\5")
    if command == 0x0E and len(data) == 4:
        yaw, pitch = struct.unpack("<hh", data)
        return -1350 <= yaw <= 1350 and -900 <= pitch <= 250
    if command == 0x0F and len(data) == 2:
        return 1 <= data[0] <= 30 and data[1] <= 9 and (data[0] < 30 or data[1] == 0)
    return False


class Datagrams(asyncio.DatagramProtocol):
    def __init__(self, callback):
        self.callback = callback

    def datagram_received(self, data, address):
        self.callback(data, address)

    def error_received(self, error):
        LOG.warning("UDP error: %s", error)


class Gateway:
    def __init__(self, camera, port, allow, watchdog=0.35):
        self.camera = (camera, port)
        self.allow = [ipaddress.ip_network(value) for value in allow]
        self.watchdog = watchdog
        self.owner = None
        self.camera_writer = None
        self.client_writer = None
        self.udp_peer = None
        self.server = self.front_udp = self.camera_udp = None
        self.moving = {}
        self.last_sequence = {}
        self.last_tcp = 0
        self._watch_task = None
        self._client_tasks = set()
        self._closed = False

    async def start(self, bind, port):
        loop = asyncio.get_running_loop()
        self.camera_udp, _ = await loop.create_datagram_endpoint(
            lambda: Datagrams(self.camera_response), remote_addr=self.camera)
        self.front_udp, _ = await loop.create_datagram_endpoint(
            lambda: Datagrams(self.client_datagram), local_addr=(bind, port))
        try:
            self.server = await asyncio.start_server(self.client, bind, port)
        except BaseException:
            self.front_udp.close(); self.camera_udp.close()
            raise
        self._watch_task = asyncio.create_task(self.watch())

    def permitted(self, address):
        ip = ipaddress.ip_address(address)
        return any(ip in network for network in self.allow)

    def client_datagram(self, data, address):
        if self.owner != address[0] or self.camera_writer is None:
            return
        if self.udp_peer is not None and address != self.udp_peer:
            return
        buffer = bytearray(data)
        packets = frames(buffer)
        if buffer or len(packets) != 1 or packets[0] != data or not allowed(data, udp=True):
            return
        self.udp_peer = address
        command, payload = data[7], data[8:-2]
        if command in (0x07, 0x05):
            if not self.new_sample(data):
                return
            if any(payload):
                self.moving[command] = time.monotonic()
            else:
                self.moving.pop(command, None)
        self.camera_udp.sendto(data)

    def new_sample(self, packet):
        """Reject reordered/replayed motion, including UDP arriving after TCP stop."""
        command = packet[7]
        sequence = struct.unpack_from("<H", packet, 5)[0]
        previous = self.last_sequence.get(command)
        if previous is not None and not 0 < ((sequence - previous) & 0xFFFF) < 0x8000:
            return False
        self.last_sequence[command] = sequence
        return True

    def camera_response(self, data, address):
        if self.owner and self.udp_peer:
            buffer = bytearray(data)
            packets = frames(buffer)
            if not buffer and len(packets) == 1 and packets[0] == data and data[2] & 2:
                self.front_udp.sendto(data, self.udp_peer)

    def stop(self, commands=(0x07, 0x05)):
        for command in commands:
            packet = encode(command, b"\0\0" if command == 0x07 else b"\0")
            # Repeat only idempotent stop packets; never replay photo/record toggles.
            if self.camera_udp:
                for _ in range(3):
                    self.camera_udp.sendto(packet)
            if self.camera_writer and not self.camera_writer.is_closing():
                self.camera_writer.write(packet)
            self.moving.pop(command, None)

    async def watch(self):
        while True:
            await asyncio.sleep(0.05)
            now = time.monotonic()
            expired = [command for command, stamp in self.moving.items() if now - stamp > self.watchdog]
            if expired:
                self.stop(expired)
                LOG.warning("UDP motion watchdog stopped %s", expired)
            if self.owner and now - self.last_tcp > 3.5 and self.client_writer:
                self.stop()
                self.client_writer.close()

    async def client(self, reader, writer):
        peer = writer.get_extra_info("peername")[0]
        if not self.permitted(peer) or self.owner is not None:
            writer.close()
            await writer.wait_closed()
            return
        self.owner = peer  # Reserve ownership before awaiting camera connection.
        task = asyncio.current_task()
        self._client_tasks.add(task)
        self.client_writer = writer
        self.last_tcp = time.monotonic()
        jobs = []
        camera_writer = None
        try:
            camera_reader, camera_writer = await asyncio.wait_for(
                asyncio.open_connection(*self.camera), timeout=2)
            self.camera_writer = camera_writer
            LOG.info("Controller connected: %s", peer)

            async def upstream():
                buffer = bytearray()
                while data := await reader.read(4096):
                    buffer.extend(data)
                    for packet in frames(buffer):
                        if not allowed(packet):
                            raise ValueError("Unsupported or invalid TCP control command")
                        self.last_tcp = time.monotonic()
                        command, payload = packet[7], packet[8:-2]
                        if command in (0x07, 0x05) and not any(payload):
                            if not self.new_sample(packet):
                                continue
                            self.moving.pop(command, None)
                        camera_writer.write(packet)
                    await camera_writer.drain()

            async def downstream():
                buffer = bytearray()
                while data := await camera_reader.read(4096):
                    buffer.extend(data)
                    for packet in frames(buffer):
                        if packet[2] & 2:
                            writer.write(packet)
                    await writer.drain()

            jobs = [asyncio.create_task(upstream()), asyncio.create_task(downstream())]
            done, _ = await asyncio.wait(jobs, return_when=asyncio.FIRST_COMPLETED)
            for job in done:
                job.result()
        except asyncio.CancelledError:
            pass  # Service shutdown still runs the stop/cleanup block below.
        except (OSError, asyncio.TimeoutError, ValueError) as error:
            LOG.warning("Control connection ended: %s", error)
        finally:
            for job in jobs:
                job.cancel()
            await asyncio.gather(*jobs, return_exceptions=True)
            self.stop()
            if camera_writer:
                try:
                    await asyncio.wait_for(camera_writer.drain(), timeout=0.2)
                except (OSError, asyncio.TimeoutError):
                    pass
                camera_writer.close()
            writer.close()
            self.camera_writer = self.client_writer = self.owner = self.udp_peer = None
            self.moving.clear()
            self.last_sequence.clear()
            self._client_tasks.discard(task)
            LOG.info("Controller released; stop sent")

    async def close(self):
        if self._closed:
            return
        self._closed = True
        if self.server:
            self.server.close()
            await self.server.wait_closed()
        self.stop()
        if self.client_writer:
            self.client_writer.close()
        if self._client_tasks:
            for task in tuple(self._client_tasks):
                task.cancel()
            await asyncio.gather(*tuple(self._client_tasks), return_exceptions=True)
        if self.camera_writer:
            try:
                await asyncio.wait_for(self.camera_writer.drain(), timeout=0.2)
            except (OSError, asyncio.TimeoutError):
                pass
            self.camera_writer.close()
        if self._watch_task:
            self._watch_task.cancel()
            await asyncio.gather(self._watch_task, return_exceptions=True)
        if self.front_udp:
            self.front_udp.close()
        if self.camera_udp:
            self.camera_udp.close()


async def main(args):
    gateway = Gateway(args.camera, args.camera_port, args.allow, args.watchdog_ms / 1000)
    await gateway.start(args.bind, args.port)
    LOG.info("Listening on %s:%s TCP+UDP; camera %s:%s", args.bind, args.port, *gateway.camera)
    stopped = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stopped.set)
    try:
        await stopped.wait()
    finally:
        await gateway.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=37264)
    parser.add_argument("--camera", default="192.168.144.25")
    parser.add_argument("--camera-port", type=int, default=37260)
    parser.add_argument("--allow", action="append", default=None, help="Allowed controller IP/CIDR; repeatable")
    parser.add_argument("--watchdog-ms", type=int, default=350)
    args = parser.parse_args()
    args.allow = args.allow or ["127.0.0.1/32"]
    if not (1 <= args.port <= 65535 and 1 <= args.camera_port <= 65535 and 100 <= args.watchdog_ms <= 1000):
        parser.error("Invalid port or watchdog (100..1000 ms)")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    asyncio.run(main(args))
