#!/usr/bin/python3

import sys
import gzip
import argparse
import gzip
import operator
import collections
import os

parser = argparse.ArgumentParser()
parser.add_argument('dbgout', nargs = '+')
parser.add_argument('-n', '--count', type = int)
parser.add_argument('--reg', '-r', default=True)
parser.add_argument('--mem', '-m', default=True)
parser.add_argument('--xmit', '-x', default=True)
parser.add_argument('-w', '--weights', type=str, required=True)
args = parser.parse_args()

weight_dict = dict()
with open(args.weights) as f:
    for line in f:
        tokens = line.split()
        w = float(tokens[0])
        i = int(tokens[1])
        weight_dict[i] = w

def open_dbgout(path: str):
    if path.endswith('.gz'):
        f = gzip.open(path, 'rt')
    else:
        f = open(path)
    if args.weights:
        cpt_idx = int(path.split('/')[-3])
        weight = weight_dict[cpt_idx]
    else:
        weight = 1
    return f, weight


weighted_files = [open_dbgout(path) for path in args.dbgout]

# TPE m-taint 0x3c6888
histos = dict()
r_histo = collections.defaultdict(int)
m_histo = collections.defaultdict(int)
x_histo = collections.defaultdict(int)
asms = dict()
for f, w in weighted_files:
    for line in f:
        tokens = line.split()
        if len(tokens) < 3 or tokens[0] != 'TPE':
            continue
        tp = tokens[1]
        inst_addr = tokens[2]
        asms[inst_addr] = tokens[4]
        if args.reg and tp == 'r-taint':
            r_histo[inst_addr] += w
        if args.mem and tp == 'm-taint':
            m_histo[inst_addr] += w
        if args.xmit and tp == 'x-taint':
            x_histo[inst_addr] += w

# At the end, print the top n.
def tag_histo(tag, histo):
    return [(*item, tag) for item in histo.items()]
tagged_histo = tag_histo('r', r_histo) + tag_histo('m', m_histo) + tag_histo('x', x_histo)
sorted_histo = sorted(tagged_histo, key = operator.itemgetter(1), reverse = True)
n = len(sorted_histo)
if args.count:
    n = args.count
for i in range(n):
    inst_addr, count, tag = sorted_histo[i]
    asm = asms[inst_addr]
    print(round(count), inst_addr, tag, asm)
