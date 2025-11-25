#!/usr/bin/python3

import os
import sys
import argparse
import json

parser = argparse.ArgumentParser()
parser.add_argument('exp', nargs='+')
parser.add_argument('--metric', default = 'ipc', choices = ['ipc', 'insts', 'cycles'])
parser.add_argument('--ignore', '-i', action='append', default=list())
args = parser.parse_args()

# compute the bench names
benches = set()
for exp in args.exp:
    for bench in os.listdir(exp):
        if os.path.isdir(os.path.join(exp, bench)):
            benches.add(bench)

for ignore in args.ignore:
    benches.remove(ignore)
            
# print results
benches = sorted(list(benches))
# print('bench', *map(lambda path: path.split('/')[-1], args.exp))
for bench in benches:
    ipcs = list()
    for exp in args.exp:
        path = os.path.join(exp, bench, 'results.json')
        if os.path.exists(path):
            with open(path) as f:
                j = json.load(f)
                w = j['weight']
                if not (0.999 < w and w < 1.001):
                    print(f'warn: weight for {bench} is {w:.2}', file = sys.stderr)
                ipcs.append(j['stats'][args.metric])
                continue
        ipcs.append('-')
    print(bench, *ipcs)
