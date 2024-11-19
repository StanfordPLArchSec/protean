#!/usr/bin/python3
import argparse
import os
import glob
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument('cptdir')
parser.add_argument('expdirs', nargs='+')
args = parser.parse_args()

successes = []
exit_code = 0

for expdir in args.expdirs:
    for ipc_path in glob.glob(f'{expdir}/*/ipc.txt'):
        cpt_ipcs = glob.glob(os.path.join(os.path.dirname(ipc_path), '[0-9]'))
        bench = os.path.basename(os.path.dirname(ipc_path))
        cptroot = os.path.join(args.cptdir, bench, 'cpt', 'm5out')
        cmd = ['python3', 'helpers/bench-ipc.py', cptroot]
        skip = False
        for cpt_ipc in cpt_ipcs:
            path = os.path.join(cpt_ipc, 'ipc.txt')
            if not os.path.exists(path):
                skip = True
                break
            cmd.append(path)
        if skip:
            print(f'Failed: missing ipc.txt', file=sys.stderr)
            continue
        result = subprocess.run(cmd, capture_output=True)
        if result.returncode:
            print(f'Failed:', *cmd, file=sys.stderr)
            sys.stderr.buffer.write(result.stdout)
            sys.stderr.buffer.write(result.stderr)
            exit_code = 1
            continue

        with open(ipc_path, 'w') as f:
            f.buffer.write(result.stdout)

        successes.append(ipc_path)


for success in successes:
    with open(success) as f:
        print('SUCCESS:', success, f.read(), end='')
