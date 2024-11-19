#!/usr/bin/python3

import os
import argparse
import glob
import collections

parser = argparse.ArgumentParser()
parser.add_argument('cptdir')
parser.add_argument('expdirs', nargs='+')
parser.add_argument('--delete', '-d', action='store_true')
args = parser.parse_args()

cpts = collections.defaultdict(set)
for path in glob.glob(f'{args.cptdir}/*/cpt/m5out/cpt.simpoint_*'):
    components = path.split('/')
    bench = components[-4]
    dstr = components[-1]
    tokens = list(dstr.removeprefix('cpt.').split('_'))
    d = dict(zip(tokens[0::2], tokens[1::2]))
    simpoint = d['simpoint']
    cpts[bench].add(int(simpoint, 10))

for e_ in args.expdirs:
    for expdir in glob.glob(f'{e_}/*'):
        components = expdir.split('/')
        bench = components[-1]
        if not bench.startswith('6'):
            continue
        cptid = max(cpts[bench])

        if args.delete and os.path.exists(f'{expdir}/{cptid}/copy.stamp'):
            os.remove(f'{expdir}/{cptid}/copy.stamp')
        
        print(f'{expdir}/{cptid}/ipc.txt')
