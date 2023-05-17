import sys
import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument('orig')
args = parser.parse_args()

with open(args.orig) as f:
    lines = f.read().splitlines()

# sort into categories based on first token
cmds = {
    "RUN:" : [],
    "VERIFY:" : [],
}
for line in lines:
    cmds[line.split()[0]].append(line)

orig_no_ext = args.orig.removesuffix(".test")

new_test_paths = []

if len(cmds["RUN:"]) == 1:
    print(args.orig)
    exit(0)

if len(cmds["RUN:"]) != len(cmds["VERIFY:"]):
    print(f'Failed to parse test file at {args.orig}', file = sys.stderr)
    assert False
    
for i, (run, verify) in enumerate(zip(cmds["RUN:"], cmds["VERIFY:"], strict = True)):
    new_test_path = f'{orig_no_ext}_{i}.test'
    with open(new_test_path, "w") as f:
        print(run, file = f)
        print(verify, file = f)
    new_test_paths.append(new_test_path)

print(';'.join(new_test_paths))
os.remove(args.orig)
