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
            if 10000000 <= simpoint.inst_count and simpoint.inst_count <= 100000000:
                return simpoint
    # print('error: failed to find requested simpoint', file = sys.stderr)
    # exit(1)
    with open(args.output, 'wt') as f:
        print('{}', file = f)
    exit(0)

simpoint = find_simpoint()

class Stat:
    def __init__(self, name: str, required: bool = True):
        self.name = name
        self.required = required
        self.value = None

key_insts = 'system.switch_cpus.commitStats0.numInsts'
stats = {
    key_insts: Stat('insts'),
    'system.switch_cpus.ipc': Stat('ipc'),
    'system.switch_cpus.numCycles': Stat('cycles'),
    'system.switch_cpus.commit.committedUnprotectedAnnotationsRate': Stat('unprot-annot-rate', required = False),
}
# stats = dict(map(lambda x: (x.name(), None), keys.values()))
with open(args.stats) as f:
    for line in f:
        tokens = line.split()
        if len(tokens) < 2:
            continue
        key = tokens[0]
        if key in stats:
            stat = stats[key]
            stat.value = float(tokens[1])

expected_insts = simpoint.inst_range[1] - simpoint.inst_range[0]
actual_insts = stats[key_insts].value
if abs(expected_insts - actual_insts) < 0:
    print(f'error: expected {expected_insts} instructions, actual {actual_insts}',
          file = sys.stderr)
    exit(1)

for stat in stats.values():
    if stat.required:
        assert stat.value is not None

def get_stats_dict(stats) -> dict:
    d = dict()
    for stat in stats.values():
        if stat.value is not None:
            d[stat.name] = stat.value
    return d
        
results = {
    'simpoint': simpoint.__dict__,
    'stats': get_stats_dict(stats),
}

with open(args.output, 'wt') as f:
    json.dump(results, f)
