import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), "../scripts"))
from benchmark import *
from compiler import *
from libc import *
from libcxx import *
import types
import json
import collections
import copy
import doit

# User-defined config vars
root = "."
test_suites = {
    "base": "/home/nmosier/llsct2/bench-ninja/sw/base/test-suite",
    # "nst": "/home/nmosier/llsct2/bench-ninja/sw/ptex-nst/test-suite",
    # "slh": "/home/nmosier/llsct2/bench-ninja/sw/slh/test-suite",
}
gem5 = "/home/nmosier/llsct2/gem5/pincpu"
fp = int(doit.get_var("fp", "1")) > 0
llvm = "/home/nmosier/llsct2/llvm/base-17/build"
addr2line = f"{llvm}/bin/llvm-addr2line"
simpoint_interval_length = 50000000 # 50M instructions
warmup_interval_length = 10000000 # 10M instructions
simpoint_exe = "/home/nmosier/llsct2/simpoint/bin/simpoint"
simpoint_max = 10

# Dependent config vars
gem5_exe = f"{gem5}/build/X86/gem5.opt"
gem5_pin = f"{gem5}/configs/pin.py" # TODO: Remove.
gem5_pintool = f"{gem5}/build/X86/cpu/pin/libclient.so"

# Benchmarks
benches = []
benches.extend(get_cpu2017_int(test_suites))
if fp:
    benches.extend(get_cpu2017_fp(test_suites))

# Fix up benchmarks if they contain '%', since pydoit later interprets
# them as a format string.
for bench in benches:
    for input in bench.inputs:
        input.args = input.args.replace('%', '%%')

debug = os.getenv("DEBUG")
if debug and int(debug):
    import pdb
    pdb.set_trace()
    

# Experiments:
# TODO: Define elsewhere.
class Experiment:
    def __init__(self, name: str, sw: str, gem5_exe: str, gem5_script: str, gem5_script_args: str):
        self.name = name
        self.sw = sw
        self.gem5_exe = gem5_exe
        self.gem5_script = gem5_script
        self.gem5_script_args = gem5_script_args

exp_base = Experiment(
    name = "base",
    gem5_exe = "/home/nmosier/llsct2/gem5/base/build/X86/gem5.opt",
    gem5_script = "/home/nmosier/llsct2/gem5/base/configs/deprecated/example/se.py",
    gem5_script_args = "",
    sw = "base",
)
exp_slh = copy.copy(exp_base)
exp_slh.name = "slh"
exp_slh.sw = "slh"
exp_stt = Experiment(
    name = "stt",
    gem5_exe = "/home/nmosier/llsct2/gem5/stt/build/X86/gem5.opt",
    gem5_script = "/home/nmosier/llsct2/gem5/stt/configs/deprecated/example/se.py",
    gem5_script_args = "--stt --implicit-channel=Lazy --speculation-model=Ctrl",
    sw = "base",
)
exps = [
    exp_base,
    exp_stt,
    # exp_slh,
]

def bench_outdir(bench: Benchmark) -> str:
    return os.path.join(root, bench.name)

def bench_input_outdir(bench: Benchmark, input: Benchmark.Input) -> str:
    return os.path.join(bench_outdir(bench), input.name)

# TODO: Remove `command_prefix`.
def build_gem5_command(rundir: str,
                       outdir: str,
                       exe: Benchmark.Executable,
                       input: Benchmark.Input,
                       gem5_exe: str,
                       gem5_script: str,
                       gem5_script_args: str,
                       gem5_exe_args: str = "",
                       command_prefix: str = ""):
    # TODO: utility function to join strip out empty tokens.
    l = [
        command_prefix,
        f"/usr/bin/time -vo {outdir}/time.txt {gem5_exe} -re --silent-redirect -d {outdir}",
        gem5_exe_args, gem5_script, f"--chdir={rundir}", f"--mem-size={input.mem_size}",
        f"--max-stack-size={input.stack_size}",
        f"--stdin={input.stdin}", "--stdout=stdout.txt", f"--stderr=stderr.txt",
        gem5_script_args,
        "--",
        exe.path, input.args,
    ]
    if None in l:
        print(l, file = sys.stderr)
    return " ".join(l)

