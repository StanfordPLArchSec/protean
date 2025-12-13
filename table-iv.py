#!/usr/bin/env python3

import argparse
from util.util import (
    set_args,
    add_common_arguments,
    geomean,
    json_cycles,
    stats_seconds,
    run_if_requested,
    format_and_render_tex,
    make_name,
)
from contextlib import chdir
from collections import defaultdict

strname, filename = make_name()

parser = argparse.ArgumentParser(
    f"Run and generate {strname} "
    "(general-purpose benchmark suite results)."
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help="Generate results for all four program classes.",
)

class ProgramClass:
    def __init__(self, name, target, baseline):
        self.name = name
        self.target = target
        self.baseline = baseline

all_program_classes = [
    ProgramClass(
        name = "arch",
        target = "base",
        baseline = "stt",
    ),
    ProgramClass(
        name = "cts",
        target = "cts",
        baseline = "spt",
    ),
    ProgramClass(
        name = "ct",
        target = "ct",
        baseline = "spt",
    ),
    ProgramClass(
        name = "unr",
        target = "nct",
        baseline = "sptsb",
    ),
]

parser.add_argument(
    "--program-class", "-c",
    choices=[program_class.name for program_class in all_program_classes],
    action="append",
    default=[],
    help="Program classes to generate results for.",
)

add_common_arguments(parser)
args = parser.parse_args()
set_args(args)

if args.all:
    args.program_class.extend(
        [program_class.name for program_class in all_program_classes]
    )

class SuiteBase:
    def __init__(self, name, benches):
        self.name = name
        self.benches = benches

    def confs(self, program_class):
        return [
            "base/unsafe",
            f"base/{program_class.baseline}.atret",
            f"{program_class.target}/protdelay.atret",
            f"{program_class.target}/prottrack.atret",
        ]

    def target(self, conf):
        l = []
        for bench in self.benches:
            l.append(self._target(bench, conf))
        return l

    def targets(self, program_class):
        l = []
        for conf in self.confs(program_class):
            l.extend(self.target(conf))
        return l

    def perf(self, conf):
        return geomean([self._perf(target) for target in self.target(conf)])

class SPECSuite(SuiteBase):
    def __init__(self, name, core_type):
        super().__init__(
            name = name,
            benches = [
            "600.perlbench_s",
            "602.gcc_s",
            "603.bwaves_s",
            "605.mcf_s",
            "607.cactuBSSN_s",
            "619.lbm_s",
            "620.omnetpp_s",
            "621.wrf_s",
            "623.xalancbmk_s",
            "625.x264_s",
            "628.pop2_s",
            "631.deepsjeng_s",
            "638.imagick_s",
            "641.leela_s",
            "644.nab_s",
            "648.exchange2_s",
            "649.fotonik3d_s",
            "657.xz_s",
        ])
        self.core_type = core_type

    def _target(self, bench, conf):
        return f"{bench}/exp/0/main/{conf}.{self.core_type}/results.json"

    def _perf(self, path):
        return json_cycles(path)
        
class PARSECSuite(SuiteBase):
    def __init__(self):
        super().__init__(
            name = "parsec",
            benches = [
            "apps/blackscholes",
            "apps/ferret",
            "apps/fluidanimate",
            "apps/swaptions",
            "kernels/canneal",
            "kernels/dedup"
        ])

    def _target(self, bench, conf):
        return f"parsec/pkgs/{bench}/run/exp/{conf}/stamp.txt"

    def _perf(self, path):
        return stats_seconds(path)

suites = [
    SPECSuite("spec_pcore", "pcore"),
    SPECSuite("spec_ecore", "ecore"),
    PARSECSuite(),
]
    
# Collect selected program classes.
program_classes = []
for program_class_name in args.program_class:
    for program_class in all_program_classes:
        if program_class.name == program_class_name:
            program_classes.append(program_class)

# Get snakemake targets.
targets = [
    suite.targets(program_class) \
    for program_class in program_classes \
    for suite in suites
]

subs = defaultdict(lambda: "- & - & -")
with chdir("bench"):
    # Generate results.
    run_if_requested(args, args.snakemake_command + targets)

    # Read in results.
    for program_class in program_classes:
        for suite in suites:
            unsafe_perf, *defense_perfs = \
                map(suite.perf, suite.confs(program_class))
            norms = []
            for defense_perf in defense_perfs:
                norms.append(defense_perf / unsafe_perf)
            v = " & ".join(map(lambda x: f"{x:.3f}", norms))
            k = f"{program_class.name}_{suite.name}"
            subs[k] = v
                
format_and_render_tex(filename, subs)
print(f"DONE: Find table in {filename}.pdf")
