#!/usr/bin/python3

import sys

lines = sys.stdin.readlines()

def get_count(line) -> int:
    tokens = line.split()
    return float(tokens[0])

total_count = 0
for line in lines:
    total_count += get_count(line)

partial_count = 0
for line in lines:
    count = get_count(line)
    partial_count += count
    pct = count / total_count * 100
    cum_pct = partial_count / total_count * 100
    print(f'{line.strip()} {pct:.2f}% {cum_pct:.2f}%')
