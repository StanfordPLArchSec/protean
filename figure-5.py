#!/usr/bin/env python3

import argparse
from util.util import (
    make_name,
    add_common_arguments,
    set_args,
    run_if_requested,
    ResultPath,
    run,
    geomean,
    arithmean,
)
from contextlib import chdir
import shutil
from pathlib import Path
import sys
import json

strname, filename = make_name()

parser = argparse.ArgumentParser(
    f"Generate {strname} (the sensitivity study)."
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help="Test all the predictor sizes tested in the paper.",
)

# Returns the actual hwconf extension.
def parse_predictor_size(s):
    if s == "none":
        return "unprot"
    elif s == "infinite":
        return "pred0"
    else:
        n = int(s, 0)
        return f"pred{n}"

parser.add_argument(
    "--predictor-size", "-s",
    action="append",
    default=[],
    type=parse_predictor_size,
    help=(
        "Specific predictor sizes to test. Options:\n"
        "    none:     no predictor\n"
        "    infinite: infinite predictor size\n"
        "    <n>:      finite predictor size (must be power of 2)\n"
    ),
)

add_common_arguments(parser)
args = parser.parse_args()
set_args(args)

# Fixup args.
if args.all:
    args.predictor_size = [
        "unprot",
        *map(lambda n: f"pred{2**n}", range(0, 13)),
        "pred0",
    ]

if len(args.predictor_size) == 0:
    print("ERROR: No predictor sizes requested!", file=sys.stderr)
    exit(1)

def get_confs(predsize):
    confs = ["base/unsafe"]
    for bin in ["base", "ct"]:
        confs.append(f"{bin}/prottrack.{predsize}.atret")
    return confs

def get_json_path(conf):
    return f"_cpu2017.int/exp/0/main/{conf}.pcore/results.json"

def parse_json(conf):
    return json.loads(ResultPath(get_json_path(conf)).read_text())

def get_cycles(conf):
    return parse_json(conf)["stats"]["cycles"]["geomean"]

def get_mispredicts(conf):
    return parse_json(conf)["stats"]["access-misp-rate"]["arithmean"]

# Generate targets to run.
targets = [get_json_path(conf) \
           for predsize in args.predictor_size \
           for conf in get_confs(predsize)]


perf_results = []
misp_results = []
with chdir("bench"):
    target = "figures/predictor.pdf"
    run_if_requested(args, [*args.snakemake_command, target])

    # Gather results.
    for predsize in args.predictor_size:
        unsafe_cycles, prottrack_arch_cycles, prottrack_ct_cycles = \
            map(get_cycles, get_confs(predsize))
        prottrack_arch_misp, prottrack_ct_misp = \
            map(get_mispredicts, get_confs(predsize)[1:])
        prottrack_cycles = geomean([prottrack_arch_cycles,
                                    prottrack_ct_cycles])
        prottrack_perf = prottrack_cycles / unsafe_cycles
        prottrack_misp = arithmean([prottrack_arch_misp,
                                    prottrack_ct_misp])
        perf_results.append(prottrack_perf)
        misp_results.append(prottrack_misp)

# Save the results in a CSV.
def format_predsize(s):
    if s == "unprot":
        return "none"
    elif s == "pred0":
        return "infinite"
    else:
        return s.removeprefix("pred")

perf_csv = f"{filename}.perf.csv"
with Path(perf_csv).open("wt") as f:
    print(",".join(map(format_predsize, args.predictor_size)), file=f)
    print(",".join(map(str, perf_results)), file=f)
misp_csv = f"{filename}.misp.csv"
with Path(misp_csv).open("wt") as f:
    print(",".join(map(format_predsize, args.predictor_size)), file=f)
    print(",".join(map(str, misp_results)), file=f)

# Run the figure generation script.
run(
    [
        "bench/figures/predictor.py",
        "--runtime-csv", perf_csv,
        "--rate-csv", misp_csv,
        "-o", f"{filename}.pdf",
        "--no-crop",
     ]
)

print(f"DONE: See {filename}.pdf.")
