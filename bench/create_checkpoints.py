import argparse
import os
import sys
import subprocess
import json
import multiprocessing
import shutil
import signal
import copy
import glob
import types

import shared
from shared import *

if 'LLSCT' not in os.environ:
    os.environ['LLSCT'] = 'none'

signal.signal(signal.SIGINT, lambda x, y: sys.exit(1))

parser = argparse.ArgumentParser()
parser.add_argument('--benchspec', required = True)
parser.add_argument('--test-suite', required = True)
parser.add_argument('--outdir', required = True)
parser.add_argument('--llsct', required = True)
parser.add_argument('--jobs', '-j', type = int, default = multiprocessing.cpu_count() + 2)
parser.add_argument('--interval', type = int, default = 10000000)
args = parser.parse_args()

# Make paths absolute
args.llsct = os.path.abspath(args.llsct)
args.benchspec = os.path.abspath(args.benchspec)
args.test_suite = os.path.abspath(args.test_suite)
args.outdir = os.path.abspath(args.outdir)

# Tool directories
gem5_dir = f'{args.llsct}/gem5'
gem5_exe = f'{gem5_dir}/build/X86/gem5.fast'
gem5_se_py = f'{gem5_dir}/configs/deprecated/example/se.py'
simpoint_exe = f'{args.llsct}/simpoint/bin/simpoint'
valgrind_exe = f'{args.llsct}/valgrind/install/bin/valgrind'

benchspec = load_benchspec(args.benchspec)

shared.jobs_sema = multiprocessing.BoundedSemaphore(args.jobs)

# Create output directory if it doesn't exist
os.makedirs(args.outdir, exist_ok = True)

mem_size = 8589934592

def execute_gem5_test(cmd: list, se_args: list, test_spec: dict, output_dir: str, **kwargs) -> int:
    assert len(cmd) >= 1
    exe = cmd[0]
    argstr = ' '.join(cmd[1:])
    gem5_cmd = [
        gem5_exe,
        f'--outdir={output_dir}/m5out',
        gem5_se_py,
        f'--output={output_dir}/stdout',
        f'--errout={output_dir}/stderr',
        f'--cmd={exe}',
        f'--options={argstr}',
        *se_args
    ]
    return execute_test(gem5_cmd, test_spec, stdout = f'{output_dir}/simout', stderr = f'{output_dir}/simerr', cmdline = f'{output_dir}/cmdline', **kwargs)



