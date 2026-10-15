#!/usr/bin/env python3
import asyncio
import importlib.util
import pathlib
import socket
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("gateway", ROOT / "scripts/gimbal-gateway.py")
gateway = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gateway)


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tcp_packets, self.udp_packets, self.camera_writers = [], [], []

        async def camera(reader, writer):
            self.camera_writers.append(writer)
            buffer = bytearray()
            try:
                while data := await reader.read(4096):
                    buffer.extend(data)
                    for packet in gateway.frames(buffer):
                        self.tcp_packets.append(packet)
                        # Echo valid ACKs using an unrelated camera sequence.
                        if packet[7] == 0x0A:
                            reply = bytearray(gateway.encode(0x0A, bytes.fromhex("00000000010100"), 4000))
                            reply[2] = 2
                            import binascii
                            import struct
                            reply[-2:] = struct.pack("<H", binascii.crc_hqx(reply[:-2], 0))
                            writer.write(reply)
                            await writer.drain()
            finally:
                writer.close()

        self.camera = await asyncio.start_server(camera, "127.0.0.1", 0)
        camera_port = self.camera.sockets[0].getsockname()[1]
        loop = asyncio.get_running_loop()
        self.camera_udp, _ = await loop.create_datagram_endpoint(
            lambda: gateway.Datagrams(lambda data, peer: self.udp_packets.append(data)),
            local_addr=("127.0.0.1", camera_port))
        # Select a free port and then bind both transports to it.
        reservation = socket.socket()
        reservation.bind(("127.0.0.1", 0))
        self.port = reservation.getsockname()[1]
        reservation.close()
        self.relay = gateway.Gateway("127.0.0.1", camera_port, ["127.0.0.1/32"], watchdog=0.1)
        await self.relay.start("127.0.0.1", self.port)
        self.client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client.bind(("127.0.0.1", 0))
        self.reader, self.writer = await asyncio.open_connection("127.0.0.1", self.port)
        await asyncio.sleep(0.03)

    async def asyncTearDown(self):
        self.writer.close()
        await self.writer.wait_closed()
        await asyncio.sleep(0.03)
        await self.relay.close()
        self.client.close()
        for writer in self.camera_writers:
            writer.close()
        self.camera.close()
        await self.camera.wait_closed()
        self.camera_udp.close()
        await asyncio.sleep(0.03)

    async def test_tcp_fragmentation_and_ack(self):
        packet = gateway.encode(0x0A)
        self.writer.write(packet[:5]); await self.writer.drain()
        await asyncio.sleep(0.02)
        self.assertEqual(self.tcp_packets, [])
        self.writer.write(packet[5:]); await self.writer.drain()
        response = await asyncio.wait_for(self.reader.readexactly(17), 1)
        self.assertEqual(response[7], 0x0A)
        self.assertEqual(self.tcp_packets, [packet])

    async def test_transport_policy(self):
        self.assertFalse(gateway.allowed(gateway.encode(0x07, b"\1\0")))
        self.assertTrue(gateway.allowed(gateway.encode(0x07, b"\1\0"), udp=True))
        for command, data in ((0x08, b"\1"), (0x0C, b"\2"), (0x0E, b"\0\0\0\0")):
            self.assertTrue(gateway.allowed(gateway.encode(command, data)))
            self.assertFalse(gateway.allowed(gateway.encode(command, data), udp=True))
        self.assertFalse(gateway.allowed(gateway.encode(0x48, b"\1")))
        self.assertFalse(gateway.allowed(gateway.encode(0x0E, bytes.fromhex("47050000"))))

    async def test_udp_watchdog_sends_stop_on_both_transports(self):
        motion = gateway.encode(0x07, b"\x14\0")
        self.client.sendto(motion, ("127.0.0.1", self.port))
        await asyncio.sleep(0.04)
        self.assertIn(motion, self.udp_packets)
        self.assertNotIn(motion, self.tcp_packets)
        await asyncio.sleep(0.15)
        stop = gateway.encode(0x07, b"\0\0")
        self.assertIn(stop, self.tcp_packets)
        self.assertGreaterEqual(self.udp_packets.count(stop), 3)

    async def test_disconnect_stops_zoom(self):
        self.client.sendto(gateway.encode(0x05, b"\1"), ("127.0.0.1", self.port))
        await asyncio.sleep(0.03)
        self.writer.close(); await self.writer.wait_closed()
        await asyncio.sleep(0.04)
        self.assertIn(gateway.encode(0x05, b"\0"), self.tcp_packets)
        self.assertIn(gateway.encode(0x05, b"\0"), self.udp_packets)
        self.assertIsNone(self.relay.owner)

    async def test_second_controller_and_unowned_udp_rejected(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port)
        self.assertEqual(await asyncio.wait_for(reader.read(1), 1), b"")
        writer.close(); await writer.wait_closed()
        # Lock UDP to the TCP owner's first UDP endpoint.
        self.client.sendto(gateway.encode(0x0D), ("127.0.0.1", self.port))
        await asyncio.sleep(0.03)
        outsider = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        packet = gateway.encode(0x07, b"\x64\x64")
        outsider.sendto(packet, ("127.0.0.1", self.port)); outsider.close()
        await asyncio.sleep(0.03)
        self.assertNotIn(packet, self.udp_packets)

    async def test_corrupt_udp_and_discrete_udp_dropped(self):
        corrupt = bytearray(gateway.encode(0x07, b"\1\0")); corrupt[-1] ^= 1
        for packet in (corrupt, gateway.encode(0x0C, b"\2"), gateway.encode(0x07, b"\1\0") + b"junk"):
            self.client.sendto(packet, ("127.0.0.1", self.port))
        await asyncio.sleep(0.04)
        self.assertEqual(self.udp_packets, [])

    async def test_photo_toggle_not_retried(self):
        packet = gateway.encode(0x0C, b"\0")
        self.writer.write(packet); await self.writer.drain()
        await asyncio.sleep(0.2)
        self.assertEqual(self.tcp_packets.count(packet), 1)

    async def test_delayed_udp_cannot_restart_motion_after_tcp_stop(self):
        first = gateway.encode(0x07, b"\x14\0", 65534)
        self.client.sendto(first, ("127.0.0.1", self.port))
        await asyncio.sleep(0.02)
        stop = gateway.encode(0x07, b"\0\0", 0)  # Sequence wrap is valid.
        self.writer.write(stop); await self.writer.drain()
        await asyncio.sleep(0.02)
        delayed = gateway.encode(0x07, b"\x15\0", 65535)
        self.client.sendto(delayed, ("127.0.0.1", self.port))
        self.client.sendto(first, ("127.0.0.1", self.port))
        await asyncio.sleep(0.02)
        self.assertIn(stop, self.tcp_packets)
        self.assertNotIn(delayed, self.udp_packets)
        self.assertEqual(self.udp_packets.count(first), 1)

    async def test_service_shutdown_stops_and_closes_session(self):
        self.client.sendto(gateway.encode(0x05, b"\1"), ("127.0.0.1", self.port))
        await asyncio.sleep(0.03)
        await self.relay.close()
        await asyncio.sleep(0.02)
        self.assertIsNone(self.relay.owner)
        self.assertIn(gateway.encode(0x05, b"\0"), self.tcp_packets)
        self.assertEqual(await asyncio.wait_for(self.reader.read(1), 1), b"")


if __name__ == "__main__":
    unittest.main()
