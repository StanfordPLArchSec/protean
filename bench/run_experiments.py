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
parser.add_argument('--gem5', required = True)
parser.add_argument('--errlog', required = True)
parser.add_argument('--force', '-f', action = 'store_true')
parser.add_argument('--jobs', '-j', type = int, default = multiprocessing.cpu_count() + 2)
parser.add_argument('--simpoint', required = True)
parser.add_argument('--color', action = 'store_true')
args = parser.parse_args()

# Make paths absolute
args.benchspec = os.path.abspath(args.benchspec)
args.test_suite = os.path.abspath(args.test_suite)
args.outdir = os.path.abspath(args.outdir)
args.gem5 = os.path.abspath(args.gem5)
args.errlog = os.path.abspath(args.errlog)
args.simpoint = os.path.abspath(args.simpoint)

with open(args.benchspec) as f:
    benchspec = json.load(f)

errlog = open(args.errlog, 'w')

def log(*args, **kwargs):
    print(*args, **kwargs, file = sys.stderr)
    print(*args, **kwargs, file = errlog)

def report_error(msg, file = sys.stderr, prefix = 'ERROR'):
    if os.isatty():
        file.write('\033[1;31m')
    file.write(prefix)
    file.write(': ')
    if os.isatty():
        file.write('\033[0;0m')
    print(msg, file = file);
    
def report_fatal_error(msg, file = sys.stderr):
    report_error(msg, file = file, prefix = 'FATAL ERROR')
    exit(1)
        
# Sanity checks
if not os.path.isdir(args.test_suite):
    print(f'{sys.argv[0]}: error: {args.test_suite} is not a directory', file = sys.stderr)
    exit(1)
if args.force:
    shutil.rmtree(args.outdir)
else:
    if os.path.exists(args.outdir):
        print(f'{sys.argv[0]}: error: {args.outdir} already exists', file = sys.stderr)
        exit(1)


os.makedirs(args.outdir, exist_ok = True)

# Organization of output directory
# - outdir/
#   - bench/
#     - test/
#       - bbv/
#         - m5out/
#           - simpoints.bb.gz
#       - simpoint_file
#       - weight_file
#       - cpt/
#         - m5out/
#           - cpt.*
#       - res/
#         - m5out-cpt.*
#           - ipc.txt
#       - ipc.txt
#     - ...
#     - ipc.txt
#   - ipcs.txt


# Design approach: Each benchmark gets its own workflow thread.

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

with open('tmp.json', 'w') as f:
    json.dump(benchspec, f)

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
    

def copy_assets(test_spec):
    for copy in test_spec['cp']:
        if os.path.isdir(copy['from']):
            shutil.copytree(copy['from'], copy['to'])
        else:
            shutil.copyfile(copy['from'], copy['to'])


def handle_benchmark_checkpoint_result(exe, test_spec, bench_name, test_name,
                                       checkpoint_dir, input_dir, parent_output_dir):
    checkpoint_tokens = os.path.basename(checkpoint_dir).removeprefix('cpt.').split('_')
    checkpoint_args = dict()
    for key, value in zip(checkpoint_tokens[::2], checkpoint_tokens[1::2]):
        checkpoint_args[key] = value
    checkpoint_args = types.SimpleNamespace(**checkpoint_args)
    checkpoint_id = int(checkpoint_args.simpoint)
    output_dir_res = f'{parent_output_dir}/{os.path.basename(checkpoint_dir)}'
    os.mkdir(output_dir_res)
    sub_res = lambda x: perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir_res)
    test_spec_res = sub_res(test_spec)
    with jobs_sema, \
         open(f'{output_dir_res}/simout', 'w') as simout, \
         open(f'{output_dir_res}/simerr', 'w') as simerr:
        options = ' '.join(test_spec_res['args'])
        copy_assets(test_spec_res)
        result = subprocess.run(
            [
                f'{args.gem5}/build/X86/gem5.fast',
                f'--outdir={output_dir_res}/m5out',
                f'{args.gem5}/configs/deprecated/example/se.py',
                '--cpu-type=X86O3CPU',
                f'--mem-size={mem_size}',
                '--l1d_size=64kB',
                '--l1i_size=16kB',
                '--caches',
                f'--checkpoint-dir={os.path.dirname(checkpoint_dir)}',
                '--restore-simpoint-checkpoint',
                f'--checkpoint-restore={checkpoint_id+1}',
                f'--cmd={exe}',
                f'--options={options}',
                f'--output={output_dir_res}/simout',
                f'--errout={output_dir_res}/simerr',
            ],
            stdin = subprocess.DEVNULL,
            stdout = simout,
            stderr = simerr,
            cwd = test_spec_res['cd'],
        )
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] results execution failed for checkpoint {checkpoint_id}')

    # Produce ipc.txt
    with open(f'{output_dir_res}/ipc.txt', 'w') as ipc_file, \
         open(f'{output_dir_res}/m5out/stats.txt') as stats_file:
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
            
    return 0
    

