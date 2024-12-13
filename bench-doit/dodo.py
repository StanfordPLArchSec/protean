import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "../scripts"))
from benchmark import *
import types
import json

# User-defined config vars
root = "."
test_suites = {
    "base": "/home/nmosier/llsct2/bench-ninja/sw/base/test-suite",
    "nst": "/home/nmosier/llsct2/bench-ninja/sw/ptex-nst/test-suite",
}
gem5 = "/home/nmosier/llsct2/gem5/pincpu"
fp = False
llvm = "/home/nmosier/llsct2/llvm/base/build"
addr2line = f"{llvm}/bin/llvm-addr2line"

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

def convert_srcinfo(s: str) -> str:
    j = json.loads(s)
    symbols = j["Symbol"]
    id_tokens = []
    addr = j["Address"]
    for symbol in symbols:
        symbol = types.SimpleNamespace(**symbol)
        if len(symbol.FileName) == 0 or \
           symbol.Line == 0 or \
           symbol.Column == 0: # TODO: Maybe too strict?
            return (addr, None)
        id_tokens.append(f"{symbol.FileName}:{symbol.Line}:{symbol.Column}")
    assert len(id_tokens) == len(symbols)
    if len(id_tokens) == 0:
        return (addr, None)
    return (addr, id_tokens[0])

def convert_srcinfos(qpath: str, srcpath: str, outpath: str):
    with open(qpath, "rt") as qfile, open(srcpath, "rt") as srcfile, open(outpath, "wt") as outfile:
        for qline, srcinfo in zip(qfile, srcfile):
            addr, srcid = convert_srcinfo(srcinfo)
            qtokens = qline.split()
            assert int(qtokens[0], 0) == int(addr, 0)
            if srcid:
                print(addr, qtokens[1], srcid, file=outfile)
            

def generate_uniqtrace(dir_rel: str, exe: Benchmark.Executable, input: Benchmark.Input):
    rundir_rel = f"{dir_rel}/run"
    outdir_rel = f"{dir_rel}/qtrace"
    outdir_abs = os.path.abspath(outdir_rel)
    gem5_pin = f"{gem5}/configs/pin.py"
    qtrace_name = "qtrace0.txt"
    qtrace = f"{outdir_rel}/{qtrace_name}"

    yield {
        "basename": qtrace,
        "actions": [
            f"mkdir -p {dir_rel} {outdir_rel}",
            f"[ -d {rundir_rel} ] || ln -sf {os.path.abspath(exe.wd)} {rundir_rel}",
            f"/usr/bin/time -vo {outdir_rel}/time.txt {gem5_exe} -re --silent-redirect -d {outdir_rel} {gem5_pin} --chdir={rundir_rel} --mem-size={input.mem_size} --max-stack-size={input.stack_size} --stdout=stdout.txt --stderr=stderr.txt --pin-tool-args='-qtrace {qtrace}' -- {exe.path} {input.args}",
        ],
        "file_dep": [gem5_exe, gem5_pin, exe.path],
        "targets": [f"{outdir_rel}/{filename}" for filename in ["simout.txt", "simerr.txt", "stdout.txt", "stderr.txt", qtrace_name, "time.txt"]],
    }

    srcinfo_json = f"{outdir_rel}/srcinfo.json"
    yield {
        "basename": srcinfo_json,
        "actions": [f"cut -d' ' -f1 < {qtrace} | {addr2line} --addresses --inlines --exe {exe.path} --output-style=JSON > {srcinfo_json}"],
        "file_dep": [qtrace, exe.path],
        "targets": [srcinfo_json],
    }

    qtrace_annot = f"{outdir_rel}/qtrace1.txt"
    yield {
        "basename": qtrace_annot,
        "actions": [(convert_srcinfos, [qtrace, srcinfo_json, qtrace_annot], {})],
        "file_dep": [srcinfo_json, qtrace],
        "targets": [qtrace_annot],
    }
    

def generate_all_uniqtraces(benches):
    for bench in benches:
        for exe in bench.exes:
            for input in bench.inputs:
                dir = os.path.join(bench.name, input.name, exe.name)
                yield generate_uniqtrace(dir, exe, input)


def task_all():
    yield generate_all_uniqtraces(benches)