# TODO: Remove.
def build_gem5_pin_command(rundir: str, outdir: str, exe: Benchmark.Executable,
                           input: Benchmark.Input, pintool_args: str) -> str:
    return f"/usr/bin/time -vo {outdir}/time.txt {gem5_exe} -re --silent-redirect -d {outdir} {gem5_pin} --chdir={rundir} --mem-size={input.mem_size} --max-stack-size={input.stack_size} --stdout=stdout.txt --stderr=stderr.txt --pin-tool-args='{pintool_args}' -- {exe.path} {input.args}"

def generate_gem5_command(dir: str, outdir: str, exe: Benchmark.Executable, input: Benchmark.Input,
                          file_dep: List[str], targets: List[str], gem5_exe: str, gem5_script: str,
                          task_dep: List[str] = [], **kwargs):
    rundir = f"{dir}/run"
    targets.extend([f"{outdir}/{name}.txt" for name in ["simout", "simerr", "stdout", "stderr", "time"]])
    file_dep.extend([gem5_exe, gem5_script, exe.path])
    yield {
        "basename": outdir,
        "actions": [
            f"rm -rf {outdir}",
            f"mkdir -p {dir} {outdir}",
            f"[ -d {rundir} ] || ln -sf {os.path.abspath(exe.wd)} {rundir}",
            build_gem5_command(rundir = rundir, outdir = outdir, exe = exe, input = input,
                               gem5_exe = gem5_exe, gem5_script = gem5_script, **kwargs),
        ],
        "file_dep": file_dep,
        "task_dep": task_dep,
        "targets": targets,
    }


# TODO: Remove this.
def generate_gem5_pin_command(dir: str, outdir: str, exe: Benchmark.Executable,
                              input: Benchmark.Input,
                              file_dep: List[str],
                              targets: List[str],
                              task_dep: List[str] = [],
                              pintool_args: str = None,
                              gem5_script_args: List[str] = "",
                              gem5_script: str = gem5_pin):
    if pintool_args:
        gem5_script_args += f" --pin-tool-args='{pintool_args}'"
    yield generate_gem5_command(dir = dir, outdir = outdir, exe = exe, input = input, file_dep = file_dep, task_dep = task_dep,
                                targets = targets, gem5_exe = gem5_exe, gem5_script = gem5_script,
                                gem5_script_args = gem5_script_args)

def generate_pin_test(dir: str, exe: Benchmark.Executable, input: Benchmark.Input, task_dep: List[str]):
    yield generate_gem5_pin_command(
        dir = dir,
        outdir = f"{dir}/test",
        exe = exe,
        input = input,
        pintool_args = "",
        file_dep = [],
        task_dep = task_dep,
        targets = [f"{dir}/test"],
    )
    
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

