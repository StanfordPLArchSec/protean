#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import subprocess
from tabulate import tabulate
import json
import re

parser = argparse.ArgumentParser()
parser.add_argument("--type", "-t", required=True, choices=["perf", "sec"])
parser.add_argument("--size", "-s", required=True, choices=["small", "medium", "full"])
parser.add_argument("--dry-run", "-n", action="store_true")
parser.add_argument("snakemake_command", nargs="*", default=["snakemake"],
                    help="Override snakemake binary/args with this.")
args = parser.parse_args()

# Go to the root directory.
root_dir = Path(__file__).resolve().parent
os.chdir(root_dir)

def do_perf_eval_small(args):
    # Run snakemake.
    class Bench:
        def __init__(self, prefix, baseline, protcc, suite_name, bench_name):
            self.prefix = prefix
            self.baseline = baseline
            self.protcc = protcc
            self.suite_name = suite_name
            self.bench_name = bench_name
        def target(self, conf):
            return f"{self.prefix}/{conf}.pcore/results.json"
        def stat(self, conf):
            path = self.target(conf)
            j = json.loads(Path(path).read_text())
            return j["stats"]["cycles"]
        def confs(self):
            return [
                "base/unsafe",
                f"base/{self.baseline}.atret",
                f"{self.protcc}/prottrack.atret",
                f"{self.protcc}/protdelay.atret",
            ]
        def normalized_stat(self, conf):
            unsafe = self.stat(self.confs()[0])
            x = self.stat(conf)
            return f"{x / unsafe : .3f}"

    class WebserverBench(Bench):
        def target(self, conf):
            return f"{self.prefix}/{conf}/stamp.txt"
        def stat(self, conf):
            l = []
            with Path(self.target(conf)).with_name("stats.txt").open() as f:
                for line in f:
                    if m := re.match(r"simSeconds\s+([0-9.]+)", line):
                        l.append(float(m.group(1)))
            assert len(l) >= 1
            return l[-1]
        def confs(self):
            return [
                "base/unsafe.se",
                f"base/{self.baseline}.se.atret",
                f"{self.protcc}/prottrack.se.atret",
                f"{self.protcc}/protdelay.se.atret",
            ]

    benches = [
        Bench(f"ctsbench.hacl.poly1305/exp/0/ctsbench", "spt", "cts", "CTS-Crypto", "hacl.poly1305"),
        Bench(f"bearssl/exp/0/ctbench", "spt", "ct", "CT-Crypto", "bearssl"),
        Bench(f"nctbench.openssl.bnexp/exp/0/nctbench", "sptsb", "nct", "UNR-Crypto", "ossl.bnexp"),
        WebserverBench(f"webserv/exp/c1r1", "sptsb", "nct.ossl-annot", "Multi-Class Webserver", "nginx.c1r1"),
    ]

    baseline_names = {
        "spt": "SPT",
        "sptsb": "SPT-SB",
    }

    targets = []
    for bench in benches:
        targets.extend(map(bench.target, bench.confs()))

    # Invoke snakemake to run the experiments.
    if not args.dry_run:
        subprocess.run(args.snakemake_command + targets, check=True, shell=True)

    # Grab the results and print a table, similar to Table IV in the paper.
    table = []
    for bench in benches:
        if bench is not benches[0]:
            table.append([])
        table.append([bench.suite_name, baseline_names[bench.baseline], "Protean (ProtTrack)", "Protean (ProtDelay)"])
        table.append([
            bench.bench_name,
            *map(bench.normalized_stat, bench.confs()[1:]),
        ])

    # Pretty-print the table.
    print("Table IV")
    print(tabulate(table))

def do_perf_eval(args):
    os.chdir("bench")
    if args.size == "small":
        do_perf_eval_small(args)
    elif args.size == "medium":
        do_perf_eval_medium(args)
    elif args.size == "full":
        do_perf_eval_full(args)
    else:
        raise ValueError(args.size)

def do_sec_eval(args):
    if args.size == "small":
        do_sec_eval_small(args)
    elif args.size == "full":
        do_sec_eval_full(args)
    else:
        raise ValueError(args.size)
    

if args.type == "perf":
    do_perf_eval(args)
elif args.type == "sec":
    do_sec_eval(args)
else:
    raise ValueError(args.type)


