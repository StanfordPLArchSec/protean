#!/usr/bin/python3

import argparse
import json
import types
import sys
import math
import collections

parser = argparse.ArgumentParser()
parser.add_argument('--output', required = True, help = '(Output) Path to aggregated output JSON')
parser.add_argument('inputs', nargs = '+', help = '(Input) Result JSONs for individual SimPoint runs')
args = parser.parse_args()

inputs = []
for path in args.inputs:
    with open(path) as f:
        j = json.load(f)
        if type(j) is dict and len(j) == 0:
            continue
        inputs.append(j)

# Get keys.
assert len(inputs) > 0
keys = inputs[0]['stats'].keys()
for i, input in enumerate(inputs):
    if input['stats'].keys() != keys:
        print(f'found mismatch in results keys at index {i}!', file = sys.stderr)
        a = set(keys)
        b = set(input['stats'].keys())
        for key in a - b:
            print(f'only in 0: {key}', file = sys.stderr)
        for key in b - a:
            print(f'only in {i}: {key}', file = sys.stderr)
        exit(1)

total_stats = dict(map(lambda key: (key, 0), keys))
total_weights = collections.defaultdict(int)
total_weight = 0
for input in inputs:
    weight = input['simpoint']['weight']
    total_weight += weight
    for key in keys:
        value = input['stats'][key]
        if math.isnan(value):
            continue
        total_stats[key] += value * weight
        total_weights[key] += weight
for key in keys:
    if total_weights[key]:
        total_stats[key] /= total_weights[key]
    else:
        total_stats[key] = 0
results = {
    'weight': total_weight,
    'stats': total_stats,
}
        
with open(args.output, 'wt') as f:
    json.dump(results, f)
