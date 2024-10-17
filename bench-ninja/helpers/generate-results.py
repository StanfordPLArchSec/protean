#!/usr/bin/python3

import argparse
import json
import types
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--stats', required = True, help = '(Input) Path to m5out/stats.txt')
parser.add_argument('--simpoints-json', required = True, help = '(Input) Path to cpt/simpoints.json')
parser.add_argument('--simpoint-idx', required = True, type = int, help = '(Input) Simpoint index')
parser.add_argument('--output', required = True, help = '(Output) Path to out JSON')
args = parser.parse_args()

simpoints = None
with open(args.simpoints_json) as f:
    simpoints = json.load(f)
    simpoints = [types.SimpleNamespace(**simpoint) for simpoint in simpoints]

def find_simpoint():
    for simpoint in simpoints:
        if int(simpoint.name) == args.simpoint_idx:
            return simpoint
    print('error: failed to find requested simpoint', file = sys.stderr)
    exit(1)

simpoint = find_simpoint()

keys = {
    'system.switch_cpus.commitStats0.numInsts': 'insts',
    'system.switch_cpus.ipc': 'ipc',
    'system.switch_cpus.numCycles': 'cycles',
}
stats = dict(map(lambda x: (x, None), keys.values()))
with open(args.stats) as f:
    for line in f:
        tokens = line.split()
        if len(tokens) < 2:
            continue
        key = tokens[0]
        if key in keys:
            stat = keys[key]
            stats[stat] = float(tokens[1])

expected_insts = simpoint.inst_range[1] - simpoint.inst_range[0]
actual_insts = stats['insts']
assert abs(expected_insts - actual_insts) < 100
for value in stats.values():
    assert value is not None

results = {
    'simpoint': simpoint.__dict__,
    'stats': stats,
}

with open(args.output, 'wt') as f:
    json.dump(results, f)
