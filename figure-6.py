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
    rgbtohex,
    make_name,
)
from contextlib import chdir

strname, filename = make_name()

parser = argparse.ArgumentParser(f"Generate {strname} from the paper.")

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

# Space out each benchmark group a bit more.
group_spacing = 1.25
x = np.arange(n) * group_spacing
width = 0.18

fig, ax = plt.subplots(figsize=(7.25, 1.75))

# Grouped bars.
def make_style(color, hatch):
    return dict(
        color = rgbtohex(*color),
        edgecolor = "black",
        linewidth = 0.6,
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
    rotation=25,
    ha="right",
)
# Small ticks between bar groups to mark boundaries.
if n > 1:
    boundary_x = (x[:-1] + x[1:]) / 2
    y0, y1 = ax.get_ylim()
    tick_h = 0.03 * (y1 - y0)
    ax.vlines(
        boundary_x,
        y0,
        y0 + tick_h,
        colors="black",
        linewidth=0.6,
        clip_on=False,
    )
    # Add a little extra x margin so the first/last groups aren't cramped.
    pad = 2.0 * width
    ax.set_xlim(x[0] - pad, x[-1] + pad)

# Optional: axis labels (if your original figure has them)
# ax.set_ylabel("Normalized execution time")
# ax.set_xlabel("Benchmark")  # Only if the original has it; otherwise drop

# Legend: 4 entries, usually above or below the plot in a single row
ax.legend(
    loc="upper right",
    bbox_to_anchor=(0.99, 0.94),
    ncol=4,
    frameon=True,
    framealpha=0.8,
    edgecolor="black",
)

ymin, ymax = ax.get_ylim()

def label_clipped_bars(bar_container, ymax, side="center",
                       fmt="{:.2f}", dy=0.02, dx=0.1):
    for bar in bar_container:
        h = bar.get_height()
        if h > ymax:
            # base x at bar center
            x = bar.get_x() + bar.get_width() / 2

            if side == "left":
                x = bar.get_x() - dx
                ha = "right"
                rot = 90
            elif side == "right":
                x = bar.get_x() + bar.get_width() + dx
                ha = "left"
                rot = 90
            else:
                ha = "center"
                rot = 90

            # 🔑 grab the bar's actual color
            color = bar.get_facecolor()

            ax.text(
                x, ymax - dy,
                fmt.format(h),
                ha=ha,
                va="top",
                color=color,
                clip_on=False,
                # path_effects=[
                #     pe.Stroke(linewidth=3, foreground="black"),
                #     pe.Normal(),
                # ],
            )

label_clipped_bars(b1, ymax, side="left")    # STT → left
label_clipped_bars(b2, ymax, side="center") # Prot-ARCH → center (optional)
label_clipped_bars(b3, ymax, side="right")  # SPT → right
label_clipped_bars(b4, ymax, side="center") # Prot-CT → center (optional)

ax.tick_params(axis="x", pad=0.5)

from matplotlib.ticker import MultipleLocator

ax.set_ylim(1.0, 2.0)

# Major ticks every 0.2
ax.yaxis.set_major_locator(MultipleLocator(0.2))

# Minor ticks every 0.05
ax.yaxis.set_minor_locator(MultipleLocator(0.05))

# Optional: style the minor ticks so they're shorter/thinner
ax.tick_params(axis="y", which="minor", length=2, width=0.5)
ax.tick_params(axis="y", which="major", length=4, width=0.8)

ax.grid(axis="y", which="major", linestyle="-", linewidth=0.4, alpha=0.3)

# Tight layout so labels don't get cut off
fig.tight_layout()

# Save as PDF to drop back into LaTeX
plt.savefig(f"{filename}.pdf", bbox_inches="tight")
