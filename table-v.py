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
    make_name,
)
from contextlib import chdir
import sys
from util.class_specific import (
    suites,
    get_results_for_benchmarks,
)

strname, filename = make_name()

parser = argparse.ArgumentParser(
    "Script for running class-representative "
    f"benchmarks and generating {strname}."
)

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
        "lbm",
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

get_results_for_benchmarks(bench_list, args)

# Generate subtables.
subs = {}
for suite in suites:
    lines = []
    for bench in suite.benches:
        # If we have the benchmarks, put in the results.
        tokens = [bench.name]
        for result in bench.results:
            if result is not None:
                tokens.append(f"{result : .3f}")
            else:
                tokens.append("-")
        line = " & ".join(tokens)
        line += r"\\\hline"
        lines.append(line)

    # If we got all the results for the benchmark, then compute the geomeans for each.
    geomean_line = ["geomean"]
    if geo_list := suite.geomean():
        geomean_line.extend([f"{x:.3f}" for x in geo_list])
    else:
        geomean_line.extend(["-"] * 3)
    lines.append(" & ".join(geomean_line) + r"\\\Xhline{1pt}")

    subs[suite.name] = "\n".join(lines)

# Format table.
format_and_render_tex(filename, subs)
print(f"DONE: Find result at {filename}.pdf")
