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

with open(args.benchspec) as f:
    benchspec = json.load(f)


# Create output directory if it doesn't exist
os.makedirs(args.outdir, exist_ok = True)

mem_size = 8589934592

jobs_sema = multiprocessing.BoundedSemaphore(args.jobs)

def resolve_inherits(d):
    if type(d) == list:
        return [resolve_inherits(x) for x in d]
    elif type(d) == str or type(d) == int:
        return d
    elif type(d) == dict:
        d = dict([(k, resolve_inherits(v)) for k, v in d.items()])
        done = False
        while not done:
            done = True
            for k, v in d.items():
                if type(v) == dict and 'inherit' in v:
                    new_v = copy.deepcopy(v)
                    inherits_from = new_v['inherit']
                    del new_v['inherit']
                    for a, b in d[inherits_from].items():
                        if a not in new_v:
                            new_v[a] = b
                    d[k] = new_v
                    done = False
                    break
        return d
    else:
        print('unexpected type: ', type(d), file = sys.stderr)
        assert False


benchspec = resolve_inherits(benchspec)
assert '"inherit"' not in json.dumps(benchspec)

# NOTE: Should not modify input d.
def perform_test_substitutions(d, bench_name, test_name, input_dir, output_dir): # returns d'
    if d is None:
        return None

    rec = lambda x: perform_test_substitutions(x, bench_name, test_name, input_dir, output_dir)
    
    if type(d) == str:
        # substitute directly on string
        d = d.replace('%S', '%R/..')
        d = d.replace('%R', input_dir)
        d = d.replace('%T', output_dir)
        d = d.replace('%n', test_name)
        d = d.replace('%b', f'{args.test_suite}/tools')
        d = d.replace('%i', bench_name.split('.')[0])
        assert '%' not in d
    elif type(d) == dict:
        d = dict([(key, rec(value)) for key, value in d.items()])
    elif type(d) == list:
        d = [rec(x) for x in d]
    return d


def find_executable(name, root):
    for root, dirs, files in os.walk(args.test_suite):
        if name in files:
            return os.path.join(root, name)
    return None

def get_success_file(dir: str) -> str:
    return os.path.join(dir, 'success')

def check_success(dir: str) -> bool:
    return os.path.exists(get_success_file(dir))

def mark_success(dir: str):
    with open(get_success_file(dir), 'w'):
        pass


def execute_test(cmd: list, test_spec: dict, stdout: str = None, stderr: str = None, cmdline: str = None, **kwargs) -> int:
    copy_assets(test_spec)
    if cmdline:
        with open(cmdline, 'w') as f:
            print(' '.join(cmd), file = f)
    with jobs_sema, \
         open(stdout, 'w') as stdout, \
         open(stderr, 'w') as stderr:
        result = subprocess.run(cmd,
                                cwd = test_spec['cd'],
                                stdin = subprocess.DEVNULL,
                                stdout = stdout,
                                stderr = stderr,
                                **kwargs)
        return result.returncode

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

def execute_verification(test_spec: dict, output_dir: str, **kwargs) -> int:
    for i, verify_command in enumerate(test_spec['verify_commands']):
        returncode = execute_test(verify_command, test_spec, f'{output_dir}/verout-{i}', f'{output_dir}/vererr-{i}',
                                  cmdline = f'{output_dir}/vercmds',
                                  shell = True,
                                  **kwargs)
        if returncode != 0:
            return returncode
    return 0

def copy_assets(test_spec):
    for copy in test_spec['cp']:
        if os.path.exists(copy['to']):
            continue
        if os.path.isdir(copy['from']):
            shutil.copytree(copy['from'], copy['to'])
        else:
            shutil.copyfile(copy['from'], copy['to'])

def parse_checkpoint_args(checkpoint_dir):
    checkpoint_name = os.path.basename(checkpoint_dir)
    assert checkpoint_name.startswith('cpt.')
    checkpoint_name = checkpoint_name.removeprefix('cpt.')
    tokens = checkpoint_name.split('_')
    args = dict()
    for k, v in zip(tokens[0::2], tokens[1::2]):
        args[k] = v
    return types.SimpleNamespace(**args)

def expand_test_spec(test_spec: dict):
    # Expand shorthands.
    if 'compare' in test_spec:
        assert 'verify_commands' not in test_spec
        verify_commands = []
        compare = test_spec['compare']
        if type(compare) == str:
            assert 'stdout' in test_spec
            compare = [{"ref": compare, "out": "%T/stdout"}]
        assert type(compare) == list
        for d in compare:
            ref = d['ref']
            out = d['out']
            verify_args = []
            if 'verify_args' in test_spec:
                verify_args = test_spec['verify_args']
            assert type(verify_args) == list
            verify_argstr = ' '.join(verify_args)
            verify_command = f'%b/fpcmp-target {verify_argstr} -i {ref} {out}'
            verify_commands.append(verify_command)
        test_spec['verify_commands'] = verify_commands
    if 'verify_commands' not in test_spec:
        test_spec['verify_commands'] = []

    if 'cd' not in test_spec:
        test_spec['cd'] = '%T'

    if 'cp' not in test_spec:
        test_spec['cp'] = []
    if 'cp' in test_spec and type(test_spec['cp']) == dict:
        test_spec['cp'] = [test_spec['cp']]
    for i, cp in enumerate(test_spec['cp']):
        if type(cp) == str:
            test_spec['cp'][i] = {
                "from": f"%R/{cp}",
                "to": f"%T/{cp}"
            }



def run_benchmark_test_host(bench_exe: str, test_name: str, test_spec: dict, input_dir: str, output_dir: str) -> int:
    if check_success(output_dir):
        return 0
    
    os.makedirs(output_dir, exist_ok = True)
    bench_name = os.path.basename(bench_exe)
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir)
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
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir)

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
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir)

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
    test_spec = perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir)

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
        return returncode

    print(f'DONE: {bench_name}->{test_name}', file = sys.stderr)

    return 0


def run_benchmark(bench_exe: str, bench_tests: list, input_dir: str, output_dir: str) -> int:
    if 'skip' in bench_tests:
        return 0

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
    
    return returncode
    

def run_benchmarks(input_dir: str, output_dir: str, bench_spec: dict):
    jobs = []
    for bench_name, bench_tests in benchspec.items():
        bench_exe = find_executable(bench_name, args.test_suite)
        assert bench_exe
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
