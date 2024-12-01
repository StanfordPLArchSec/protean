import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "../scripts"))
from benchmark import *

# User-defined config vars
root = "."
test_suite_base = "/home/nmosier/llsct2/bench-ninja/sw/base/test-suite"
gem5 = "/home/nmosier/llsct2/gem5/pincpu"
fp = False

# Dependent config vars
gem5_exe = f"{gem5}/build/X86/gem5.opt"

# Benchmarks
benches = []
benches.extend(get_cpu2017_int(os.path.abspath(test_suite_base)))    
if fp:
    benches.extend(get_cpu2017_fp(os.path.abspath(test_suite_base)))


def bench_outdir(bench: Benchmark) -> str:
    return os.path.join(root, bench.name)

def bench_input_outdir(bench: Benchmark, input: Benchmark.Input) -> str:
    return os.path.join(bench_outdir(bench), input.name)

def generate_uniqtrace(bench: Benchmark, input: Benchmark.Input):
    dir_rel = bench_input_outdir(bench, input)
    rundir_rel = f"{dir_rel}/run"
    outdir_rel = f"{dir_rel}/qtrace"
    outdir_abs = os.path.abspath(outdir_rel)
    gem5_pin = f"{gem5}/configs/pin.py"
    qtrace_name = "qtrace.out"
    yield {
        "basename": f"{bench.name}.{input.name}.uniqtrace",
        "actions": [
            f"mkdir -p {dir_rel} {outdir_rel}",
            f"ln -sf {os.path.abspath(bench.wd)} {rundir_rel}",
            f"cd {rundir_rel} && /usr/bin/time -vo {outdir_abs}/time.txt {gem5_exe} -re --silent-redirect -d {outdir_abs} {gem5_pin} --mem-size={input.mem_size} --max-stack-size={input.stack_size} --errout=stderr.txt --output=stdout.txt --pin-tool-args='-qtrace {outdir_abs}/{qtrace_name}' -- {bench.exe} {input.args}",
        ],
        "file_dep": [gem5_exe, gem5_pin, bench.exe],
        "targets": [f"{outdir_rel}/{filename}" for filename in ["simout.txt", "simerr.txt", "stdout.txt", "stderr.txt", qtrace_name, "time.txt"]],
    }



def generate_all_uniqtraces(benches):
    for bench in benches:
        for input in bench.inputs:
            yield generate_uniqtrace(bench, input)


def task_all():
    yield generate_all_uniqtraces(benches)
