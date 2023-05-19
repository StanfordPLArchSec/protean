import argparse
import os
import sys
import subprocess
import json
import multiprocessing
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--benchspec', required = True)
parser.add_argument('--test-suite', required = True)
parser.add_argument('--outdir', required = True)
parser.add_argument('--gem5', required = True)
parser.add_argument('--errlog', required = True)
parser.add_argument('--force', '-f', action = 'store_true')
parser.add_argument('--jobs', '-j', type = int, default = multiprocessing.cpu_count() + 2)
args = parser.parse_args()

# Make paths absolute
args.benchspec = os.path.abspath(args.benchspec)
args.test_suite = os.path.abspath(args.test_suite)
args.outdir = os.path.abspath(args.outdir)
args.gem5 = os.path.abspath(args.gem5)
args.errlog = os.path.abspath(args.errlog)

with open(args.benchspec) as f:
    benchspec = json.load(f)

errlog = open(args.errlog, 'w')

def log(*args, **kwargs):
    print(*args, **kwargs, file = sys.stderr)
    print(*args, **kwargs, file = errlog)

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

jobs_sema = multiprocessing.BoundedSemaphore(args.jobs)

def resolve_inherits_direct(d):
    done = False
    while not done:
        done = True
        for key, value in d.items():
            if type(value) == dict and 'inherit' in value:
                inherits_from = value['inherit']
                del value['inherit']
                d[key] = {**d[inherits_from], **value}
                done = False
                break
        

def resolve_inherits_all(d):
    if type(d) != dict:
        return
    resolve_inherits_direct(d)
    for value in d.values():
        resolve_inherits_all(value)
resolve_inherits_all(benchspec)
assert '"inherit"' not in json.dumps(benchspec)

with open('tmp.json', 'w') as f:
    json.dump(benchspec, f)

# NOTE: Should not modify input d.
def perform_test_substitutions(d, test_name, input_dir, output_dir): # returns d'
    if d is None:
        return None

    rec = lambda x: perform_test_substitutions(x, test_name, input_dir, output_dir)
    
    if type(d) == str:
        # substitute directly on string
        d = d.replace('%S', '%R/..')
        d = d.replace('%R', input_dir)
        d = d.replace('%T', output_dir)
        d = d.replace('%n', test_name)
        d = d.replace('%b', f'{args.test_suite}/tools')
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
    

def handle_benchmark_test(exe, test_name, test_spec, parent_input_dir, parent_output_dir):
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


    # 1. Run test on host.
    output_dir_host = f'{output_dir}/host'
    os.mkdir(output_dir_host)
    sub_host = lambda x: perform_test_substitutions(test_spec, test_name, input_dir, output_dir_host)
    test_spec_host = sub_host(test_spec)
    with jobs_sema, open(f'{output_dir_host}/stdout', 'w') as stdout, open(f'{output_dir_host}/stderr', 'w') as stderr:
        result = subprocess.run([exe, *test_spec_host['args']], stdout = stdout, stderr = stderr, cwd = test_spec_host['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: host execution failed')
            return 1
    
    # 2. Verify results of host test.
    for i, verify_command in enumerate(test_spec_host['verify_commands']):
        with jobs_sema, open(f'{output_dir_host}/verout-{i}', 'w') as stdout, open(f'{output_dir_host}/vererr-{i}', 'w') as stderr:
            result = subprocess.run(verify_command, shell = True, stdout = stdout, stderr = stderr, cwd = test_spec_host['cd'])
            if result.returncode != 0:
                log(f'[{bench_name}->{test_name}] ERROR: host verification {i} failed')
                return 1

    # 3. Profiling and Generating BBV.
    output_dir_bbv = f'{output_dir}/bbv'
    os.mkdir(output_dir_bbv)
    sub_bbv = lambda x: perform_test_substitutions(test_spec, test_name, input_dir, output_dir_bbv)
    test_spec_bbv = sub_bbv(test_spec)
    with jobs_sema, open(f'{output_dir_bbv}/simout', 'w') as simout, open(f'{output_dir_bbv}/simerr', 'w') as simerr:
        options = ' '.join(test_spec_bbv['args'])
        result = subprocess.run([f'{args.gem5}/build/X86/gem5.fast', f'--outdir={output_dir_bbv}/m5out', f'{args.gem5}/configs/deprecated/example/se.py',
                                 '--cpu-type=X86NonCachingSimpleCPU', f'--mem-size={mem_size}', '--simpoint-profile', f'--output={output_dir_bbv}/stdout',
                                 f'--errout={output_dir_bbv}/stderr',
                                 f'--cmd={exe}',
                                 f'--options={options}'],
                                stdout = simout,
                                stderr = simerr,
                                cwd = test_spec_bbv['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: bbv execution failed')
            return 1
    assert os.path.exists(f'{output_dir_bbv}/m5out/simpoint.bb.gz')

    # 4. SimPoint Analysis
    output_dir_spt = f'{output_dir}/spt'
    os.mkdir(output_dir_spt)
    with jobs_sema, open(f'{output_dir_spt}/stdout', 'w') as stdout, open(f'{output_dir_spt}/stderr', 'w') as stderr:
        result = subprocess.run([f'{args.simpoint}/bin/simpoint', '-loadFVFile', f'{output_dir_bbv}/m5out/simpoint.bb.gz',
                                 '-maxK', '30', '-saveSimpoints', f'{output_dir_spt}/simpoints.out',
                                 '-saveSimpointWeights', f'{output_dir_spt}/weights.out', '-inputVectorsGzipped'],
                                stdout = stdout,
                                stderr = stderr)
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: simpoint analysis failed')
            return 1

    assert os.path.exists(f'{output_dir_spt}/simpoints.out')
    assert os.path.exists(f'{output_dir_spt}/weights.out')

    # 5. Taking SimPoint Checkpoints in gem5
    output_dir_cpt = f'{output_dir}/cpt'
    os.mkdir(output_dir_cpt)
    sub_cpt = lambda x: perform_test_substitutions(test_spec, test_name, input_dir, output_dir_cpt)
    test_spec_cpt = sub_cpt(test_spec)
    with jobs_sema, open(f'{output_dir_cpt}/simout', 'w') as simout, open(f'{output_dir_cpt}/simerr', 'w') as simerr:
        options = ' '.join(test_spec_cpt['args'])
        result = subprocess.run([f'{args.gem5}/build/X86/gem5.fast', f'--outdir={output_dir_cpt}/m5out', f'{args.gem5}/configs/deprecated/example/se.py',
                                 '--cpu-type=X86NonCachingSimpleCPU', f'--mem-size={mem_size}',
                                 f'--take-simpoint-checkpoint={output_dir_spt}/simpoints.out,{output_dir_spt}/weights.out,10000000,10000',
                                 f'--cmd={exe}',
                                 f'--options={options}',
                                 f'--output={output_dir_cpt}/stdout', f'--errout={output_dir_cpt}/stderr'],
                                stdout = simout,
                                stderr = simerr,
                                cwd = test_spec_cpt['cd'])
        if result.returncode != 0:
            log(f'[{bench_name}->{test_name}] ERROR: cpt execution failed')
            return 1
    
    return 0
        
def handle_benchmark(bench_name, bench_tests, parent_input_dir, parent_output_dir):
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
