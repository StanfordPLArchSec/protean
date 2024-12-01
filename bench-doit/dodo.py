import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "../scripts"))
from benchmark import *

# User-defined config vars
root = "."
test_suites = {
    "base": "/home/nmosier/llsct2/bench-ninja/sw/base/test-suite",
    "nst": "/home/nmosier/llsct2/bench-ninja/sw/ptex-nst/test-suite",
}
gem5 = "/home/nmosier/llsct2/gem5/pincpu"
fp = False

# Dependent config vars
gem5_exe = f"{gem5}/build/X86/gem5.opt"

# Benchmarks
benches = []
benches.extend(get_cpu2017_int(test_suites))
if fp:
    benches.extend(get_cpu2017_fp(test_suites))

def bench_outdir(bench: Benchmark) -> str:
    return os.path.join(root, bench.name)

def bench_input_outdir(bench: Benchmark, input: Benchmark.Input) -> str:
    return os.path.join(bench_outdir(bench), input.name)

def generate_uniqtrace(dir_rel: str, exe: Benchmark.Executable, input: Benchmark.Input):
    rundir_rel = f"{dir_rel}/run"
    outdir_rel = f"{dir_rel}/qtrace"
    outdir_abs = os.path.abspath(outdir_rel)
    gem5_pin = f"{gem5}/configs/pin.py"
    qtrace_name = "qtrace.out"
    yield {
        "basename": outdir_rel,
        "actions": [
            f"mkdir -p {dir_rel} {outdir_rel}",
            f"[ -d {rundir_rel} ] || ln -sf {os.path.abspath(exe.wd)} {rundir_rel}",
            f"/usr/bin/time -vo {outdir_rel}/time.txt {gem5_exe} -re --silent-redirect -d {outdir_rel} {gem5_pin} --chdir={rundir_rel} --mem-size={input.mem_size} --max-stack-size={input.stack_size} --stdout=stdout.txt --stderr=stderr.txt --pin-tool-args='-qtrace {outdir_rel}/{qtrace_name}' -- {exe.path} {input.args}",
        ],
        "file_dep": [gem5_exe, gem5_pin, exe.path],
        "targets": [f"{outdir_rel}/{filename}" for filename in ["simout.txt", "simerr.txt", "stdout.txt", "stderr.txt", qtrace_name, "time.txt"]],
    }



def generate_all_uniqtraces(benches):
    for bench in benches:
        for exe in bench.exes:
            for input in bench.inputs:
                dir = os.path.join(bench.name, input.name, exe.name)
                yield generate_uniqtrace(dir, exe, input)


def task_all():
    yield generate_all_uniqtraces(benches)
