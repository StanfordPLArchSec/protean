import argparse
import sys
import os
import types
import json

parser = argparse.ArgumentParser()
parser.add_argument('--config', required = True)
parser.add_argument('--root', required = True)
parser.add_argument('--outdir', required = True)
args = parser.parse_args()

with open(args.config) as f:
    config = types.SimpleNamespace(**json.load(f))


# Prepare output directory
os.makedirs(args.outdir, exist_ok = True)
    
# Build glibc
glibc_src=
os.mkdir(
