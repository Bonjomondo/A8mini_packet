#!/usr/bin/python3
"""Decode real H.265 RTSP/TCP on Jetson and save frame progress evidence."""
import argparse
import datetime
import json
import pathlib
import time

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

parser = argparse.ArgumentParser()
parser.add_argument('--url', default='rtsp://127.0.0.1:8554/a8mini')
parser.add_argument('--seconds', type=float, default=120)
parser.add_argument('--output', default='artifacts/proxy-decode.json')
parser.add_argument('--credentials', default='artifacts/client-credentials.json')
parser.add_argument('--no-auth', action='store_true', help='Probe the camera directly')
args = parser.parse_args()
Gst.init(None)
source = Gst.ElementFactory.make('rtspsrc', 'source')
source.set_property('location', args.url)
if not args.no_auth:
    credentials = json.loads(pathlib.Path(args.credentials).read_text())
    source.set_property('user-id', credentials['username'])
    source.set_property('user-pw', credentials['password'])
source.set_property('protocols', 4)  # GstRtsp.RTSPLowerTrans.TCP
source.set_property('latency', 300)
pipeline = Gst.parse_launch('rtph265depay name=depay ! h265parse ! nvv4l2decoder ! fakesink name=sink sync=false signal-handoffs=true')
pipeline.add(source)
source.connect('pad-added', lambda src, pad: pad.link(pipeline.get_by_name('depay').get_static_pad('sink')) if not pipeline.get_by_name('depay').get_static_pad('sink').is_linked() else None)
start = time.monotonic()
stats = {'url': args.url, 'timestamp': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(), 'duration_requested_s': args.seconds, 'latency_ms': 300, 'decoder': 'nvv4l2decoder', 'frames': 0, 'first_frame_s': None, 'max_frame_gap_s': 0, 'errors': [], 'warnings': [], 'samples': []}
last_frame = None

def handoff(sink, buffer, pad):
    global last_frame
    now = time.monotonic()
    stats['frames'] += 1
    if last_frame is None:
        stats['first_frame_s'] = now - start
        stats['caps'] = pad.get_current_caps().to_string()
    else:
        stats['max_frame_gap_s'] = max(stats['max_frame_gap_s'], now - last_frame)
    last_frame = now

pipeline.get_by_name('sink').connect('handoff', handoff)
bus = pipeline.get_bus()
pipeline.set_state(Gst.State.PLAYING)
next_sample = start + 10
try:
    while time.monotonic() - start < args.seconds:
        msg = bus.timed_pop_filtered(100 * Gst.MSECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS | Gst.MessageType.WARNING)
        now = time.monotonic()
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                error, debug = msg.parse_error()
                stats['errors'].append({'error': str(error), 'debug': debug})
                break
            if msg.type == Gst.MessageType.EOS:
                stats['errors'].append({'error': 'Unexpected EOS'})
                break
            warning, debug = msg.parse_warning()
            stats['warnings'].append({'warning': str(warning), 'debug': debug})
        if last_frame is None and now - start > 25:
            stats['errors'].append({'error': 'No first frame within 25s'})
            break
        if last_frame is not None and now - last_frame > 5:
            stats['errors'].append({'error': 'No frame progress for 5s'})
            break
        if now >= next_sample:
            sample = {'elapsed_s': round(now - start, 3), 'frames': stats['frames']}
            stats['samples'].append(sample)
            print(json.dumps(sample), flush=True)
            next_sample += 10
finally:
    stats['elapsed_s'] = time.monotonic() - start
    pipeline.set_state(Gst.State.NULL)
stats['average_fps'] = stats['frames'] / max(0.001, stats['elapsed_s'] - (stats['first_frame_s'] or 0))
stats['passed'] = not stats['errors'] and stats['frames'] > 0 and stats['elapsed_s'] >= args.seconds and stats['average_fps'] >= 5
pathlib.Path(args.output).write_text(json.dumps(stats, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(stats, ensure_ascii=False, indent=2))
raise SystemExit(0 if stats['passed'] else 1)
