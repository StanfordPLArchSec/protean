#!/usr/bin/python3

import sys
import argparse
import gzip

parser = argparse.ArgumentParser()
parser.add_argument('--simpoints', required = True)
parser.add_argument('--bbv', required = True)
args = parser.parse_args()


# Parse intervals.
intervals = set()
with open(args.simpoints) as f:
    for line in f:
        tokens = line.split()
        assert len(tokens) == 2
        interval = int(tokens[0])
        intervals.add(interval)

# Iterate over bbv file.
calls = set()

def register_call_line(line: str):
    tokens = line.split()
    assert len(tokens) == 2
    assert tokens[0] == 'call'
    calls.add(int(tokens[1]))

with gzip.open(args.bbv, 'rt') as f:
    interval = 0
    lines = f.readlines()
    # Retain only 'T' and 'calls' lines.
    lines = [line for line in lines if \
             line.startswith('T') or line.startswith('calls')]
    for i, line in enumerate(lines):
        if line.startswith('T'):
            if interval in intervals:
                # This interval is selected as a SimPoint.
                # Grab the start and end call counts.
                assert i > 0 and i < len(lines) - 1
                register_call_line(lines[i - 1])
                register_call_line(lines[i + 1])
            interval += 1
        
