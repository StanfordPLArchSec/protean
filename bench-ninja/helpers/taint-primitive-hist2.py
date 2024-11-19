#!/usr/bin/python3

import argparse
import collections
import sys
import os
import multiprocessing
import gzip

parser = argparse.ArgumentParser()
parser.add_argument('cptdir')
parser.add_argument('expdir')
args = parser.parse_args()

# Get weights.
weights_path = os.path.join(args.cptdir, 'spt', 'weights.out')
weights_dict = dict()
with open(weights_path) as f:
    for line in f:
        tokens = line.split()
        w = float(tokens[0])
        i = int(tokens[1])
        weights_dict[i] = w


def hist_for_trace(cptid):
    dbgout_path = os.path.join(args.expdir, f'{cptid}', 'm5out', 'dbgout.txt.gz')
    dbgout = gzip.open(dbgout_path, 'rt')
    hist = collections.defaultdict(int)
    for line in dbgout:
        tokens = line.split()
        if tokens < 3 or tokens[0] != 'TPE' or not tokens[1].endswith('-taint'):
            continue
        
        hist[line] += 1

    
