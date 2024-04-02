import sys
import os
import argparse
import glob
import types
import math
from shared import parse_checkpoint_args
import functools

parser = argparse.ArgumentParser()
parser.add_argument('results_dirs', nargs = '+')
parser.add_argument('--stat', default = 'ipc')
args = parser.parse_args()

def parse_checkpoint_args(checkpoint_dir):
    name = os.path.basename(checkpoint_dir)
    assert name.startswith('cpt.')
    name = name.removeprefix('cpt.')
    tokens = name.split('_')
    args = dict()
    for k, v in zip(tokens[0::2], tokens[1::2]):
        args[k] = v
    return types.SimpleNamespace(**args)

def get_results_for_checkpoint(checkpoint_dir):
    ipc_path = f'{checkpoint_dir}/ipc.txt'
    if not os.path.exists(ipc_path):
        return None
    with open(f'{checkpoint_dir}/ipc.txt') as f:
        lines = f.read().splitlines()
    assert len(lines) == 1
    tokens = lines[0].split()
    assert len(tokens) == 1
    if args.stat == 'ipc':
        return float(tokens[0])
    else:
        stats_path = f'{checkpoint_dir}/m5out/stats.txt'
        matching_lines = []
        with open(stats_path) as f:
            for line in f:
                if args.stat in line:
                    matching_lines.append(line)
        if len(matching_lines) == 0:
            print('error: stat not found in stats.txt', file = sys.stderr)
            exit(1)
        line = matching_lines[-1]
        return float(line.split()[1])

def prune(x):
    if type(x) != dict:
        return x

    todel = list()
    for k, v in x.items():
        prune(v)
        if len(v) == 0:
            todel.append(k)
    for k in todel:
        del x[k]
        
        
        

def get_results_for_dir(dir: str):
    results = dict()
    for bench_name in os.listdir(dir):
        bench_dir = f'{dir}/{bench_name}'
        results[bench_name] = dict()
        for test_name in os.listdir(bench_dir):
            test_dir = f'{bench_dir}/{test_name}'
            results[bench_name][test_name] = dict()
            for cpt_name in os.listdir(test_dir):
                cpt_dir = f'{test_dir}/{cpt_name}'
                ipc_path = f'{cpt_dir}/ipc.txt'
                if os.path.exists(ipc_path):
                    if args.stat == 'ipc':
                        with open(ipc_path) as f:
                            lines = f.read().splitlines()
                            ipc = float(lines[0])
                            results[bench_name][test_name][cpt_name] = [ipc]
                    else:
                        stats_path = f'{cpt_dir}/m5out/stats.txt'
                        matching_lines = []
                        with open(stats_path) as f:
                            for line in f:
                                if args.stat in line:
                                    matching_lines.append(line)
                        if len(matching_lines) == 0:
                            print(f'error: stat not found in {stats_path}', file = sys.stderr)
                            # exit(1)
                            matching_lines.append('0 0 0')
                        line = matching_lines[-1]
                        results[bench_name][test_name][cpt_name] = [float(line.split()[1])]
                        

    # Prune if necessary
    prune(results)
    return results

                        
results_list = [get_results_for_dir(dir) for dir in args.results_dirs]

# Intersect lists

def merge_results(a, b) -> dict:
    assert type(a) == type(b)
    t = type(a)
    if t == dict:
        keys = set(a.keys()) & set(b.keys())
        d = dict()
        for key in keys:
            d[key] = merge_results(a[key], b[key])
    elif t == list:
        d = a + b
    else:
        assert False
    return d


merged_results = functools.reduce(merge_results, results_list[1:], results_list[0])

# Erase any merged results that are empty
to_del = list()
for key, value in merged_results.items():
    if len(value) == 0:
        to_del.append(key)
for key in to_del:
    del merged_results[key]

# Compute overheads for each benchmark
bench_ipcs = dict()

def compute_test_ipc(i, test_results):
    test_ipc = 0
    test_weight = 0
    for cpt_name, cpt_ipcs in test_results.items():
        cpt_args = parse_checkpoint_args(cpt_name)
        cpt_weight = float(cpt_args.weight)
        test_weight += cpt_weight
        test_ipc += cpt_ipcs[i] * cpt_weight
    test_ipc /= test_weight
    return test_ipc

def compute_bench_ipc(i, bench_results):
    bench_ipcs = list()
    for test_results in bench_results.values():
        bench_ipcs.append(compute_test_ipc(i, test_results))
    return pow(math.prod(bench_ipcs), 1 / len(bench_ipcs))


for bench_name, bench_results in merged_results.items():
    ipcs = list()
    for i in range(len(args.results_dirs)):
        ipcs.append(compute_bench_ipc(i, bench_results))
    print(bench_name, *ipcs);
