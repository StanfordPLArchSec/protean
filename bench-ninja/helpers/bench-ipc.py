import argparse
import glob
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument('cptroot')
parser.add_argument('cptipcs', nargs='+')
args = parser.parse_args()

total_ipc = 0
total_weight = 0

for cptipc in args.cptipcs:
    cptidx = os.path.basename(os.path.dirname(os.path.abspath(cptipc)))
    cptidx = int(cptidx, 10)
    glob_path = os.path.join(args.cptroot, f'cpt.simpoint_{cptidx:02}_*')
    matches = glob.glob(glob_path)
    assert len(matches) <= 1
    if len(matches) == 1:
        tokens = os.path.basename(matches[0]).removeprefix('cpt.').split('_')
        d = dict(zip(tokens[0::2], tokens[1::2]))
        weight = float(d['weight'])
        total_weight += weight
        with open(cptipc) as f:
            try: 
                ipc = float(f.read())
            except:
                print(f'failed to parse file: {cptipc}', file=sys.stderr)
                exit(1)
            if ipc < 0.01 and os.path.basename(cptipc) not in ['miss-rate.txt', 'miss-penalty.txt']:
                print(f'ipc less than threshold, aborting: {cptipc}', file=sys.stderr)
                exit(1)
            total_ipc += ipc * weight
            
    else:
        print(f'note: missing checkpoint {cptidx}', file=sys.stderr)
        pass

if total_weight < 0.9 or total_weight > 1.1:
    print('error: total weight less than required threshold', file=sys.stderr)
    exit(1)

tolerance = 0.1
if not (1 - tolerance <= total_weight and total_weight <= 1 + tolerance):
    print(f'warning: total weight out of tolerance range: {total_weight}', file=sys.stderr)
    exit(1)

print(total_ipc / total_weight, total_weight)
