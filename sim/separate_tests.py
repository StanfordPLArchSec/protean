import sys
import argparse
import os
import collections

parser = argparse.ArgumentParser()
parser.add_argument('orig')
args = parser.parse_args()

with open(args.orig) as f:
    lines = f.read().splitlines()


cmds_by_outfile = collections.defaultdict(list)
cmds_by_directive = collections.defaultdict(list)
for line in lines:
    tokens = line.split()
    cmds_by_outfile[tokens[-1]].append(line)
    cmds_by_directive[tokens[0]].append(line)

if len(cmds_by_directive['RUN:']) <= 1:
    print(args.orig)
    exit(0)

orig_no_ext = args.orig.removesuffix(".test")
new_test_paths = []
for i, cmdlist in enumerate(cmds_by_outfile.values()):
    assert len(cmdlist) <= 2
    new_test_path = f'{orig_no_ext}_{i}.test'
    with open(new_test_path, "w") as f:
        for cmd in cmdlist:
            print(cmd, file = f)
    new_test_paths.append(new_test_path)

print(';'.join(new_test_paths))
os.remove(args.orig)
