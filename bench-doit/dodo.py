import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "../scripts"))
from benchmark import *
import types
import json
import collections

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
simpoint_interval_length = 50000000 # 50M instructions
warmup_interval_length = 10000000 # 10M instructions
simpoint = "/home/nmosier/llsct2/simpoint/bin/simpoint"

# Dependent config vars
gem5_exe = f"{gem5}/build/X86/gem5.opt"
gem5_pin = f"{gem5}/configs/pin.py"
gem5_pintool = f"{gem5}/build/X86/cpu/pin/libclient.so"

# Benchmarks
benches = []
benches.extend(get_cpu2017_int(test_suites))
if fp:
    benches.extend(get_cpu2017_fp(test_suites))

def bench_outdir(bench: Benchmark) -> str:
    return os.path.join(root, bench.name)

def bench_input_outdir(bench: Benchmark, input: Benchmark.Input) -> str:
    return os.path.join(bench_outdir(bench), input.name)

def build_gem5_pin_command(rundir: str, outdir: str, exe: Benchmark.Executable,
                           input: Benchmark.Input, pintool_args: str) -> str:
    return f"/usr/bin/time -vo {outdir}/time.txt {gem5_exe} -re --silent-redirect -d {outdir} {gem5_pin} --chdir={rundir} --mem-size={input.mem_size} --max-stack-size={input.stack_size} --stdout=stdout.txt --stderr=stderr.txt --pin-tool-args='{pintool_args}' -- {exe.path} {input.args}"

def generate_gem5_pin_command(dir: str, outdir: str, exe: Benchmark.Executable,
                              input: Benchmark.Input, pintool_args: str,
                              file_dep: List[str],
                              targets: List[str]):
    rundir = f"{dir}/run"
    implicit_targets = [f"{outdir}/{name}.txt" for name in ["simout", "simerr", "stdout", "stderr", "time"]]
    yield {
        "basename": outdir,
        "actions": [
            f"mkdir -p {dir} {outdir}",
            f"[ -d {rundir} ] || ln -sf {os.path.abspath(exe.wd)} {rundir}",
            build_gem5_pin_command(rundir, outdir, exe, input, pintool_args),
        ],
        "file_dep": [gem5_exe, gem5_pin, gem5_pintool, exe.path, *file_dep],
        "targets": targets + implicit_targets,
    }
    

def get_srcloc(line: str) -> str:
    j = json.loads(line)
    assert len(j["Symbol"]) == 1
    symbol = types.SimpleNamespace(**j["Symbol"][0])
    addr = j["Address"].removeprefix("0x")
    if len(symbol.FileName) == 0 or \
       symbol.Line == 0 or \
       symbol.Column == 0: # TODO: Maybe too strict?
        return None
    return f"{addr} {symbol.FileName}:{symbol.Line}:{symbol.Column}"

def compute_srclocs(inpath: str, outpath: str):
    with open(inpath) as infile, \
         open(outpath, "wt") as outfile:
        for line in infile:
            srcloc = get_srcloc(line)
            if srcloc:
                print(srcloc, file = outfile)

def generate_srclocs(dir: str):
    srclist = f"{dir}/srclist.txt"
    srclocs = f"{dir}/srclocs.txt"
    yield {
        "basename": f"{dir}/srclocs",
        "actions": [(compute_srclocs, [], {"inpath": srclist, "outpath": srclocs})],
        "file_dep": [srclist],
        "targets": [srclocs],
    }

def generate_bbhist(dir: str, exe: Benchmark.Executable, input: Benchmark.Input):
    bbhist_base = f"{dir}/bbhist"
    bbhist_dir = bbhist_base
    bbhist_txt = f"{dir}/bbhist.txt"
    yield generate_gem5_pin_command(
        dir = dir,
        outdir = bbhist_dir,
        exe = exe,
        input = input,
        pintool_args = f"-bbhist {bbhist_txt}",
        file_dep = [],
        targets = [bbhist_txt],
    )

def compute_instlist(inpath: str, outpath: str):
    with open(inpath) as infile, \
         open(outpath, "wt") as outfile:
        insts = set()
        for line in infile:
            insts.update(line.split()[1].split(","))
        for inst in sorted(list(insts)):
            print(inst, file = outfile)

def generate_instlist(dir: str):
    bbhist_txt = f"{dir}/bbhist.txt"
    instlist_base = f"{dir}/instlist"
    instlist_txt = f"{instlist_base}.txt"
    yield {
        "basename": instlist_base,
        "actions": [(compute_instlist, [], {"inpath": bbhist_txt, "outpath": instlist_txt})],
        "file_dep": [bbhist_txt],
        "targets": [instlist_txt],
    }

def assert_equal_file_lengths(*paths):
    def file_length(path) -> int:
        num_lines = 0
        with open(path) as f:
            for line in f:
                num_lines += 1
        return num_lines

    lengths = list(map(file_length, paths))
    for length in lengths[1:]:
        assert length == lengths[0]

def generate_srclist(dir: str, exe: Benchmark.Executable):
    instlist = f"{dir}/instlist.txt"
    srclist = f"{dir}/srclist.txt"
    yield {
        "basename": f"{dir}/srclist",
        "actions": [
            f"{addr2line} --exe {exe.path} --output-style=JSON < {instlist} > {srclist}",
            (assert_equal_file_lengths, [instlist, srclist]),
        ],
        "file_dep": [instlist],
        "targets": [srclist],
    }

