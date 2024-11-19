#!/usr/bin/python3

import argparse
import sys
import collections

parser = argparse.ArgumentParser()
parser.add_argument('files', nargs='*')
args = parser.parse_args()

# This script will create a histogram of the input(s).

files = [open(path) for path in args.files]
if len(files) == 0:
    files = [sys.stdin]


hist = collections.defaultdict(int)
for file in files:
    for line in file:
        hist[line] += 1

for line, count in hist.items():
    print(count, line.strip())

