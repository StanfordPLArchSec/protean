import sys
import json
import argparse
import glob
import collections
import os

parser = argparse.ArgumentParser()
parser.add_argument('dirs', nargs = '+')
parser.add_argument('--key', required = True)
# parser.add_arguments('--avg', choices = ['geo', 'arith'], required = True)
args = parser.parse_args()

# map from bench name to list of jsons
json_map = collections.defaultdict(lambda: [None] * len(args.dirs))

for dir_idx, dir in enumerate(args.dirs):
    # find all jsons
    for json_path in glob.glob(f'{dir}/**/*.json', recursive = True):
        bench_name = os.path.basename(os.path.dirname(json_path))
        if os.path.basename(json_path).removesuffix('.json') != bench_name:
            # print(f'warning: found stray json file: {json_path}', file = sys.stderr)
            continue
        # with open(json_path) as f:
        json_map[bench_name][dir_idx] = json_path


# check that no benches were missing
# for bench_name, json_list in json_map.items():
#     if len(json_list) != len(args.dirs):
#         print(f'error: missing JSON file for {bench_name}', file = sys.stderr)
#         exit(1)

# print out results
for bench_name in sorted(list(json_map)):
    json_list = json_map[bench_name]
    tokens = [bench_name]
    for json_path in json_list:
        if json_path is None:
            tokens.append('-')
            continue
        with open(json_path) as f:
            j = json.load(f)
        try:
            tokens.append(str(j['tests'][0]['metrics'][args.key]))
        except:
            print(f'error: JSON file missing key(s): {json_path}', file = sys.stderr)
            exit(1)
    print(*tokens)
