#!/usr/bin/env python3

import argparse
from util.util import (
    make_name,
    add_common_arguments,
    set_args,
    format_and_render_tex,
)
from util.class_specific import (
    suites,
    get_results_for_benchmarks,
)
from collections import defaultdict

strname, filename = make_name()

parser = argparse.ArgumentParser(
    f"Generate {strname}."
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help="Generate all results.",
)

program_class_choices = ["arch", "cts", "ct", "unr", "multiple"]

parser.add_argument(
    "--program-class", "-c",
    choices=program_class_choices,
    action="append",
    default=[],
    help=(
        f"Program classes (columns in {strname}) to "
        "generate results for."
    ),
)

add_common_arguments(parser)

args = parser.parse_args()
set_args(args)

# Populate the program class choices.
if args.all:
    args.program_class = program_class_choices

# Map program class choices to suites.
program_classes = {}
for program_class, suite in zip(["arch", "cts", "ct", "unr", "multiple"],
                                suites):
    if program_class in args.program_class:
        program_classes[program_class] = suite

# Collect list of benchmarks to run.
bench_list = [bench \
              for suite in program_classes.values() \
              for bench in suite.benches]

get_results_for_benchmarks(bench_list, args)    

subs = defaultdict(lambda: "-")
for program_class, suite in program_classes.items():
    for defense_name, normalized_runtime in zip(
            [suite.baseline.removesuffix(".atret"), "prottrack", "protdelay"],
            suite.geomean()):
        class_name = "multi" if program_class == "multiple" else program_class
        key = f"{defense_name}_{class_name}"
        value = f"{(normalized_runtime - 1) * 100:.0f}"
        subs[key] = value

format_and_render_tex(filename, subs)
print(f"DONE: Find result at {filename}.pdf")
