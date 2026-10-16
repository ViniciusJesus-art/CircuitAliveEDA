#!/usr/bin/env python3
"""Measure the real worker across resumed windows; no schematic GUI is involved."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--worker', type=Path, default=ROOT / 'build/circuitalive/eeschema/sim/circuitalive-sim-worker')
parser.add_argument('--windows', type=int, default=200)
parser.add_argument('--window', type=float, default=0.01)
parser.add_argument('--output', type=Path, default=ROOT / '.circuitalive/logs/interactive-memory.json')
args = parser.parse_args()
if args.windows < 10 or not math.isfinite(args.window) or args.window <= 0:
    parser.error('Use at least 10 windows and a positive finite window duration')

deck = 'RC memory measurement\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1u ic=0\n.end\n'
observations = []
started = time.monotonic()
with subprocess.Popen([str(args.worker)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                      text=True) as worker:
    def request(message):
        worker.stdin.write(json.dumps(message, allow_nan=False) + '\n')
        worker.stdin.flush()
        response = json.loads(worker.stdout.readline())
        if not response['ok']:
            raise RuntimeError(response['error'])
        return response

    try:
        request(dict(op='hello'))
        request(dict(op='configure', netlist=deck, step=1e-6, max_step=1e-6,
                     horizon=(args.windows + 1) * args.window, capacity=64,
                     signals=['v(out)', 'i(v1)'], uic=True))
        for index in range(1, args.windows + 1):
            request(dict(op='advance', time=index * args.window))
            snap = request(dict(op='snapshot', drain=True))
            assert snap['state'] == 'paused' and snap['retained'] == 1
            assert len(snap['history']) <= 64
            assert abs(snap['time'] - index * args.window) < 1e-10
            expected = 1 - math.exp(-snap['time'] / 0.001)
            assert abs(snap['values'][0] - expected) < 2e-5
            observations.append({key: snap[key] for key in
                                 ('time', 'samples', 'resident_bytes', 'retained', 'generation')})
        request(dict(op='stop'))
    finally:
        worker.stdin.close()
        worker.stdin = None
        try:
            worker.wait(timeout=2)
        except subprocess.TimeoutExpired:
            worker.kill()
            worker.wait()

warm = observations[9:]
result = dict(windows=args.windows, samples=observations[-1]['samples'],
              simulated_seconds=observations[-1]['time'],
              wall_seconds=time.monotonic() - started,
              engine_retained_points=observations[-1]['retained'], history_capacity=64,
              resident_min_after_warmup=min(p['resident_bytes'] for p in warm),
              resident_max_after_warmup=max(p['resident_bytes'] for p in warm),
              observations=observations,
              scope='Analog RC, finite configured horizon, ngspice shared library, Linux')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items() if k != 'observations'}, indent=2))