def compute_lehist(bbhist_path: str, srclocs_path: str, outpath: str):
    # Parse srclocs.
    locs = dict()
    with open(srclocs_path) as f:
        for line in f:
            inst, loc = line.split()
            locs[inst] = loc

    # Compute location edge histogram.
    hist = collections.defaultdict(int)
    with open(bbhist_path) as f:
        for line in f:
            tokens = line.split()
            block_hits = int(tokens[0])
            insts = tokens[1].split(",")
            assert len(insts) > 0
            for inst1, inst2 in zip(insts[:-1], insts[1:]):
                if inst1 in locs and inst2 in locs:
                    loc1 = locs[inst1]
                    loc2 = locs[inst2]
                    if loc1 != loc2:
                        hist[(loc1, loc2)] += block_hits

    # Write out sorted histogram.
    hist = sorted(list(hist.items()))
    with open(outpath, "wt") as f:
        for locs, count in hist:
            print(*locs, count, file = f)


def generate_lehist(dir: str):
    lehist_base = f"{dir}/lehist"
    lehist_txt = f"{lehist_base}.txt"
    bbhist_txt = f"{dir}/bbhist.txt"
    srclocs_txt = f"{dir}/srclocs.txt"
    yield {
        "basename": lehist_base,
        "actions": [(compute_lehist, [], {
            "bbhist_path": bbhist_txt,
            "srclocs_path": srclocs_txt,
            "outpath": lehist_txt,
            })],
        "file_dep": [bbhist_txt, srclocs_txt],
        "targets": [lehist_txt],
    }

def compute_shlocedges(outpath: str, inpaths: List[str]):
    assert len(inpaths) > 0

    def get_line_set(path: str) -> set:
        with open(path) as f:
            return set(f)

    lines = list(set.intersection(*map(get_line_set, inpaths)))
    lines.sort()

    with open(outpath, "wt") as f:
        for line in lines:
            f.write(line)
        
    
def generate_shlocedges(dir: str, exes: List[Benchmark.Executable]):
    lehists = [f"{dir}/{exe.name}/lehist.txt" for exe in exes]
    out_base = f"{dir}/locedges"
    out = f"{out_base}.txt"
    yield {
        "basename": out_base,
        "actions": [(compute_shlocedges, [], {"outpath": out, "inpaths": lehists})],
        "file_dep": lehists,
        "targets": [out],
    }

def compute_progmark(outpath: str, bbhist: str, locedges: str, locmap: str):
    # Parse locmap.
    # Format: instaddr srclocstr
    inst_to_loc = {}
    with open(locmap) as f:
        for line in f:
            inst, loc = line.split()
            inst_to_loc[inst] = loc        

    # Parse locedges.
    # Format: srcloc dstloc count
    loc_edges = set()
    with open(locedges) as f:
        for line in f:
            loc1, loc2, count = line.split()
            loc_edges.add((loc1, loc2))

    # Parse bbhist into an intra-block instruction successor map.
    # Format: count inst1,inst2,...,instn
    # For the above line, we'd parse it into inst1->inst2, inst2->inst3, ..., inst{n-1}->instn.
    inst_edges = {}
    with open(bbhist) as f:
        for line in f:
            count, insts = line.split()
            insts = insts.split(",")
            assert len(insts) > 0
            for inst1, inst2 in zip(insts[:-1], insts[1:]):
                if inst1 in inst_to_loc and inst2 in inst_to_loc:
                    loc1 = inst_to_loc[inst1]
                    loc2 = inst_to_loc[inst2]
                    if (loc1, loc2) in loc_edges:
                        assert inst_edges.get(inst1, inst2) == inst2
                        inst_edges[inst1] = inst2

    # Print inst_edges to file.
    with open(outpath, "wt") as f:
        for inst1 in inst_edges:
            print(inst1, file = f)

def generate_progmark(dir: str):
    out_base = f"{dir}/progmark"
    out_txt = f"{out_base}.txt"
    bbhist_txt = f"{dir}/bbhist.txt"
    locedges_txt = os.path.join(os.path.dirname(dir), "locedges.txt")
    locmap_txt = f"{dir}/srclocs.txt"
    yield {
        "basename": out_base,
        "actions": [(compute_progmark, [], {
            "outpath": out_txt,
            "bbhist": bbhist_txt,
            "locedges": locedges_txt,
            "locmap": locmap_txt,
        })],
        "file_dep": [bbhist_txt, locedges_txt, locmap_txt],
        "targets": [out_txt],
    }

def generate_bbv(dir: str, exe: Benchmark.Executable, input: Benchmark.Input):
    bbv_base = f"{dir}/bbv"
    bbv_txt = f"{dir}/bbv.txt"
    bbv_dir = bbv_base
    progmark_txt = f"{dir}/progmark.txt"
    yield generate_gem5_pin_command(
        dir = dir,
        outdir = bbv_dir,
        exe = exe,
        input = input,
        pintool_args = f"-slev {bbv_txt} -slev-interval {simpoint_interval_length} -slev-progmark {progmark_txt}",
        file_dep = [progmark_txt],
        targets = [bbv_txt],
    )
    
def generate_all(benches):
    for bench in benches:
        for input in bench.inputs:
            for exe in bench.exes:
                dir = os.path.join(bench.name, input.name, exe.name)
                yield generate_bbhist(dir, exe, input)
                yield generate_instlist(dir)
                yield generate_srclist(dir, exe)
                yield generate_srclocs(dir)
                yield generate_lehist(dir)

            # Among all exes, generate the shared set of locedges with identical hit counts.
            yield generate_shlocedges(os.path.join(bench.name, input.name), bench.exes)

            # Collect source location edge vectors for leader.
            for exe in bench.exes:
                dir = os.path.join(bench.name, input.name, exe.name)
                yield generate_bbv(dir, exe, input)
                yield generate_progmark(dir)

def task_all():
    yield generate_all(benches)
