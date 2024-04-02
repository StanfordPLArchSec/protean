import argparse
import os
import sys
import signal
import multiprocessing
import gzip
import glob

import shared
from shared import *

parser = argparse.ArgumentParser()
parser.add_argument('--outdir', required = True)
parser.add_argument('--cptdir', required = True)
parser.add_argument('--bindir', required = True)
parser.add_argument('--benchspec', required = True)
parser.add_argument('--jobs', '-j', type = int, default = multiprocessing.cpu_count() + 2)
parser.add_argument('--gem5', type = str, required = True)
parser.add_argument('--se', type = str, required = True)
parser.add_argument('--gem5-opt', action = "append", type = str, default = [])
parser.add_argument('extra_se_options', nargs = '*')

args = parser.parse_args()

args.outdir = os.path.abspath(args.outdir)
args.cptdir = os.path.abspath(args.cptdir)
args.bindir = os.path.abspath(args.bindir)
args.benchspec = os.path.abspath(args.benchspec)
args.gem5 = os.path.abspath(args.gem5)
args.se = os.path.abspath(args.se)

signal.signal(signal.SIGINT, lambda x, y: sys.exit(1))


gem5_exe = args.gem5
gem5_se_py = args.se

benchspec = load_benchspec(args.benchspec)
shared.jobs_sema = multiprocessing.BoundedSemaphore(args.jobs)

def execute_gem5_test(cmd: list, se_args: list, test_spec: dict, output_dir: str, **kwargs) -> int:
    assert len(cmd) >= 1
    exe = cmd[0]
    argstr = ' '.join(cmd[1:])
    gem5_cmd = [
        gem5_exe,
        f'--outdir={output_dir}/m5out',
        f'--debug-file={output_dir}/dbgout.gz',
        *args.gem5_opt,
        gem5_se_py,
        f'--output={output_dir}/stdout',
        f'--errout={output_dir}/stderr',
        f'--cmd={exe}',
        f'--options={argstr}',
        *se_args,
    ]
    return execute_test(gem5_cmd, test_spec, stdout = f'{output_dir}/simout',
                        stderr = f'{output_dir}/simerr', cmdline = f'{output_dir}/cmdline',
                        **kwargs)

def run_benchmark_test_checkpoint(bench_exe: str, test_name: str, test_spec: dict, bindir: str, cptdir: str, outdir: str) -> int:
    if check_success(outdir):
        return 0
    
    os.makedirs(outdir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, bindir, outdir, test_suite = args.bindir)

    checkpoint_args = parse_checkpoint_args(outdir)
    checkpoint_id = int(checkpoint_args.simpoint)
    mem_size = test_spec['memsize']

    se_args = [
        '--cpu-type=X86O3CPU',
        f'--mem-size={mem_size}',
        '--l1d_size=64kB',
        '--l1i_size=16kB',
        '--caches',
        f'--checkpoint-dir={cptdir}/m5out',
        '--restore-simpoint-checkpoint',
        f'--checkpoint-restore={checkpoint_id+1}',
        *args.extra_se_options,
    ]
    returncode = execute_gem5_test(test_spec['cmd'], se_args, test_spec, outdir)
    if returncode != 0:
        print(f'{ERROR}: {bench_name}->{test_name}->{checkpoint_id}: failed')
        return returncode

    with open(f'{outdir}/ipc.txt', 'w') as ipc_file, \
         open(f'{outdir}/m5out/stats.txt') as stats_file:
        stats_keys = {'simInsts': [], 'simTicks': [], 'system.clk_domain.clock': []}        
        for line in stats_file.read().splitlines():
            for key in stats_keys:
                if line.startswith(key):
                    stats_keys[key].append(float(line.split()[1]))
        for l in stats_keys.values():
            if len(l) != 2:
                print(stats_keys, file = sys.stderr)
            assert len(l) == 2

        interval = int(checkpoint_args.interval)
        simTicks = stats_keys['simTicks'][-1]
        period = stats_keys['system.clk_domain.clock'][-1]
        ipc = interval / (simTicks / period)
        print(f'{ipc}', file = ipc_file)

    mark_success(outdir)
    return 0

def run_benchmark_test(bench_exe: str, test_name: str, test_spec: dict, bindir: str, cptdir: str, outdir: str) -> int:
    if 'skip' in test_spec:
        return 0

    if not check_success(cptdir):
        return 0

    bench_name = os.path.basename(bench_exe)
    expand_test_spec(bench_exe, test_spec)

    jobs = []
    for checkpoint_dir in glob.glob(f'{cptdir}/m5out/cpt.*'):
        assert os.path.isdir(checkpoint_dir)
        job = multiprocessing.Process(target = run_benchmark_test_checkpoint,
                                      args = (bench_exe, test_name, test_spec, bindir, cptdir,
                                              f'{outdir}/{os.path.basename(checkpoint_dir)}'))
        job.start()
        jobs.append(job)

    returncode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            returncode = 1

    if returncode == 0:
        print(f'{DONE}: {bench_name}->{test_name}')
    
    sys.exit(returncode)

def run_benchmark(bench_exe: str, bench_tests: dict, bindir: str, cptdir: str, outdir: str):
    if 'skip' in bench_tests:
        return 0

    bench_name = os.path.basename(bench_exe)

    jobs = []
    for test_name, test_spec in bench_tests.items():
        test_bindirs = glob.glob(f'{bindir}/run_*')
        assert len(test_bindirs) == 1
        test_bindir = test_bindirs[0]
        test_cptdir = f'{cptdir}/{test_name}/cpt'
        test_outdir = f'{outdir}/{test_name}'
        job = multiprocessing.Process(target = run_benchmark_test,
                                      args = (bench_exe, test_name, test_spec,
                                              test_bindir, test_cptdir, test_outdir))
        job.start()
        jobs.append(job)

    returncode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            returncode = 1

    if returncode == 0:
        print(f'{DONE}: {bench_name}', file = sys.stderr)
    sys.exit(returncode)

def run_benchmarks(benchspec: dict, bindir: str, cptdir: str, outdir: str):
    jobs = []
    for bench_name, bench_tests in benchspec.items():
        bench_exe = find_executable(bench_name, args.bindir)
        assert bench_exe
        bench_bindir = os.path.dirname(bench_exe)
        bench_cptdir = os.path.join(args.cptdir, bench_name)
        bench_outdir = os.path.join(args.outdir, bench_name)
        job = multiprocessing.Process(target = run_benchmark,
                                      args = (bench_exe, bench_tests,
                                              bench_bindir, bench_cptdir, bench_outdir))
        job.start()
        jobs.append(job)


    returncode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            returncode = 1
    return returncode
        
run_benchmarks(benchspec, args.bindir, args.cptdir, args.outdir)