def run_benchmark_test_host(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if check_success(output_dir):
        return 0
    
    os.makedirs(output_dir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir, test_suite = args.test_suite)
    cmd = [bench_exe, *test_spec['args']]
    returncode = execute_test(cmd, test_spec, stdout = f'{output_dir}/stdout', stderr = f'{output_dir}/stderr', cmdline = f'{output_dir}/cmdline')
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: host run failed', file = sys.stderr)
        return returncode

    returncode = execute_verification(test_spec, output_dir)
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: host verification failed', file = sys.stderr)
        return returncode

    mark_success(output_dir)
    print(f'DONE: {bench_name}->{test_name}: host', file = sys.stderr)
    
    return 0

def run_benchmark_test_bbv(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if check_success(output_dir):
        return 0

    os.makedirs(output_dir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir, test_suite = args.test_suite)

    cmd = [
        valgrind_exe,
        '--tool=exp-bbv',
        f'--bb-out-file={output_dir}/bbv.out',
        f'--pc-out-file={output_dir}/pc.out',
        f'--interval-size={args.interval}',
        f'--log-file={output_dir}/valout',
        '--', bench_exe, *test_spec['args']
    ]
    returncode = execute_test(cmd, test_spec, stdout = f'{output_dir}/stdout', stderr = f'{output_dir}/stderr', cmdline = f'{output_dir}/cmdline')
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: valgrind run failed', file = sys.stderr)
        return returncode

    returncode = execute_verification(test_spec, output_dir)
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: valgrind verification failed', file = sys.stderr)
        return returncode

    mark_success(output_dir)
    print(f'DONE: {bench_name}->{test_name}: bbv', file = sys.stderr)

    return 0


def run_benchmark_test_spt(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if check_success(output_dir):
        return 0
    
    os.makedirs(output_dir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir, test_suite = args.test_suite)

    cmd = [
        simpoint_exe,
        '-loadFVFile', f'{output_dir}/../bbv/bbv.out',
        '-maxK', '30',
        '-saveSimpoints', f'{output_dir}/simpoints.out',
        '-saveSimpointWeights', f'{output_dir}/weights.out'
    ]
    returncode = execute_test(cmd, test_spec, stdout = f'{output_dir}/stdout', stderr = f'{output_dir}/stderr', cmdline = f'{output_dir}/cmdline')
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: simpoint processing error', file = sys.stderr)
        return returncode

    assert os.path.exists(f'{output_dir}/simpoints.out')
    assert os.path.exists(f'{output_dir}/weights.out')

    mark_success(output_dir)
    print(f'DONE: {bench_name}->{test_name}: spt', file = sys.stderr)

    return 0


def run_benchmark_test_cpt(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if check_success(output_dir):
        return 0

    os.makedirs(output_dir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir, test_suite = args.test_suite)

    se_args = [
        '--cpu-type=X86NonCachingSimpleCPU',
        f'--mem-size={mem_size}',
        f'--take-simpoint-checkpoint={output_dir}/../spt/simpoints.out,{output_dir}/../spt/weights.out,{args.interval},10000',
    ]
    returncode = execute_gem5_test([bench_exe, *test_spec['args']], se_args, test_spec, output_dir)
    if returncode != 0:
        print(f'ERROR: {bench_name}->{test_name}: checkpoint step failed', file = sys.stderr)
        return returncode

    mark_success(output_dir)
    print(f'DONE: {bench_name}->{test_name}: cpt', file = sys.stderr)

    return 0


def run_benchmark_test(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if 'skip' in test_spec:
        return 0

    bench_name = os.path.basename(bench_exe)

    expand_test_spec(test_spec)

    # 1. Run+verify the test on the host.
    returncode = run_benchmark_test_host(bench_exe, test_name, test_spec, input_dir, f'{output_dir}/host')
    if returncode != 0:
        return returncode

    # 2. Run+verfiy bbv geneation using valgrind.
    returncode = run_benchmark_test_bbv(bench_exe, test_name, test_spec, input_dir, f'{output_dir}/bbv')
    if returncode != 0:
        return returncode

    # 3. SimPoint Analysis
    returncode = run_benchmark_test_spt(bench_exe, test_name, test_spec, input_dir, f'{output_dir}/spt')
    if returncode != 0:
        return returncode

    # 4. Taking SimPoint Checkpoints in gem5
    returncode = run_benchmark_test_cpt(bench_exe, test_name, test_spec, input_dir, f'{output_dir}/cpt')
    if returncode != 0:
        sys.exit(returncode)

    print(f'DONE: {bench_name}->{test_name}', file = sys.stderr)

    return 0


def run_benchmark(bench_exe: str, bench_tests: list, input_dir: str, output_dir: str) -> int:
    if 'skip' in bench_tests:
        return 0

    bench_name = os.path.basename(bench_exe)

    jobs = []
    for test_name, test_spec in bench_tests.items():
        test_input_dir = f'{input_dir}/run_test'
        test_output_dir = f'{output_dir}/{test_name}'
        job = multiprocessing.Process(target = run_benchmark_test, args = (bench_exe, test_name, test_spec, test_input_dir, test_output_dir))
        job.start()
        jobs.append(job)

    returncode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            returncode = 1

    if returncode == 0:
        print(f'DONE: {bench_name}', file = sys.stderr)
    
    sys.exit(returncode)
    

def run_benchmarks(input_dir: str, output_dir: str, bench_spec: dict):
    jobs = []
    for bench_name, bench_tests in bench_spec.items():
        bench_exe = find_executable(bench_name, args.test_suite)
        if bench_exe == None:
            print(f'ERROR: {bench_name}: missing executable', file = sys.stderr)
            continue
        bench_input_dir = os.path.dirname(bench_exe)
        bench_output_dir = f'{args.outdir}/{bench_name}'
        job = multiprocessing.Process(target = run_benchmark, args = (bench_exe, bench_tests, bench_input_dir, f'{args.outdir}/{bench_name}'))
        job.start()
        jobs.append(job)
    returncode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            returncode = 1
    return returncode
                                      

run_benchmarks(args.test_suite, args.outdir, benchspec)
