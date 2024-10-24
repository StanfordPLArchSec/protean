#!/usr/bin/python3

import argparse
import gzip
import os
import sys
import json
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--simpoints', required = True, help = 'Path to simpoints.out')
parser.add_argument('--weights', required = True, help = 'Path to weights.out')
parser.add_argument('--bbv', required = True, help = 'Path to bbv.out.gz')
parser.add_argument('--funcs', required = True, help = '(Output, temp) Path to funcs.out')
parser.add_argument('--insts', required = True, help = '(Output, temp) Path to insts.out')
parser.add_argument('--output', required = True, help = '(Output) JSON file describing normalized SimPoint')
parser.add_argument('--skip-pin', action = 'store_true', help = '(Debug) skip running Pin; mainly used for script debugging')
parser.add_argument('--early-exit', action = 'store_true', help = '(Input) Allow early exit')
parser.add_argument('cmd')
parser.add_argument('args', nargs = '*')
args = parser.parse_args()

class SimPoint:
    def __init__(self, name: str, weight: float = None, interval: int = None, func_range: tuple = None):
        self.name = name
        self.interval = interval
        self.weight = weight
        self.func_range = func_range
        self.inst_range = None

    def as_dict(self) -> dict:
        return {
            'name': self.name,
            'interval': self.interval,
            'weight': self.weight,
            'func_range': [*self.func_range],
            'func_count': self.func_count(),
            'inst_range': [*self.inst_range],
            'inst_count': self.inst_count(),
        }

    def func_count(self) -> int:
        return self.func_range[1] - self.func_range[0]

    def inst_count(self) -> int:
        return self.inst_range[1] - self.inst_range[0]

simpoints = dict()

def filterfile(f):
    f = map(str.strip, f)
    f = filter(lambda line: not (len(line) == 0 or line.startswith('#')), f)
    f = map(str.split, f)
    return f

# Parse simpouts.out
with open(args.simpoints) as f:
    for tokens in filterfile(f):
        assert len(tokens) == 2
        name = tokens[1]
        interval = int(tokens[0])
        assert name not in simpoints
        simpoints[name] = SimPoint(name = name, interval = interval)

# Parse weights.out
with open(args.weights) as f:
    for tokens in filterfile(f):
        assert len(tokens) == 2
        name = tokens[1]
        weight = float(tokens[0])
        simpoints[name].weight = weight
# Sanity check weights
total_weight = sum([simpoint.weight for simpoint in simpoints.values()])
assert 0.999 < total_weight and total_weight < 1.001

# Parse bbv.out.gz
with gzip.open(args.bbv, 'rt') as f:
    # Map intervals of interest to simpoint/
    intervals = dict()
    for simpoint in simpoints.values():
        assert simpoint.interval not in intervals
        intervals[simpoint.interval] = simpoint

    func_begin = 0
    lines = f.readlines()
    interval = 0
    for i, line in enumerate(lines):
        tokens = line.strip().split()
        if not (len(tokens) >= 5 and tokens[0] == '#' and tokens[1] == 'func-range'):
            continue
        interval, func_begin, func_end = tuple(list(map(int, tokens[2:]))[:3])
        if interval in intervals:
            intervals[interval].func_range = (func_begin, func_end)

    # Make sure we hit all the SimPoints' intervals.
    for simpoint in simpoints.values():
        assert simpoint.func_range

assert len(simpoints) > 0

# Tighten the simpoints function ranges to avoid large overshoots.
for simpoint in simpoints.values():
    if simpoint.func_range[1] - simpoint.func_range[0] > 2:
        func_begin, func_end = simpoint.func_range
        simpoint.func_range = (func_begin + 1, func_end - 1)

# Create list of function counts for PinTool.
funcs = set()
for simpoint in simpoints.values():
    func_begin, func_end = simpoint.func_range
    funcs.add(func_begin)
    funcs.add(func_end)
funcs = list(funcs)
funcs.sort()
with open(args.funcs, 'wt') as f:
    for func in funcs:
        print(func, file = f)

# Run Pin.
if not args.skip_pin:
    flags = []
    if args.early_exit:
        flags = ['-allow-early-exit', '1']
    # pin_cmd = [args.pin, '-t', args.pintool, '-i', args.funcs, '-o', args.insts, *flags, '--', args.cmd, *args.args]
    pin_cmd = [args.cmd, *args.args]
    print('Running Pin command:', *pin_cmd, file = sys.stderr)
    subprocess.run(pin_cmd, check = True)

# Parse instruction counts.
func_to_inst = dict()
with open(args.insts) as f:
    for line in f:
        if line.startswith('#'):
            continue
        tokens = [int(token) for token in line.split()]
        assert len(tokens) == 2
        func_to_inst[tokens[0]] = tokens[1]
assert list(funcs) == list(func_to_inst.keys())

# Annotate SimPoints with instruction counts.
for simpoint in simpoints.values():
    func_begin, func_end = simpoint.func_range
    inst_begin = func_to_inst[func_begin]
    inst_end = func_to_inst[func_end]
    simpoint.inst_range = (inst_begin, inst_end)

# Output SimPoints.
simpoints = list(simpoints.values())
simpoints.sort(key = lambda simpoint: simpoint.func_range[0])
simpoints = [simpoint.as_dict() for simpoint in simpoints]
with open(args.output, 'wt') as f:
    json.dump(simpoints, f)
