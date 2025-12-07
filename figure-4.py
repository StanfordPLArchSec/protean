#!/usr/bin/env python3

import argparse
from util.util import (
    make_name,
    add_common_arguments,
    set_args,
    run_if_requested,
)
from contextlib import chdir
import shutil

strname, filename = make_name()

parser = argparse.ArgumentParser(
    f"Generate {strname} (the sensitivty study)."
)

add_common_arguments(parser)
args = parser.parse_args()
set_args(args)

with chdir("bench"):
    target = "figures/predictor.pdf"
    run_if_requested(args, [*args.snakemake_command, target])

# Copy to the expected path.
shutil.copyfile("bench/figures/predictor.pdf", f"{filename}.pdf")
print(f"DONE: See {filename}.pdf.")
