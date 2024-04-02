import argparse
import os
import sys
import subprocess
import json
import shutil
import copy
import glob
import types
import multiprocessing
import colorama

DONE = f'{colorama.Fore.GREEN}DONE{colorama.Style.RESET_ALL}'
ERROR = f'{colorama.Fore.RED}ERROR{colorama.Style.RESET_ALL}'

jobs_sema = None 

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


def load_benchspec(path: str) -> dict:
    with open(path) as f:
        bench_spec = json.load(f)

    bench_spec = resolve_inherits(bench_spec)
    assert '"inherit"' not in json.dumps(bench_spec)

    return bench_spec

def escape_str(s: str) -> str:
    assert '"' not in s
    return f'"{s}"'

def execute_test(cmd: list, test_spec: dict, stdout: str = None, stderr: str = None, cmdline: str = None, **kwargs) -> int:
    copy_assets(test_spec)
    if cmdline:
        with open(cmdline, 'w') as f:
            if type(cmd) is list:
                cmdlinestr = ' '.join(map(escape_str, cmd))
            else:
                assert type(cmd) is str
                cmdlinestr = cmd
            print(cmdlinestr, file = f)
    with jobs_sema, \
         open(stdout, 'w') as stdout, \
         open(stderr, 'w') as stderr:
        cd = test_spec['cd']
        os.makedirs(cd, exist_ok = True)
        result = subprocess.run(cmd,
                                cwd = cd,
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
    if len(test_spec['verify_commands']) == 0:
        print('CANNOT VERIFY: ', output_dir, file = sys.stderr)
        exit(1)
        
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
            shutil.copymode(copy['from'], copy['to'])            

def parse_checkpoint_args(checkpoint_dir):
    checkpoint_name = os.path.basename(checkpoint_dir)
    assert checkpoint_name.startswith('cpt.')
    checkpoint_name = checkpoint_name.removeprefix('cpt.')
    tokens = checkpoint_name.split('_')
    args = dict()
    for k, v in zip(tokens[0::2], tokens[1::2]):
        args[k] = v
    return types.SimpleNamespace(**args)


def expand_test_spec(exe: str, test_spec: dict):
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

    if 'exe' in test_spec:
        exe = test_spec['exe']
        del test_spec['exe']
    test_spec['cmd'] = [exe, *test_spec['args']]
    del test_spec['args']

    if 'cpu' not in test_spec:
        test_spec['cpu'] = 'X86KvmCPU'

    if 'memsize' not in test_spec:
        # test_spec['memsize'] = '512MB'
        test_spec['memsize'] = '512MB'

    if 'interval' not in test_spec:
        test_spec['interval'] = 10000000 # 10M

    if 'bbvcpu' not in test_spec:
        test_spec['bbvcpu'] = 'X86NonCachingSimpleCPU'


def find_executable(name, base):
    for root, dirs, files in os.walk(base):
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



# NOTE: Should not modify input d.
def perform_test_substitutions(d, bench_name, test_name, input_dir, output_dir, *, test_suite): # returns d'
    if d is None:
        return None

    rec = lambda x: perform_test_substitutions(x, bench_name, test_name, input_dir, output_dir, test_suite = test_suite)
    
    if type(d) == str:
        # substitute directly on string
        d = d.replace('%S', '%R/..')
        d = d.replace('%R', input_dir)
        d = d.replace('%T', output_dir)
        d = d.replace('%n', test_name)
        d = d.replace('%N', bench_name)
        d = d.replace('%b', f'{test_suite}/tools')
        d = d.replace('%i', bench_name.split('.')[0])
        assert '%' not in d
    elif type(d) == dict:
        d = dict([(key, rec(value)) for key, value in d.items()])
    elif type(d) == list:
        d = [rec(x) for x in d]
    return d

