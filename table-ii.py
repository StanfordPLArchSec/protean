#!/usr/bin/env python3

from util.util import (
    comma_list,
    add_common_arguments,
    should_regenerate,
    ResultPath,
    set_args,
    format_and_render_tex,
    make_name,
    run,
)
from contextlib import chdir
from collections import defaultdict
import json
import argparse
from pathlib import Path
import sys
import shutil

strname, filename = make_name()

def do_generate_table_i(args):
    # Fill in default arguments.
    if args.all:
        if args.size is None:
            args.size = "full"
        if args.instrumentation is None:
            args.instrumentation = instrumentations
    else:
        if args.size is None:
            args.size = "small"
        if args.instrumentation is None:
            args.instrumentation = ["rand"]
    assert args.size is not None and args.instrumentation is not None
    
    defenses = ["none", "prottrack", "protdelay"]
    adversaries = ["cache", "commit"]

    # Enumerate all directories.
    dirs = []
    obs_gen_pairs = []
    for instrumentation in args.instrumentation:
        # Get observer and generator.
        if instrumentation == "rand":
            observer = generator = "prot"
        elif instrumentation == "unr":
            generator = "unr"
            observer = "ct"
        else:
            generator = observer = instrumentation

        obs_gen_pairs.append((observer, f"llvm.{generator}"))

        # Enumerate directories.
        for defense in defenses:
            for adversary in adversaries:
                dirs.append(f"{defense}-{observer}-llvm.{generator}-{adversary}")
            

    # Compute the fuzz and triage targets.
    fuzz_targets = [f"{dir}/all" for dir in dirs]
    triage_targets = [f"{dir}/triage" for dir in dirs]

    # Compute any extra snakemake args.
    extra_snakemake_args = []
    if args.size == "small":
        extra_snakemake_args = [
            "--config", "instances=5", "programs=50",
            "inputs_cache=50", "inputs_commit=5",
        ]

    with chdir("amulet"):
        if should_regenerate(args):
            # Make sure that the output directories are empty.
            for d in dirs:
                d = Path(d)
                if d.exists():
                    if args.force:
                        print(f"WARN: overwriting results directory {d}", file=sys.stderr)
                        shutil.rmtree(str(d))
                    else:
                        print(f"ERROR: refusing to overwrite existing results directory {d}", file=sys.stderr)
                        exit(1)

            # Run the fuzzing campaign.
            run(args.snakemake_command + fuzz_targets + extra_snakemake_args)

            # Triage.
            run(args.snakemake_command + triage_targets)
            
        # Read in the results.
        subs = defaultdict(lambda: "- & - & -")
        for observer, generator in obs_gen_pairs:
            l = []
            for defense in defenses:
                order = ["true-positive", "false-positive"]
                x = [0, 0]
                for adversary in adversaries:
                    p = ResultPath(f"{defense}-{observer}-{generator}-{adversary}/triage")
                    j = json.loads(p.read_text())
                    for result in j.values():
                        x[order.index(result["result"])] += 1
                tps, fps = x
                s = r"\textbf{" + str(tps) + r"} (" + str(fps) + r")"
                l.append(s)
            subs[f"{observer}-{generator}"] = " & ".join(l)
        

    # Generate the table.
    format_and_render_tex(filename, subs)
    print(f"DONE: See {filename}.pdf")


parser = argparse.ArgumentParser(
    "Script for running results for and generating Table I in Protean's "
    "Security Evaluation"
)
parser.add_argument(
    "--size", "-s",
    choices=["small", "full"],
    help=(
        "The size of the run:\n"
        "  small: Scaled down number of instances/programs/inputs.\n"
        "  full:  Use number of instances/programs/inputs described\n"
        "         in the paper.\n"),
)
instrumentations = [
    "rand", "arch", "cts", "ct", "unr",
]
parser.add_argument(
    "--instrumentation", "-i",
    choices=instrumentations,
    action="append",
    help=(
        "Selects which row(s) of Table I to generate, selected based on instrumentation used.\n"
        "  rand:   Test against UNPROT-SEQ using ProtCC-RAND instrumentation (i.e., randomly insert PROT prefixes).\n"
        "  arch:   Test against ARCH-SEQ using ProtCC-ARCH instrumentation (i.e., do not insert PROT prefxies).\n"
        "  cts:    Test against CTS-SEQ using ProtCC-CTS instrumentation.\n"
        "  ct:     Test against CT-SEQ using ProtCC-CT instrumentation.\n"
        "  unr:    Test against CT-SEQ using ProtCC-UNR instrumentation.\n"),
)

parser.add_argument(
    "--all", "-a",
    action="store_true",
    help=(
        "Generate the full paper version of Table I.\n"
        "  WARNING: Takes a long time!\n"
    ),
)

parser.add_argument(
    "--force", "-f",
    action="store_true",
    help="Overwrite existing AMuLeT* results.",
)
add_common_arguments(parser)

args = parser.parse_args()
set_args(args)

do_generate_table_i(args)
