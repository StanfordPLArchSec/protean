import argparse
import sys
import os

parser = argparse.ArgumentParser()
parser.add_argument('run_under', nargs = '*')
parser.add_argument('--name', required = True)
parser.add_argument('--site', type = bool, default = False)
args = parser.parse_args()

for line in sys.stdin:
    if line.startswith('config.name = '):
        print(f'config.name = "{args.name}"')
    elif line.startswith('config.run_under = '):
        print('config.run_under = ' + '"' + ' '.join(args.run_under) + '"')
    else:
        line = line.replace('"lit.cfg"', f'"{args.name}.lit.cfg"')
        print(line, end = '')
