#!/usr/bin/python3

# TODO: Need to handle Benchmark.Input.stdin.

import argparse
import os
import json
import sys
import ninja_syntax
import benchmark
from benchmark import Benchmark, get_cpu2017_int, get_cpu2017_fp

parser = argparse.ArgumentParser()
parser.add_argument("--test-suite", required = True, help = "Path to LLVM test suite containing SPEC benchmarks")
parser.add_argument("--gem5", required = True, help = "Path to gem5 directory. build/X86/gem5.opt must be present")
parser.add_argument("--out", default = "build.ninja", help = "Path to build output file")
parser.add_argument("--fp", action = "store_true", help = "Include FP benchmarks")
args = parser.parse_args()

build_file = open(args.out, "wt")
ninja = ninja_syntax.Writer(build_file)

gem5_dir = os.path.abspath(args.gem5)
gem5_exe = os.path.join(gem5_dir, "build", "X86", "gem5.opt")
gem5_config = os.path.join(gem5_dir, "configs", "deprecated", "example", "se.py")

# Define Ninja rules

ninja.build(
    outputs = "dummy",
    rule = "phony",
)

## Run simulation
ninja.rule(
    name = "simulate-kvm",
    command = f"ln -sf $wd $outdir/wd && cd $outdir/wd && /usr/bin/time -vo $outdir/time.out {gem5_exe} -re --silent-redirect -d $outdir {gem5_config} --cpu-type=X86KvmCPU --mem-size=$mem_size --cmd=$exe --options='$args' --errout=stderr.txt --output=stdout.txt && touch $outdir/run.stamp",
    description = f"Run gem5 simulation on X86KvmCPU: $desc",
)

def emit_simulate_kvm(bench: Benchmark, input: Benchmark.Input):
    outdir_rel = os.path.join("kvm", bench.name, input.name)
    outdir_abs = os.path.abspath(outdir_rel)
    stamp_rel = os.path.join(outdir_rel, "run.stamp")
    ninja.build(
        outputs = stamp_rel,
        rule = "simulate-kvm",
        inputs = [bench.exe, gem5_exe, gem5_config],
        variables = {
            "wd": bench.wd,
            "mem_size": input.mem_size,
            "args": input.args,
            "exe": bench.exe,
            "outdir": os.path.join(outdir_abs),
            "desc": f"{bench.name}.{input.name} -> {outdir_rel}",
        },
    )
    return stamp_rel

benches = []
benches.extend(get_cpu2017_int(os.path.abspath(args.test_suite)))
if args.fp:
    benches.extend(get_cpu2017_fp(os.path.abspath(args.test_suite)))

all_outputs = []
for bench in benches:
    for input in bench.inputs:
        all_outputs.append(emit_simulate_kvm(bench, input))
        
ninja.build(
    outputs = "all",
    rule = "phony",
    inputs = all_outputs,
)



# SELF-REGENERATION INFRA

## Compute the dependenices.

# Regenerate itself.
regenerate_args = " " .join(sys.argv[1:])
ninja.rule(
    name = "regenerate",
    command = f"cd {os.getcwd()} && {__file__} {regenerate_args}",
    description = "Regenerate build file",
    depfile = "$out.d",
)
ninja.build(
    outputs = args.out,
    rule = "regenerate",
    inputs = [],
)

# Write dep file
with open(f"{args.out}.d", "wt") as f:
    files = []
    for module in sys.modules.values():
        if "__file__" in module.__dict__:
            path = module.__file__
            if path:
                files.append(path)
    print(args.out, ":", *files, file = f)
