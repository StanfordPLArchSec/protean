#!/usr/bin/python3

import sys
import gzip
import argparse
import gzip
import operator
import collections

parser = argparse.ArgumentParser()
parser.add_argument('dbgout', nargs = '*')
parser.add_argument('-n', '--count', type = int)
parser.add_argument('--reg', '-r', action='store_true')
parser.add_argument('--mem', '-m', action='store_true')
parser.add_argument('--xmit', '-x', action='store_true')
args = parser.parse_args()

def open_dbgout(path: str):
    if path.endswith('.gz'):
        return gzip.open(path, 'rt')
    else:
        return open(path)

files = [open_dbgout(path) for path in args.dbgout]

# 70915340918500: system.switch_cpus: TPE m-taint 0x271bdf   MOV_R_M : ld   ecx, SS:[t0 + rsp + 0x4]
histo = collections.defaultdict(int)
asms = dict()
for f in files:
    for line in f:
        tokens = line.split()
        if len(tokens) < 5 or tokens[2] != 'TPE':
            continue
        tp = tokens[3]
        if (args.reg and tp == 'r-taint') or \
           (args.mem and tp == 'm-taint') or \
           (args.xmit and tp == 'x-taint'):
            inst_addr = tokens[4]
            histo[inst_addr] += 1
            asms[inst_addr] = tokens[6]

# At the end, print the top n.
sorted_histo = sorted(histo.items(), key = operator.itemgetter(1), reverse = True)
n = len(sorted_histo)
if args.count:
    n = args.count
for i in range(n):
    inst_addr, count = sorted_histo[i]
    asm = asms[inst_addr]
    print(count, inst_addr, asm)
    
