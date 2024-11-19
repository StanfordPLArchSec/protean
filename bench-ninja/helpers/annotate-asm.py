#!/bin/python3

import sys
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('asm')
args = parser.parse_args()


asm_map = dict()
with open(args.asm) as f:
    for line in f:
        line = line.strip()
        tokens = line.split(':')
        try:
            addr = int(tokens[0], 16)
            addr_s = f'0x{addr:x}'
            asm_map[addr_s] = ':'.join(tokens[1:])
        except:
            pass

for line in sys.stdin:
    addr = line.split()[2]
    if addr in asm_map:
        print(line.strip(), asm_map[addr])
    else:
        print(line.strip())

        
