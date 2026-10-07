#!/usr/bin/python3
"""Run three real decode consumers and retain each result, including failures."""
import concurrent.futures
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent
jobs = [
    ('direct-host', 'rtsp://192.168.144.25:8554/main.264', True),
    ('wired-proxy-host', 'rtsp://192.168.1.5:8554/a8mini', False),
    ('wifi-proxy-host', 'rtsp://192.168.2.113:8554/a8mini', False),
]

def run(job):
    name, url, no_auth = job
    cmd = ['/usr/bin/python3', str(root / 'scripts/probe-video.py'), '--seconds', '60', '--url', url, '--output', str(root / 'artifacts' / (name + '.json')), '--credentials', str(root / 'artifacts/client-credentials.json')]
    if no_auth:
        cmd.append('--no-auth')
    with (root / 'artifacts' / (name + '.log')).open('w') as stream:
        result = subprocess.run(cmd, cwd=str(root), stdout=stream, stderr=subprocess.STDOUT, timeout=90)
    return {'test': name, 'exit_code': result.returncode}

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    results = list(pool.map(run, jobs))
(root / 'artifacts/concurrent-host-tests.json').write_text(json.dumps(results, indent=2) + '\n')
print(json.dumps(results, indent=2))
sys.exit(0 if all(item['exit_code'] == 0 for item in results) else 1)
