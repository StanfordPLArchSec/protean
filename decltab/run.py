import os
import sys
import argparse
import gzip
import multiprocessing
import subprocess
import shutil
import math
from collections import defaultdict as ddict

parser = argparse.ArgumentParser()
parser.add_argument('dir')
parser.add_argument('decltab_cmd', nargs='+')
parser.add_argument('--jobs', '-j', type = int, default = multiprocessing.cpu_count() + 2)
args = parser.parse_args()

jobs_sema = multiprocessing.BoundedSemaphore(args.jobs)

def mylistdir(dir):
    paths = [(filename, os.path.join(dir, filename)) for filename in os.listdir(dir)]
    return [(filename, path) for filename, path in paths if os.path.isdir(path)]

def get_cpt_weight(cpt):
    cpt_tokens = cpt.split('_')
    cpt_dict = dict(zip(cpt_tokens[0::2], cpt_tokens[1::2]))
    return float(cpt_dict['weight'])

def output_to_dict(f):
    d = dict()
    for line in f:
        tokens = line.split()
        assert len(tokens) == 2
        d[tokens[0]] = float(tokens[1])
    return d

def merge_dicts(a, b):
    c = ddict(lambda: 0)
    for d in [a, b]:
        for key, value in d.items():
            c[key] += value
    return c

processes = []

# bench -> test -> [(weight, process)]
processes = ddict(lambda: ddict(list))

for bench, bench_dir in mylistdir(args.dir):
    for test, test_dir in mylistdir(bench_dir):
        for cpt, cpt_dir in mylistdir(test_dir):
            cpt_weight = get_cpt_weight(cpt)
            if not os.path.isfile(os.path.join(cpt_dir, 'success')):
                print(f'error: missing success marker for checkpoint: {cpt_dir}', file = sys.stderr)
                exit(1)
            dbgout_path = os.path.join(cpt_dir, 'dbgout.gz')
            cmd = ['gunzip', '<', dbgout_path, '|', *args.decltab_cmd]
            process = subprocess.Popen(' '.join(cmd),
                                       shell = True,
                                       stdin = subprocess.PIPE,
                                       stdout = subprocess.PIPE,
                                       text = True
                                       )
            # stdout = subprocess.PIPE)
            processes[bench][test].append((cpt_weight, process))

all_stats = ddict(lambda: 1)
            
for bench, tests in processes.items():
    bench_stats = ddict(lambda: 1)
    for test, cpts in tests.items():
        total_weight = 0
        test_stats = ddict(lambda: 0)
        for cpt_weight, process in cpts:
            for key, value in output_to_dict(process.stdout).items():
                test_stats[key] += cpt_weight * value
            process.wait()
            if process.returncode != 0:
                raise ChildProcessError
            total_weight += cpt_weight
        assert total_weight > 0
        for key in test_stats:
            test_stats[key] /= total_weight

        test_miss_rate = test_stats['misses'] / (test_stats['hits'] + test_stats['misses'])
        bench_stats['miss-rate'] *= test_miss_rate

    for key in bench_stats:
        bench_stats[key] = math.pow(bench_stats[key], 1 / len(tests))

    for key, value in bench_stats.items():
        print(bench, key, value)
        all_stats[key] *= value


for key in all_stats:
    all_stats[key] = math.pow(all_stats[key], 1 / len(processes))

for key, value in all_stats.items():
    print('mean', key, value)
