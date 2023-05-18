import argparse
import os
import sys
import types
import glob
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--name', required = True)
parser.add_argument('--llsct', required = True)
parser.add_argument('--ipc', required = True)
parser.add_argument('cmd')
parser.add_argument('args', nargs = '*')
args = parser.parse_args()

gem5_binary = f'{args.llsct}/gem5/build/X86/gem5.fast'
gem5_se_py = f'{args.llsct}/gem5/configs/deprecated/example/se.py'
bench = os.path.basename(os.path.dirname(os.getcwd()))
mem_size = 8589934592

i = 0
while len(glob.glob(f'm5out-res-{args.name}-{i}-*')) > 0:
    i += 1

checkpoint_dir = f'm5out-cpt-{i}'
if not os.path.isdir(checkpoint_dir):
    print(f'ERROR: checkpoint directory does not exist: {checkpoint_dir}', file = sys.stderr)
    exit(1)

# for each checkpoint in checkpoints:

def parse_cpt_name(name):
    assert name.startswith('cpt.')
    name = name.removeprefix('cpt.')
    tokens = name.split('_')
    d = {}
    for key, value in zip(tokens[0::2], tokens[1::2], strict = True):
        d[key] = value
    return types.SimpleNamespace(**d)

stats = []
for checkpoint_path in glob.glob(f'{checkpoint_dir}/cpt.*'):
    checkpoint_name = os.path.basename(checkpoint_path)
    checkpoint_args = parse_cpt_name(checkpoint_name)
    checkpoint_id = int(checkpoint_args.simpoint, 10)
    m5out_res = f'm5out-res-{args.name}-{i}-{checkpoint_id}'
    argstr = ' '.join(args.args)
    result = subprocess.run([gem5_binary, f'--outdir={m5out_res}', gem5_se_py, '--cpu-type=X86O3CPU', f'--mem-size={mem_size}',
                             '--l1d_size=64kB', '--l1i_size=16kB', '--caches', f'--checkpoint-dir={checkpoint_dir}',
                             '--restore-simpoint-checkpoint', f'--checkpoint-restore={checkpoint_id+1}',
                             f'--cmd={args.cmd}', f'--options={argstr}'],
                            capture_output = True,
                            encoding = 'ascii'
                            )
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        exit(result.returncode)
    stats.append((f'{m5out_res}/stats.txt', checkpoint_args))

weighted_ipcs = []
for stats_path, cpt_args in stats:
    with open(stats_path) as f:
        keys = {'simInsts': [], 'simTicks': [], 'system.clk_domain.clock': []}
        for line in f.readlines():
            for key in keys:
                if line.startswith(key):
                    keys[key].append(float(line.split()[1]))

        for l in keys.values():
            assert len(l) == 2

        interval = int(checkpoint_args.interval)
        print('TRACE simInsts:', *keys['simInsts'], file = sys.stderr)
        # assert ((keys['simInsts'][1] - keys['simInsts'][0] / float(interval)) < 0.01)
        
        simTicks = keys['simTicks'][-1]
        period = keys['system.clk_domain.clock'][-1]
        ipc = interval / (simTicks / period)

        weight = float(checkpoint_args.weight)
        weighted_ipcs.append(ipc * weight)

    
with open(args.ipc, 'a') as f:
    print(f'{bench}-{i} {sum(weighted_ipcs)}', file = f)
