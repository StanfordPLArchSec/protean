#!/usr/bin/env python3

import argparse
from util.util import (
    add_common_arguments,
    run_if_requested,
    set_args,
    comma_list,
    format_and_render_tex,
    ResultPath,
    geomean,
)
from contextlib import chdir
import sys
import json
import re

parser = argparse.ArgumentParser(
    "Script for running class-representative "
    "benchmarks and generating Table IV."
)

class SuiteBase:
    def __init__(self, name, benches, baseline, protcc):
        self.name = name
        self.benches = benches
        self.baseline = baseline
        self.protcc = protcc
        for bench in self.benches:
            bench.suite = self

class Suite(SuiteBase):
    def __init__(self, name, benches, baseline, protcc, group):
        super().__init__(name, benches, baseline, protcc)
        self.group = group

    def _target(self, bench, bin, hwconf):
        return (
            f"{bench.target_name}/exp/0/{self.group}/{bin}/"
            f"{hwconf}.pcore/results.json"
        )

    def perf(self, target):
        j = json.loads(ResultPath(target).read_text())
        return j["stats"]["cycles"]

class WebserverSuite(SuiteBase):
    def _target(self, bench, bin, hwconf):
        hwconf_l = hwconf.split(".")
        hwconf_l.insert(1, "se")
        hwconf = ".".join(hwconf_l)
        return (
            f"webserv/exp/{bench.target_name}/{bin}/{hwconf}/stamp.txt"
        )

    def perf(self, target):
        l = []
        with ResultPath(target).with_name("stats.txt").open() as f:
            for line in f:
                if m := re.match(r"simSeconds\s+([0-9.]+)", line):
                    l.append(float(m.group(1)))
        assert len(l) >= 1
        return l[-1]

class Bench:
    def __init__(self, name, target):
        self.name = name
        self.target_name = target
        self.results = [None] * 3

    def _target(self, bin, hwconf):
        return self.suite._target(self, bin, hwconf)

    def baseline(self):
        return self.suite.baseline

    def protcc(self):
        return self.suite.protcc

    def target_unsafe(self):
        return self._target("base", "unsafe")

    def target_baseline(self):
        return self._target("base", self.baseline())

    def target_prottrack(self):
        return self._target(self.protcc(), "prottrack.atret")

    def target_protdelay(self):
        return self._target(self.protcc(), "protdelay.atret")

    def targets(self):
        return [
            self.target_unsafe(),
            self.target_baseline(),
            self.target_prottrack(),
            self.target_protdelay(),
        ]

    def perf(self, target):
        return self.suite.perf(target)

    def perf_unsafe(self):
        return self.perf(self.target_unsafe())

    def perf_baseline(self):
        return self.perf(self.target_baseline())

    def perf_prottrack(self):
        return self.perf(self.target_prottrack())

    def perf_protdelay(self):
        return self.perf(self.target_protdelay())
        
suites = [
    Suite(
        name = "arch-wasm",
        benches = [
            Bench("bzip2", "wasm.401.bzip2"),
            Bench("mcf", "wasm.429.mcf"),
            Bench("milc", "wasm.433.milc"),
            Bench("namd", "wasm.444.namd"),
            Bench("libquantum", "wasm.462.libquantum"),
            Bench("lbm", "wasm.470.lbm"),
        ],
        baseline = "stt.atret",
        protcc = "base",
        group = "base",
    ),
    Suite(
        name = "cts-crypto",
        benches = [
            Bench("hacl.chacha20", "ctsbench.hacl.chacha20"),
            Bench("hacl.curve25519", "ctsbench.hacl.curve25519"),
            Bench("hacl.poly1305", "ctsbench.hacl.poly1305"),
            Bench("sodium.salsa20", "ctsbench.libsodium.salsa20"),
            Bench("sodium.sha256", "ctsbench.libsodium.sha256"),
            Bench("ossl.chacha20", "ctsbench.openssl.chacha20"),
            Bench("ossl.curve25519", "ctsbench.openssl.curve25519"),
            Bench("ossl.sha256", "ctsbench.openssl.sha256"),
        ],
        baseline = "spt.atret",
        protcc = "cts",
        group = "ctsbench",
    ),
    Suite(
        name = "ct-crypto",
        benches = [
            Bench("bearssl", "bearssl"),
            Bench("ctaes", "ctaes"),
            Bench("djbsort", "djbsort"),
        ],
        baseline = "spt.atret",
        protcc = "ct",
        group = "ctbench",
    ),
    Suite(
        name = "unr-crypto",
        benches = [
            Bench("ossl.bnexp", "nctbench.openssl.bnexp"),
            Bench("ossl.dh", "nctbench.openssl.dh"),
            Bench("ossl.ecadd", "nctbench.openssl.ecadd"),
        ],
        baseline = "sptsb.atret",
        protcc = "nct",
        group = "nctbench",
    ),
    WebserverSuite(
        name = "webserver",
        benches = [
            Bench(f"nginx.c{c}r{r}", f"c{c}r{r}")
            for c, r in [(1, 1), (2, 2), (1, 4), (4, 1), (4, 4)]
        ],
        baseline = "sptsb.atret",
        protcc = "nct.ossl-annot",
    )
]

