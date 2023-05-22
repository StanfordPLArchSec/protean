import sys
import os
import argparse
import glob
import types
import math

parser = argparse.ArgumentParser()
parser.add_argument('results_dir')
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
    return float(tokens[0])

def get_results_for_benchmark_test(test_dir):
    test_ipc = 0
    weight = 0
    n = 0
    for checkpoint_dir in glob.glob(f'{test_dir}/cpt.*'):
        checkpoint_ipc = get_results_for_checkpoint(checkpoint_dir)
        if checkpoint_ipc is None:
            continue
        checkpoint_args = parse_checkpoint_args(checkpoint_dir)
        checkpoint_weight = float(checkpoint_args.weight)
        test_ipc += checkpoint_ipc * checkpoint_weight
        weight += checkpoint_weight
        n += 1
    if n == 0:
        return None
    return test_ipc / weight

def get_results_for_benchmark(bench_dir):
    bench_name = os.path.basename(bench_dir)
    test_ipcs = []
    for test_name in os.listdir(bench_dir):
        test_dir = os.path.join(bench_dir, test_name)
        if os.path.isdir(test_dir):
            test_ipc = get_results_for_benchmark_test(test_dir)
            if test_ipc is not None:
                test_ipcs.append(test_ipc)
    if len(test_ipcs) == 0:
        return None
    bench_ipc = pow(math.prod(test_ipcs), 1 / len(test_ipcs))
    print(f'{bench_name} {bench_ipc:.4}')
    return bench_ipc

def get_results_for_all(results_dir):
    bench_ipcs = []
    for bench_name in os.listdir(results_dir):
        bench_dir = os.path.join(results_dir, bench_name)
        if os.path.isdir(bench_dir):
            bench_ipc = get_results_for_benchmark(bench_dir)
            if bench_ipc is not None:
                bench_ipcs.append(bench_ipc)
    if len(bench_ipcs) == 0:
        print(f'NO RESULTS FOUND!')
        exit(1)
    geomean_ipc = pow(math.prod(bench_ipcs), 1 / len(bench_ipcs))
    print(f'geomean {geomean_ipc:.4}')

get_results_for_all(args.results_dir)
            
            