def handle_benchmark_test(exe, test_name, test_spec, parent_input_dir, parent_output_dir):
    if 'skip' in test_spec:
        return 0
    
    assert type(test_spec) == dict
    jobs = []

    input_dir = f'{parent_input_dir}/run_test'
    output_dir = f'{parent_output_dir}/{test_name}'
    os.mkdir(output_dir)

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

    # 1. Run test on host.
    output_dir_host = f'{output_dir}/host'
    os.mkdir(output_dir_host)
    sub_host = lambda x: perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir_host)
    test_spec_host = sub_host(test_spec)
    if 'args' not in test_spec_host:
        print(bench_name, test_name, file = sys.stderr)
    cmdline_host = [exe, *test_spec_host['args']]
    with open(f'{output_dir_host}/cmdline', 'w') as f:
        print(' '.join(cmdline_host), file = f)
    with jobs_sema, open(f'{output_dir_host}/stdout', 'w') as stdout, open(f'{output_dir_host}/stderr', 'w') as stderr:
        copy_assets(test_spec_host)
        result = subprocess.run(cmdline_host,
                                stdin = subprocess.DEVNULL, stdout = stdout, stderr = stderr,
                                cwd = test_spec_host['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: host execution failed')
            return 1
    
    # 2. Verify results of host test.
    for i, verify_command in enumerate(test_spec_host['verify_commands']):
        with jobs_sema, open(f'{output_dir_host}/verout-{i}', 'w') as stdout, open(f'{output_dir_host}/vererr-{i}', 'w') as stderr:
            result = subprocess.run(verify_command, shell = True,
                                    stdin = subprocess.DEVNULL, stdout = stdout, stderr = stderr,
                                    cwd = test_spec_host['cd'])
            if result.returncode != 0:
                log(f'[{bench_name}->{test_name}] ERROR: host verification {i} failed')
                return 1

    # 3. Profiling and Generating BBV.
    output_dir_bbv = f'{output_dir}/bbv'
    os.mkdir(output_dir_bbv)
    sub_bbv = lambda x: perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir_bbv)
    test_spec_bbv = sub_bbv(test_spec)
    with jobs_sema, open(f'{output_dir_bbv}/simout', 'w') as simout, open(f'{output_dir_bbv}/simerr', 'w') as simerr:
        options = ' '.join(test_spec_bbv['args'])
        copy_assets(test_spec_bbv)
        result = subprocess.run([f'{args.gem5}/build/X86/gem5.fast', f'--outdir={output_dir_bbv}/m5out', f'{args.gem5}/configs/deprecated/example/se.py',
                                 '--cpu-type=X86NonCachingSimpleCPU', f'--mem-size={mem_size}', '--simpoint-profile', f'--output={output_dir_bbv}/stdout',
                                 f'--errout={output_dir_bbv}/stderr',
                                 f'--cmd={exe}',
                                 f'--options={options}'],
                                stdin = subprocess.DEVNULL,
                                stdout = simout,
                                stderr = simerr,
                                cwd = test_spec_bbv['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: bbv execution failed')
            return 1
    assert os.path.exists(f'{output_dir_bbv}/m5out/simpoint.bb.gz')

    # 3.1. Verify results of simulation.
    for i, verify_command in enumerate(test_spec_host['verify_commands']):
        with jobs_sema, open(f'{output_dir_bbv}/verout-{i}', 'w') as stdout, open(f'{output_dir_bbv}/vererr-{i}', 'w') as stderr:
            result = subprocess.run(verify_command, shell = True,
                                    stdin = subprocess.DEVNULL, stdout = stdout, stderr = stderr,
                                    cwd = test_spec_bbv['cd'])
            if result.returncode != 0:
                log(f'[{bench_name}->{test_name}] ERROR: bbv verification {i} failed')
                return 1
        

    # 4. SimPoint Analysis
    output_dir_spt = f'{output_dir}/spt'
    os.mkdir(output_dir_spt)
    with jobs_sema, open(f'{output_dir_spt}/stdout', 'w') as stdout, open(f'{output_dir_spt}/stderr', 'w') as stderr:
        result = subprocess.run([args.simpoint, '-loadFVFile', f'{output_dir_bbv}/m5out/simpoint.bb.gz',
                                 '-maxK', '30', '-saveSimpoints', f'{output_dir_spt}/simpoints.out',
                                 '-saveSimpointWeights', f'{output_dir_spt}/weights.out', '-inputVectorsGzipped'],
                                stdin = subprocess.DEVNULL, stdout = stdout, stderr = stderr)
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: simpoint analysis failed')
            return 1

    assert os.path.exists(f'{output_dir_spt}/simpoints.out')
    assert os.path.exists(f'{output_dir_spt}/weights.out')

    # 5. Taking SimPoint Checkpoints in gem5
    output_dir_cpt = f'{output_dir}/cpt'
    os.mkdir(output_dir_cpt)
    sub_cpt = lambda x: perform_test_substitutions(test_spec, bench_name, test_name, input_dir, output_dir_cpt)
    test_spec_cpt = sub_cpt(test_spec)
    with jobs_sema, open(f'{output_dir_cpt}/simout', 'w') as simout, open(f'{output_dir_cpt}/simerr', 'w') as simerr:
        options = ' '.join(test_spec_cpt['args'])
        copy_assets(test_spec_cpt)
        result = subprocess.run([f'{args.gem5}/build/X86/gem5.fast', f'--outdir={output_dir_cpt}/m5out', f'{args.gem5}/configs/deprecated/example/se.py',
                                 '--cpu-type=X86NonCachingSimpleCPU', f'--mem-size={mem_size}',
                                 f'--take-simpoint-checkpoint={output_dir_spt}/simpoints.out,{output_dir_spt}/weights.out,10000000,10000',
                                 f'--cmd={exe}',
                                 f'--options={options}',
                                 f'--output={output_dir_cpt}/stdout', f'--errout={output_dir_cpt}/stderr'],
                                stdin = subprocess.DEVNULL,
                                stdout = simout,
                                stderr = simerr,
                                cwd = test_spec_cpt['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: cpt execution failed')
            return 1

    # 6. Resuming from gem5 Checkpoints
    output_dir_res = f'{output_dir}/res'
    os.mkdir(output_dir_res)
    jobs_res = []
    for checkpoint_dir in glob.glob(f'{output_dir_cpt}/m5out/cpt.*'):
        job = multiprocessing.Process(target = handle_benchmark_checkpoint_result,
                                      args = (exe, test_spec, bench_name, test_name,
                                              checkpoint_dir, input_dir, output_dir_res))
        job.start()
        jobs_res.append(job)
    res_failed = 0
    for job in jobs_res:
        job.join()
        if job.exitcode != 0:
            res_failed = 1
    if res_failed != 0:
        return 1

    # TODO
    return 0
        
def handle_benchmark(bench_name, bench_tests, parent_input_dir, parent_output_dir):
    if 'skip' in bench_tests:
        return 0
    
    # Locate binary.
    exe = find_executable(bench_name, parent_input_dir)
    if exe is None:
        exit_codes[exit_code_idx] = 1
        log(f'{sys.argv[0]}: could not find test binary for {bench_name} in test suite directory {args.test_suite}')
        return
                       
    input_dir = os.path.dirname(exe)
    output_dir = f'{parent_output_dir}/{bench_name}'
    os.mkdir(output_dir)
                       
    jobs = []
    for test_name, test_spec in bench_tests.items():
        if type(test_spec) != dict:
            print(test_spec, type(test_spec))
        assert type(test_spec) == dict
        job = multiprocessing.Process(target = handle_benchmark_test, args = (exe, test_name, test_spec, input_dir, output_dir))
        job.start()
        jobs.append(job)
    # Wait on all jobs
    exitcode = 0
    for job in jobs:
        job.join()
        if job.exitcode != 0:
            exitcode = 1
    # TODO: handle results.
    return exitcode
    

jobs = []
for bench_name, bench_tests in benchspec.items():
    job = multiprocessing.Process(target = handle_benchmark, args = (bench_name, bench_tests, args.test_suite, args.outdir))
    job.start()
    jobs.append(job)

exitcode = 0
for job in jobs:
    job.join()
    if job.exitcode != 0:
        exitcode = 1

exit(exitcode)
