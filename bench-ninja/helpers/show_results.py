#!/usr/bin/python3

import os
import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('exp', nargs='+')
args = parser.parse_args()

# compute the bench names
benches = set()
for exp in args.exp:
    for bench in os.listdir(exp):
        if os.path.isdir(os.path.join(exp, bench)):
            benches.add(bench)

# print results
benches = sorted(list(benches))
# print('bench', *map(lambda path: path.split('/')[-1], args.exp))
for bench in benches:
    ipcs = list()
    for exp in args.exp:
        path = os.path.join(exp, bench, 'ipc.txt')
        if os.path.exists(path):
            with open(os.path.join(exp, bench, 'ipc.txt')) as f:
                ipc = f'{float(f.read()):.3}'
        else:
            ipc = '-'
        ipcs.append(ipc)
    print(bench, *ipcs)