def generate_bbhist(dir: str, exe: Benchmark.Executable, input: Benchmark.Input, task_dep: List[str]):
    bbhist_base = f"{dir}/bbhist"
    bbhist_dir = bbhist_base
    bbhist_txt = f"{dir}/bbhist.txt"
    yield generate_gem5_pin_command(
        dir = dir,
        outdir = bbhist_dir,
        exe = exe,
        input = input,
        # pintool_args = f"-bbhist {bbhist_txt}",
        gem5_script = f"{gem5}/configs/pin-bbhist.py",
        gem5_script_args = f"--bbhist={bbhist_txt}",
        file_dep = [],
        task_dep = task_dep,
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

# TODO: Rename progmark -> waypoints
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
    parent_dir = os.path.dirname(dir)
    bbv_base = f"{parent_dir}/bbv"
    bbv_txt = f"{parent_dir}/bbv.txt"
    aux_txt = f"{parent_dir}/bbv.info.txt"
    bbv_dir = bbv_base
    progmark_txt = f"{dir}/progmark.txt"
    yield generate_gem5_pin_command(
        dir = dir,
        outdir = bbv_dir,
        exe = exe,
        input = input,
        gem5_script = f"{gem5}/configs/pin-bbv.py",
        gem5_script_args = f"--bbv={bbv_txt} --bbvinfo={aux_txt} --warmup={warmup_interval_length} --interval={simpoint_interval_length} --waypoints={progmark_txt}",
        file_dep = [progmark_txt],
        targets = [bbv_txt, aux_txt],
    )

def generate_simpoint_intervals(dir: str):
    simpoint_base = f"{dir}/intervals"
    simpoint_intervals = f"{dir}/intervals.txt"
    simpoint_weights = f"{dir}/weights.txt"
    bbv = f"{dir}/bbv.txt"
    yield {
        "basename": simpoint_base,
        "actions": [f"{simpoint_exe} -loadFVFile {bbv} -maxK {simpoint_max} -saveSimpoints {simpoint_intervals} -saveSimpointWeights {simpoint_weights} -fixedLength off"],
        "file_dep": [simpoint_exe, bbv],
        "targets": [simpoint_intervals, simpoint_weights],
    }

# weights: the weights.txt file produced by SimPoint utility.
# intervals: the intervals.txt file produced by SimPoint utility.
# waypoints: the bbv.info.txt file produced by pin-bbv.py.
def compute_simpoint_json(out: str, weights: str, waypoints: str, intervals: str):
    infos = []
    with open(weights) as f_weights, \
         open(intervals) as f_intervals:
        for weight_line, interval_line in zip(f_weights, f_intervals):
            weight, idx1 = weight_line.split()
            interval, idx2 = interval_line.split()
            assert idx1 == idx2
            weight = float(weight)
            interval = int(interval)
            infos.append({
                "interval": interval,
                "weight": weight,
            })

    # Add waypoints.
    with open(waypoints) as f:
        waypoints_lines = list(map(str.strip, f))
    for info in infos:
        tokens = waypoints_lines[info["interval"]].split()
        assert len(tokens) == 3
        info["waypoints"] = tokens

    # Number the simpoints.
    infos.sort(key = lambda d: d["interval"])
    for i, info in enumerate(infos):
        info["name"] = str(i)

    # Dump output file.
    with open(out, "wt") as f:
        json.dump(infos, f, indent = 4)


def generate_simpoint_json(dir: str):
    simpoint_base = f"{dir}/simpoint"
    out_simpoint_json = f"{simpoint_base}.json"
    in_weights = f"{dir}/weights.txt"
    in_waypoints = f"{dir}/bbv.info.txt"
    in_intervals = f"{dir}/intervals.txt"
    yield {
        "basename": simpoint_base,
        "actions": [(compute_simpoint_json, [], {
            "out": out_simpoint_json,
            "weights": in_weights,
            "waypoints": in_waypoints,
            "intervals": in_intervals,
        })],
        "file_dep": [in_weights, in_waypoints, in_intervals],
        "targets": [out_simpoint_json],
    }



# TODO: remove command_prefix
def generate_take_checkpoints(dir: str, exe: Benchmark.Executable, input: Benchmark.Input):
    parent = os.path.dirname(dir)
    out_base = f"{dir}/cpt"
    out_dir = out_base
    simpoints_json = f"{parent}/simpoint.json"
    waypoints = f"{dir}/progmark.txt"

    yield generate_gem5_command(
        dir = dir,
        outdir = out_dir,
        exe = exe,
        input = input,
        gem5_exe = gem5_exe,
        gem5_script = f"{gem5}/configs/pin-cpt.py",
        gem5_script_args = f"--simpoints-json={simpoints_json} --waypoints={waypoints}",
        file_dep = [simpoints_json, waypoints],
        targets = [],
    )

def generate_simpoints(benches):
    for bench in benches:
        b_dir = bench.name
        input_to_target = dict()
        for input in bench.inputs:
            bi_dir = os.path.join(b_dir, input.name)
            for exe in bench.exes:
                bix_dir = os.path.join(bi_dir, exe.name)
                task_dep = [f"{b_dir}/{input_dep.name}/{exe.name}/cpt" for input_dep in input.deps]
                if int(doit.get_var("pin-test", "0")) > 0:
                    yield generate_pin_test(bix_dir, exe, input, task_dep = task_dep)
                yield generate_bbhist(bix_dir, exe, input, task_dep = task_dep)
                yield generate_instlist(bix_dir)
                yield generate_srclist(bix_dir, exe)
                yield generate_srclocs(bix_dir)
                yield generate_lehist(bix_dir)

            # Among all exes, generate the shared set of locedges with identical hit counts.
            yield generate_shlocedges(bi_dir, bench.exes)

            # Collect source location edge vectors for leader.
            for exe in bench.exes:
                yield generate_progmark(os.path.join(bi_dir, exe.name))

            main_exe = bench.exes[0]
            bixmain_dir = os.path.join(bi_dir, main_exe.name)
            yield generate_bbv(bixmain_dir, main_exe, input)
            yield generate_simpoint_intervals(bi_dir)
            yield generate_simpoint_json(bi_dir)

            for exe in bench.exes:
                bix_dir = os.path.join(bi_dir, exe.name)
                yield generate_take_checkpoints(bix_dir, exe, input)

def task_simpoints():
    yield generate_simpoints(benches)

def compute_single_results(stats: dict, inpath: str, outpath: str):
    results = {}
    with open(inpath) as f:
        for line in f:
            tokens = line.split()
            if len(tokens) > 0 and tokens[0] in stats:
                results[stats[tokens[0]]] = float(tokens[1])
    with open(outpath, "wt") as f:
        json.dump(results, f, indent = 4)

def compute_total_results(inpaths: List[str], outpath: str, simpoints: str):
    with open(simpoints) as f:
        simpoints = json.load(f)
    simpoints = dict([(simpoint["name"], simpoint) for simpoint in simpoints])
    total_weight = 0
    total_results = dict()
    for inpath in inpaths:
        simpoint = simpoints[os.path.basename(os.path.dirname(inpath))]
        weight = simpoint["weight"]
        with open(inpath) as f:
            results = json.load(f)
        for key in results:
            results[key] *= weight
        if total_results:
            assert total_results.keys() == results.keys()
            for key, value in results.items():
                total_results[key] += value
        else:
            total_results = results
        total_weight += weight
    tolerance = 0.001
    assert 1 - tolerance <= total_weight and total_weight <= 1 + tolerance
    with open(outpath, "wt") as f:
        json.dump(total_results, f, indent = 4)
        
        
def generate_experiments(benches, exps):
    for bench in benches:
        for input in bench.inputs:
            for exp in exps:
                # Find matching binary.
                exes = [exe for exe in bench.exes if exe.name == exp.sw]
                assert len(exes) == 1
                exe = exes[0]

                bi_dir = os.path.join(bench.name, input.name)
                root_dir = os.path.join(bench.name, input.name, exe.name) # TODO: Rename to bix_dir.
                exp_dir = os.path.join(root_dir, exp.name)
                cpt_dir = os.path.join(root_dir, "cpt")
                cpt_stamp = f"{cpt_dir}/simout.txt"
                rundir = f"{root_dir}/run"
                intervals = f"{bench.name}/{input.name}/intervals"

                # FIXME: Should instead look at simpoints.json, not intervals.txt.
                @doit.create_after(executed = intervals, target_regex = f"{exp_dir}/([0-9]+/(stats.txt|results.json)|results.json)")
                def _task_resume_from_simpoints(bench=bench,
                                                input=input,
                                                exp=exp,
                                                exe=exe,
                                                root_dir=root_dir,
                                                exp_dir=exp_dir,
                                                cpt_dir=cpt_dir,
                                                cpt_stamp=cpt_stamp,
                                                rundir=rundir,
                                                intervals=intervals,
                                                bi_dir=bi_dir):
                    intervals_txt = intervals + ".txt"

                    # Compute number of simpoints.
                    if not os.path.exists(intervals_txt):
                        return
                    with open(intervals_txt) as f:
                        num_simpoints = len(list(f))

                    # For each simpoint.
                    results_jsons = []
                    stats_dict = {
                        "system.switch_cpus.commitStats0.ipc": "ipc",
                        "system.switch_cpus.numCycles": "cycles",
                        "system.switch_cpus.thread_0.numInsts": "insts",
                    }
                    for i in range(num_simpoints):
                        dir = f"{exp_dir}/{i}"
                        stats = f"{dir}/stats.txt"

                        # Resume from simpoint.
                        yield {
                            "basename": dir,
                            "actions": [
                                f"{exp.gem5_exe} -re --silent-redirect -d {dir} " + \
                                f"{exp.gem5_script} --chdir={rundir} --mem-size={input.mem_size} " + \
                                f"--errout=stderr.txt --output=stdout.txt {exp.gem5_script_args} --cmd={exe.path} " + \
                                f"--options='{input.args}' --cpu-type=X86O3CPU --checkpoint-dir={cpt_dir} " + \
                                f"--restore-simpoint-checkpoint --checkpoint-restore={i + 1} " + \
                                f"--caches"
                            ],
                            "file_dep": [exp.gem5_exe, exp.gem5_script, cpt_stamp],
                            "targets": [stats],
                        }

                        # Collect stats into results.json
                        results = f"{dir}/results"
                        results_json = f"{results}.json"
                        yield {
                            "basename": results,
                            "actions": [(compute_single_results, [], {
                                "stats": stats_dict,
                                "inpath": stats,
                                "outpath": f"{dir}/results.json"})],
                            "file_dep": [stats],
                            "targets": [results_json],
                        }
                        results_jsons.append(results_json)

                    # Collect weighted stats into main stats.
                    total_results = f"{exp_dir}/results"
                    total_results_json = f"{total_results}.json"
                    simpoints_json = f"{bi_dir}/simpoint.json"
                    yield {
                        "basename": total_results,
                        "actions": [(compute_total_results, [], {
                            "inpaths": results_jsons,
                            "outpath": total_results_json,
                            "simpoints": simpoints_json,
                        })],
                        "file_dep": [simpoints_json] + results_jsons,
                        "targets": [total_results_json],
                    }


                name = f"task_experiments_{bench.name}_{input.name}_{exp.name}"
                _task_resume_from_simpoints.__name__ = name
                globals()[name] = _task_resume_from_simpoints

if int(doit.get_var("exps", "1")) > 0:
    generate_experiments(benches, exps)


# REVERTME: Re-enable these.
if False:

    # Define the compilers.
    # TODO: Split compiler up into Executables + Config.
    cc_base = llvm_compiler(
        name = "base",
        prefix = "/home/nmosier/llsct2/llvm/base-17/build",
        cflags = "-O3 -mno-avx -g",
        ldflags = "-static -fuse-ld=lld -Wl,--allow-multiple-definition",
        cmake_build_type = "Release",
    )
    cc_slh = cc_base.dup().set_name("slh").add_cflags("-mllvm --x86-speculative-load-hardening")

    cc_ptex = llvm_compiler(
        name = "ptex",
        prefix = "/home/nmosier/llsct2/llvm/ptex-17/build",
        cflags = cc_base.cflags,
        ldflags = cc_base.ldflags,
        cmake_build_type = cc_base.cmake_build_type,
    )
    cc_ptex_nst = cc_ptex.dup()
    # TODO: Enable optimizations.
    cc_ptex_nst.cflags += " -mllvm -x86-ptex=nst"

    compilers = [cc_base, cc_slh, cc_ptex_nst]

    def generate_lib(compilers: List[Compiler], src: str, bin: str, build_lib):
        for cc in compilers:
            yield build_lib(
                src = src,
                bin = f"{bin}/{cc.name}",
                compiler = cc,
            )

    def task_libs():
        for cc in compilers:
            cc = cc.dup()
            cc.ldflags = ""
            for lib, build_lib in [("libc", build_libc), ("libcxx", build_libcxx)]:
                yield build_lib(
                    src = "/home/nmosier/llsct2/llvm/base-17",
                    bin = f"{lib}/{cc.name}",
                    compiler = cc,
                )
