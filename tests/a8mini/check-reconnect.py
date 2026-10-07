#!/usr/bin/env python3
"""Drop only this Probe's TCP connection; never restart Orin services."""
import asyncio
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


async def main():
    writers = set()
    available = True

    async def client(reader, writer):
        if not available:
            writer.close()
            return
        writers.add(writer)
        upstream = None
        try:
            source, upstream = await asyncio.open_connection("192.168.2.113", 8554)
            writers.add(upstream)

            async def copy(src, dst):
                while data := await src.read(65536):
                    dst.write(data)
                    await dst.drain()

            jobs = [asyncio.create_task(copy(reader, upstream)), asyncio.create_task(copy(source, writer))]
            await asyncio.wait(jobs, return_when=asyncio.FIRST_COMPLETED)
            for job in jobs:
                job.cancel()
            await asyncio.gather(*jobs, return_exceptions=True)
        except (ConnectionError, OSError):
            pass
        finally:
            writer.close()
            writers.discard(writer)
            if upstream:
                upstream.close()
                writers.discard(upstream)

    server = await asyncio.start_server(client, "127.0.0.1", 18554)
    report = ROOT / "artifacts/probe-macos-reconnect.json"
    process = await asyncio.create_subprocess_exec(
        "bash", str(ROOT / "scripts/run-probe-macos.sh"),
        "--url", "rtsp://127.0.0.1:18554/a8mini",
        "--seconds", "25", "--report", str(report), cwd=ROOT,
    )
    try:
        await asyncio.sleep(7)
        available = False
        for writer in list(writers):
            writer.close()
        print("Closed this client's connection for 4 seconds", flush=True)
        await asyncio.sleep(4)
        available = True
        result = await asyncio.wait_for(process.wait(), timeout=30)
        evidence = json.loads(report.read_text())
        recovered = result == 0 and evidence["passed"] and evidence["reconnects"] >= 1
        evidence["fault_injection"] = {"connection_dropped_at_s": 7, "unavailable_s": 4,
                                       "exit_code": result, "recovered": recovered}
        report.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n")
        return 0 if recovered else 1
    finally:
        server.close()
        await server.wait_closed()
        for writer in list(writers):
            writer.close()
        if process.returncode is None:
            process.terminate()
            await process.wait()


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
