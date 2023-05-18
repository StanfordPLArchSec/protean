import argparse
import glob
import subprocess
import os
import sys
import types

parser = argparse.ArgumentParser()
parser.add_argument('--run-sh', required = True)
parser.add_argument('--checkpoint-dir', required = True)
parser.add_argument('--ipc', required = True)
args = parser.parse_args()

stats_paths = []

stats = []

def parse_checkpoint_name(checkpoint_name):
    assert checkpoint_name.startswith('cpt.')
    checkpoint_name = checkpoint_name.removeprefix('cpt.')
    tokens = checkpoint_name.split('_')
    assert len(tokens) % 2 == 0
    d = {}
    for key, value in zip(tokens[0::2], tokens[1::2], strict = True):
        d[key] = value
    return types.SimpleNamespace(**d)

for checkpoint_path in glob.glob(f'{args.checkpoint_dir}/cpt.*'):
    checkpoint_name = os.path.basename(checkpoint_path)
    checkpoint_args = parse_checkpoint_name(checkpoint_name)
    checkpoint_id = int(checkpoint_args.simpoint, 10)
    outdir = f'{os.getcwd()}/m5out-{checkpoint_id}'
    result = subprocess.run(['bash', args.run_sh, '-o', outdir, '-c', str(checkpoint_id + 1)], capture_output = True, encoding = 'ascii')
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode != 0:
        exit(result.returncode)
    stats.append((f'{outdir}/stats.txt', float(checkpoint_args.weight), checkpoint_args))


weighted_ipcs = []
    
for stats_path, weight, checkpoint_args in stats:
    with open(stats_path) as f:
        # simInsts
        # simTicks
        # system.clk_domain.clock
        keys = {'simInsts': [], 'simTicks': [], 'system.clk_domain.clock': []}
        for line in f.readlines():
            for key in keys:
                if line.startswith(key):
                    keys[key].append(float(line.split()[1]))

        for l in keys.values():
            assert len(l) == 2

        interval = int(checkpoint_args.interval)
        assert int(keys['simInsts'][1]) - int(keys['simInsts'][0]) == interval

        simTicks = keys['simTicks'][-1]
        period = keys['system.clk_domain.clock'][-1]
        ipc = interval / (simTicks / period)

        weighted_ipcs.append(ipc * weight)

with open(args.ipc, 'w') as f:
    print(sum(weighted_ipcs), file = f)



