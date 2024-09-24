#!/usr/bin/python3

import os
import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('exp', nargs='+')
parser.add_argument('--ignore', '-i', action='append')
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
        path = os.path.join(exp, bench, 'ipc.txt')
        if os.path.exists(path):
            with open(os.path.join(exp, bench, 'ipc.txt')) as f:
                tokens = f.read().split()
                tokens = [float(token) for token in tokens]
                if len(tokens) != 2:
                    print(bench, exp, tokens, file=sys.stderr)
                assert len(tokens) == 2
                assert tokens[1] >= 0.99
                ipc = f'{tokens[0]:.3}'
        else:
            ipc = '-'
        ipcs.append(ipc)
    print(bench, *ipcs)
