#!/usr/bin/python3

import argparse
import bisect
import gzip

parser = argparse.ArgumentParser()
parser.add_argument('--ref-bbv', required = True, help = 'Path to reference BBV')
parser.add_argument('--exp-bbv', required = True, help = 'Path to experiment BBV')
parser.add_argument('--ref-spt', required = True, help = 'Path to reference simpoints')
args = parser.parse_args()

def get_num_calls(line: str) -> int:
    assert line.startswith('calls')
    tokens = line.split()
    assert len(tokens) == 2
    return int(tokens[1])

# First, create an array of traces for ref and exp's bbvs.
# These are really just pairs of functino counts.
def get_bbv(f) -> list:
    bbv = list()
    calls_in = 0
    lines = f.readlines()
    for i, line in enumerate(lines):
        if line.startswith('calls'):
            calls_in = get_num_calls(line)
        elif line.startswith('T'):
            calls_out = get_num_calls(lines[i + 1])
            bbv.append((calls_in, calls_out))
    return bbv

ref_bbv = None
exp_bbv = None
with gzip.open(args.ref_bbv, 'rt') as f:
    ref_bbv = get_bbv(f)
with gzip.open(args.exp_bbv, 'rt') as f:
    exp_bbv = get_bbv(f)

def map_interval_error(ref_pair, exp_pair):
    pass

def map_interval(ref_interval: int, ref_bbv: list, exp_bbv: list) -> int:
    # Find the reference call counts.
    ref_in, ref_out = ref_bbv[ref_interval]

    # Want to find the first exp interval that overlaps with the ref interval.
    # ref:                (ref_in, ref_out)
    # exp: (exp_in, exp_out)
    
    exp_interval_begin = bisect.bisect(exp_bbv, ref_in, key = lambda exp_p: exp_p[1])
    exp_interval_end = bisect.bisect(exp_bbv, ref_out, key = lambda exp_p: exp_p[0])
    exp_interval_begin = max(exp_interval_begin - 1, 0)
    exp_interval_end = min(exp_interval_end + 1, len(exp_bbv))

    assert exp_interval_begin < exp_interval_end

    def map_interval_error(exp_interval) -> int:
        exp_in, exp_out = exp_bbv[exp_interval]
        error = abs(exp_in - ref_in) + abs(exp_out - ref_out)
        return error
    exp_interval = min(range(exp_interval_begin, exp_interval_end), key = map_interval_error)
    return exp_interval, map_interval_error(exp_interval)
    

# Generate exp's simpoint file.
with open(args.ref_spt) as f:
    for ref_spt_line in f:
        ref_tokens = ref_spt_line.split()
        ref_interval = int(ref_tokens[0])
        exp_interval, abs_error = map_interval(ref_interval, ref_bbv, exp_bbv)
        ref_range = ref_bbv[ref_interval]
        exp_range = exp_bbv[exp_interval]
        ref_calls = ref_range[1] - ref_range[0]
        exp_calls = exp_range[1] - exp_range[0]
        rel_ref_error = abs_error / ref_calls
        rel_exp_error = abs_error / exp_calls
        print(f'{exp_interval} {ref_tokens[1]} # {ref_interval} {ref_bbv[ref_interval]} -> {exp_interval} {exp_bbv[exp_interval]} :: error abs={abs_error} rel-ref={rel_ref_error * 100 :.2f}% rel-exp={rel_exp_error * 100 :.2f}%')
