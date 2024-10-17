#!/usr/bin/python3

import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--invert', action = 'store_true')
args = parser.parse_args()

for line in sys.stdin:
    tokens = line.split()
    if len(tokens) == 0:
        continue
    output = [tokens[0]]
    assert len(tokens) >= 2
    ref = None
    if tokens[1] != '-':
        ref = float(tokens[1])
    for exp_s in tokens[1:]:
        exp = None
        if exp_s != '-':
            exp = float(exp_s)
        if ref and exp:
            val = exp / ref
            if args.invert:
                val = 1 / val
            output.append(f'{val :.9}')
        else:
            output.append('-')
    print(*output)
