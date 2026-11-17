#!/usr/bin/env python3
"""Measure chooser control reads with the application's native wxPython runtime.

Run with the packaged Python (which provides wx) on the platform being measured.
This isolates native getter overhead; it does not measure full chooser latency.
No application preferences or projects are modified. The frame stays hidden.
"""
import argparse
import json
import statistics
import time

import wx


def measure(nodes, repeats):
    app = wx.App(False)
    frame = wx.Frame(None, title='CircuitAliveEDA control benchmark')
    choice = wx.Choice(frame, choices=['All', 'Favorites', 'Recent'])
    pins = wx.SpinCtrl(frame, min=0, max=2048, initial=0)

    def repeated(count):
        total = 0
        for _ in range(count):
            if choice.GetSelection() == 1:
                total += 1
            if choice.GetSelection() == 2:
                total += 2
            total += pins.GetValue() if pins.GetValue() else 0
        return total

    def snapshot(count):
        selected = choice.GetSelection()
        pin_count = pins.GetValue()
        total = 0
        for _ in range(count):
            if selected == 1:
                total += 1
            if selected == 2:
                total += 2
            total += pin_count if pin_count else 0
        return total

    results = []
    try:
        for count in nodes:
            for selected, pin_count in [(0, 0), (1, 14), (2, 2)]:
                choice.SetSelection(selected)
                pins.SetValue(pin_count)
                timings, outputs = {}, {}
                for name, function in [('repeated', repeated), ('snapshot', snapshot)]:
                    samples = []
                    for _ in range(repeats):
                        start = time.perf_counter()
                        outputs[name] = function(count)
                        samples.append(time.perf_counter() - start)
                    timings[name] = statistics.median(samples)
                if outputs['repeated'] != outputs['snapshot']:
                    raise RuntimeError('Control snapshot changed the filter inputs')
                results.append(dict(nodes=count, selection=selected, pins=pin_count,
                                    seconds=timings, ratio=timings['repeated'] / timings['snapshot']))
    finally:
        frame.Destroy()
    return dict(wx_version=wx.version(), results=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nodes', type=int, nargs='+', default=[22923, 50000])
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1 or any(count < 1 for count in args.nodes):
        parser.error('Use positive node counts and repeat count')
    print(json.dumps(measure(args.nodes, args.repeats), indent=2))