suite_list = [suite.name for suite in suites]
bench_list = [bench.name for suite in suites for bench in suite.benches]

parser.add_argument(
    "--suite", "-s",
    choices=suite_list,
    action="extend",
    type=comma_list,
    help=(
        "Class-representative benchmark suites to run "
        "and generate results for."
    ),
)

parser.add_argument(
    "--bench", "-b",
    choices=bench_list,
    action="extend",
    type=comma_list,
    help=(
        "Individual class-representative benchmarks to run "
        "and generate results for."
    ),
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help="Run all class-representative benchmarks.",
)

add_common_arguments(parser)

args = parser.parse_args()
set_args(args)

# Set default arguments.
if args.suite is None and args.bench is None:
    args.bench = [
        "hacl.poly1305",
        "bearssl",
        "ossl.bnexp",
        "nginx.c1r1",
    ]
    args.suite = []

if args.all:
    args.suite = suite_list

# Collect list of suites and benchmarks to run.
suite_list = []
for suite_name in args.suite:
    for suite in suites:
        if suite.name == suite_name:
            suite_list.append(suite)

bench_list = []
for suite in suite_list:
    bench_list.extend(suite.benches)
for bench_name in args.bench:
    for suite in suites:
        for bench in suite.benches:
            if bench.name == bench_name:
                bench_list.append(bench)

if len(bench_list) == 0:
    print("WARN: benchmark list is empty! Nothing to run.",
          file=sys.stderr)
    exit(1)

# Construct a list of targets.
targets = []
for bench in bench_list:
    targets.extend(bench.targets())

with chdir("bench"):
    # Run results.
    run_if_requested(args, [*args.snakemake_command, *targets])

    # Read in results.
    for bench in bench_list:
        unsafe = bench.perf_unsafe()
        baseline = bench.perf_baseline()
        prottrack = bench.perf_prottrack()
        protdelay = bench.perf_protdelay()
        assert unsafe != 0
        assert baseline != 0
        assert prottrack != 0
        assert protdelay != 0
        bench.results = [
            baseline / unsafe, prottrack / unsafe, protdelay / unsafe,
        ]

# Generate subtables.
subs = {}
for suite in suites:
    lines = []
    full_suite = True
    for bench in suite.benches:
        # If we have the benchmarks, put in the results.
        tokens = [bench.name]
        for result in bench.results:
            if result is not None:
                tokens.append(f"{result : .3f}")
            else:
                tokens.append("-")
                full_suite = False
        line = " & ".join(tokens)
        line += r"\\\hline"
        lines.append(line)

    # If we got all the results for the benchmark, then compute the geomeans for each.
    geomean_line = ["geomean"]
    if full_suite:
        result_lists = [[], [], []]
        for bench in suite.benches:
            for result, result_list in zip(bench.results, result_lists):
                result_list.append(result)
        geomean_line.extend(
            map(lambda x: f"{x : .3f}", map(geomean, result_lists))
        )
    else:
        geomean_line.extend(["-"] * 3)
    lines.append(" & ".join(geomean_line) + r"\\\Xhline{1pt}")

    subs[suite.name] = "\n".join(lines)

# Format table.
format_and_render_tex("table-iv", subs)
print("DONE: Find result at table-iv.pdf")
