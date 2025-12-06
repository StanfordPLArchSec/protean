#!/usr/bin/env python3

import argparse
import re
import sys
from util.util import (
    json_cycles,
    stats_seconds,
    set_args,
    comma_list,
    run_if_requested,
    add_common_arguments,
    rgbtohex
)
from contextlib import chdir

parser = argparse.ArgumentParser("Generate Figure 7 from the paper.")

class SuiteBase:
    def __init__(self, name, benches):
        self.name = name
        self.benches = benches
        for bench in benches:
            bench.suite = self

class SPECSuite(SuiteBase):
    def _target(self, bench, bin, hwconf):
        return (
            f"{bench.target_name}/exp/0/main/{bin}/"
            f"{hwconf}.pcore/results.json"
        )

    def _perf(self, target):
        return json_cycles(target)

class PARSECSuite(SuiteBase):
    def _target(self, bench, bin, hwconf):
        return (
            f"parsec/pkgs/{bench.target_name}/run/exp/{bin}/"
            f"{hwconf}/stamp.txt"
        )

    def _perf(self, target):
        return stats_seconds(target)

class Bench:
    def __init__(self, name, target):
        self.name = name
        self.target_name = target
        self.results = None

    def target(self, bin, hwconf):
        return self.suite._target(self, bin, hwconf)

    def perf(self, target):
        return self.suite._perf(target)

suites = []
cpu2017_benches = []
for bench in ["600.perlbench_s",
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
              "657.xz_s"]:
    m = re.fullmatch(r"6\d\d\.(.*)_s", bench)
    assert m
    cpu2017_benches.append(Bench(
        name = m.group(1) + ".s",
        target = bench,
    ))
suites.append(SPECSuite(
    name = "spec2017",
    benches = cpu2017_benches,
))


parsec_benches = []
for bench in ["apps/blackscholes",
              "apps/ferret",
              "apps/fluidanimate",
              "apps/swaptions",
              "kernels/canneal",
              "kernels/dedup"]:
    parsec_benches.append(Bench(
        name = bench.split("/")[-1] + ".p",
        target = bench,
    ))
suites.append(PARSECSuite(
    name = "parsec",
    benches = parsec_benches,
))

suite_list = [suite.name for suite in suites]
full_bench_list = [bench.name for suite in suites for bench in suite.benches]

parser.add_argument(
    "--suite", "-s",
    choices=suite_list,
    action="append",
    default=[],
    help="Benchmark suites to run and include in the figure.",
)

parser.add_argument(
    "--bench", "-b",
    choices=full_bench_list,
    action="append",
    default=[],
    help="Individual benchmarks to run and include in the figure.",
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help="Run all benchmarks.",
)

add_common_arguments(parser)

args = parser.parse_args()
set_args(args)

# Expand suites into bench lists.
bench_list = []
for suite in suites:
    for bench in suite.benches:
        if suite.name in args.suite or \
           bench.name in args.bench or \
           args.all:
            bench_list.append(bench)
if len(bench_list) == 0:
    print("WARN: benchmark list is empty! Nothing to run.",
          file=sys.stderr)
    exit(1)
            
# Collect targets to run.
targets = []
confs = [
    ("base", "unsafe"),
    ("base", "stt.atret"),
    ("base", "prottrack.atret"),
    ("base", "spt.atret"),
    ("ct", "prottrack.atret"),
]
for bench in bench_list:
    for bin, hwconf in confs:
        targets.append(bench.target(bin, hwconf))

with chdir("bench"):
    # Run results.
    run_if_requested(args, [*args.snakemake_command, *targets])

    # Read in results.
    for bench in bench_list:
        unsafe = bench.perf(bench.target("base", "unsafe"))
        l = []
        for bin, hwconf in confs[1:]:
            defense = bench.perf(bench.target(bin, hwconf))
            l.append(defense / unsafe)
        bench.results = l

########################
# Generate the figure. #
########################
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.patheffects as pe
import matplotlib as mpl

mpl.rcParams["text.usetex"] = True
mpl.rcParams["font.family"] = "serif"
mpl.rcParams["text.latex.preamble"] = r"""
\usepackage{ulem}
\usepackage{newtxtext}
\usepackage{newtxmath}
"""
mpl.rcParams.update({
    "font.size": 8,
    "axes.labelsize": 8,
    "axes.titlesize": 8,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "legend.fontsize": 8,
})

all_benches = [bench for suite in suites for bench in suite.benches]
n = len(all_benches)

# Prepopulate the results table with 1s.
table = [np.ones(n) for i in range(4)]

for i, bench in enumerate(all_benches):
    # If the benchmark results are empty, then skip.
    if bench.results is None:
        continue
    for j, result in enumerate(bench.results):
        table[j][i] = result
stt, prot_arch, spt, prot_ct = table

x = np.arange(n)
width = 0.18

fig, ax = plt.subplots(figsize=(7.5, 2))

# Grouped bars.
def make_style(color, hatch):
    return dict(
        color = rgbtohex(*color),
        edgecolor = "black",
        hatch = hatch,
    )
stt_style = make_style((255, 255, 84), "///")
prot_arch_style = make_style((100, 175, 220), "///")
spt_style = make_style((218, 120, 66), "---")
prot_ct_style = make_style((79, 173, 91), "---")
    
b1 = ax.bar(x - 1.5 * width, stt, width, label="STT", **stt_style)
b2 = ax.bar(x - 0.5 * width, prot_arch, width,
            label=r"\textbf{Protean-Track-ARCH}",
            **prot_arch_style)
b3 = ax.bar(x + 0.5 * width, spt, width, label="SPT", **spt_style)
b4 = ax.bar(x + 1.5 * width, prot_ct, width,
            label=r"\textbf{Protean-Track-CT}",
            **prot_ct_style)

# Y-axis: 1 to 2 in 0.2 increments (matches the tick labels in the PDF)
ax.set_ylim(1.0, 2.0)
ax.set_yticks(np.arange(1.0, 2.01, 0.2))

# X-axis labels: benchmarks
stylized_bench_list = []
for bench in all_benches:
    if bench.results:
        s = bench.name
    else:
        s = r"\sout{" + bench.name + r"}"
    stylized_bench_list.append(s)
        
ax.set_xticks(x)
ax.set_xticklabels(
    stylized_bench_list,
    rotation=20,
    ha="right",
)

# Optional: axis labels (if your original figure has them)
# ax.set_ylabel("Normalized execution time")
# ax.set_xlabel("Benchmark")  # Only if the original has it; otherwise drop

# Legend: 4 entries, usually above or below the plot in a single row
ax.legend(
    loc="upper right",
    bbox_to_anchor=(0.98, 0.98),
    ncol=4,
    frameon=True,
    framealpha=0.6,
    edgecolor="black",
)

# Tight layout so labels don't get cut off
fig.tight_layout()

# Save as PDF to drop back into LaTeX
plt.savefig("figure-7.pdf", bbox_inches="tight")
