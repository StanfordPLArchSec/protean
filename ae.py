#!/usr/bin/env python3

import argparse
import os
from pathlib import Path
import subprocess
from tabulate import tabulate
import json
import re
from contextlib import chdir
import shlex
import sys
from collections import defaultdict

parser = argparse.ArgumentParser()
subparser = parser.add_subparsers(dest="command", required=True)
subparser_run = subparser.add_parser("run")
subparser_run.add_argument("--type", "-t", required=True, choices=["perf", "sec"])
subparser_run.add_argument("--size", "-s", required=True, choices=["small", "medium", "full"])
subparser_gen = subparser.add_parser("generate")
subparser_gen.add_argument("name", choices=["table-iv", "section-ix-a", "figure-5", "figure-1", "table-i"])
subparser_gen.add_argument("--output", "-o")

# Some shared flags.
for sub in [subparser_run, subparser_gen]:
    sub.add_argument("snakemake_command", nargs="*", default=["snakemake", "--cores=all"],
                     help="Override snakemake binary/args with this.")
    sub.add_argument("--dry-run", "-n", action="store_true")
    sub.add_argument("--expected", action="store_true")
    sub.add_argument("--verbose", "-v", action="store_true")
    sub.add_argument("--force", "-f", action="store_true",
                     help="Overwrite existing results")


g_args = args = parser.parse_args()

# Go to the root directory.
root_dir = Path(__file__).resolve().parent
os.chdir(root_dir)

def ResultPath(path):
    path = Path(path)
    if args.expected:
        assert path.parts[0] != "reference"
        path = Path("reference") / path
    return path

def run(cmd, *args, **kwargs):
    if g_args.verbose:
        cmd_str = shlex.join(cmd)
        print(f"INFO: running subprocess: {cmd_str}", file=sys.stderr)
    subprocess.run(cmd, *args, **kwargs)

def should_regenerate(args):
    return not args.dry_run and not args.expected

def pdflatex(tex):
    pdflatex_args = []
    if not args.verbose:
        pdflatex_args.append("--interaction=batchmode")
    run(["pdflatex",
         *pdflatex_args,
         str(tex)],
        check=True)

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
            j = json.loads(ResultPath(path).read_text())
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
            with ResultPath(self.target(conf)).with_name("stats.txt").open() as f:
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
    if not args.dry_run and not args.expected:
        run(args.snakemake_command + targets, check=True)

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
    print(tabulate(table))

def do_sec_eval_small(args):
    # Just run the UNPROT tests with scaled down parameters.
    # Number of jobs per tuple: 5
    # Number of programs: 50
    # Number of inputs per program: 50/3 (?)
    dirs = []
    fuzz_targets = []
    triage_targets = []
    for defense in ["none", "prottrack", "protdelay"]:
        for adversary in ["cache", "commit"]:
            d = f"{defense}-prot-llvm.prot-{adversary}"
            dirs.append(d)
            fuzz_targets.append(d + "/all")
            triage_targets.append(d + "/triage")

    # Run amulet via snakemake.
    with chdir("amulet"):
        if should_regenerate(args):
            # Make sure that the output directory is empty.
            for d in dirs:
                d = Path(d)
                if d.exists():
                    if args.force:
                        print(f"WARN: overwriting results directory {d}", file=sys.stderr)
                        d.rmdir()
                    else:
                        print(f"ERROR: refusing to overwrite existing results directory {d}", file=sys.stderr)
                        exit(1)
            
            # Fuzz.
            run([*args.snakemake_command, *fuzz_targets,
                 "--config",
                 "instances=5",
                 "programs=50",
                 "inputs_cache=50",
                 "inputs_commit=3"],
                check=True)

            # Triage.
            run([*args.snakemake_command, *triage_targets],
                check=True)

        # Read the results of triaging.
        l = []
        for defense in ["none", "prottrack", "protdelay"]:
            l.append({
                "true-positive": 0,
                "false-positive": 0,
            })
            for adversary in ["commit", "cache"]:
                p = ResultPath(f"{defense}-prot-llvm.prot-{adversary}/triage")
                j = json.loads(p.read_text())
                for result in j.values():
                    l[-1][result["result"]] += 1

    # TODO: Shared code with elsewhere.
    # Factor out common code.
    subs = defaultdict(lambda: "- & - & -")
    def format_result(result):
        tps = result["true-positive"]
        fps = result["false-positive"]
        return r"\textbf{" + str(tps) + r"} (" + str(fps) + ")"
    subs["prot-llvm.prot"] = " & ".join(map(format_result, l))

    do_format_table_i(subs)

    # Generate the PDF.
    pdflatex("table-i.tex")

    print("DONE: See table-i.pdf")
            
    
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

def do_run(args):
    if args.type == "perf":
        do_perf_eval(args)
    elif args.type == "sec":
        do_sec_eval(args)
    else:
        raise ValueError(args.type)

# GENERATE TABLE IV.
def do_generate_class_specific_table(args):
    # Collect the snakemake targets.
    names = [
        "wasmbench",
        "ctsbench",
        "ctbench",
        "nctbench",
        "webserv",
    ]
    targets = map(lambda x: f"tables/{x}.tex", names)

    with chdir("bench"):
        if should_regenerate(args):
            run([*args.snakemake_command, "--configfile=checkpoint-config.yaml", *targets],
                           check=True)
        content = dict()
        for name in names:
            content[name] = ResultPath(f"tables/{name}.tex").read_text()

    # Fill in template.
    tex = Path("table-iv.tex")
    text = Path("templates/table-iv.tex.in").read_text()
    for k, v in content.items():
        text = text.replace(f"@{k}@", v)
    tex.write_text(text)

    # Make .pdf.
    pdflatex(tex)

    # Done!
    print("DONE.")
    print("table-iv.pdf contains rendered PDF of table.")
    print("table-iv.tex contains raw .tex for table.")

def do_generate_results_text(args):
    names = [
        "protcc-overhead",
        "protl1-variants",
        "access",
        "spectre-ctrl",
        "baseline-fixes",
    ]

    targets = list(map(lambda x: f"results/{x}.tex", names))

    with chdir("bench"):
        if should_regenerate(args):
            run([*args.snakemake_command, "--configfile=checkpoint-config.yaml", *targets],
                           check=True)
        content = dict()
        for k, target in zip(names, targets):
            content[k] = ResultPath(target).read_text()

    # Fill in template.
    tex = Path(f"section-ix-a.tex")
    text = Path("templates/section-ix-a.tex.in").read_text()
    for k, v in content.items():
        text = text.replace(f"@{k}@", v)
    tex.write_text(text)

    # Make PDF.
    pdflatex(tex)

    print("DONE.")
    print(f"{args.name}.pdf contains rendered PDF of text.")
    print(f"{args.name}.tex contains raw .tex.")

def do_generate_ablation(args):
    targets = [
        "figures/predictor-mispredict-rate.csv",
        "figures/predictor-runtime.csv",
    ]
    with chdir("bench"):
        if should_regenerate(args):
            run([*args.snakemake_command, *targets, "--configfile=checkpoint-config.yaml"],
                           check=True)
        run(["figures/predictor.py", 
                        f"--rate-csv={ResultPath(targets[0])}",
                        f"--runtime-csv={ResultPath(targets[1])}",
                        "-o", "../figure-5.pdf",
                        "--no-crop"],
                       check=True)
    print("DONE: See figure-5.pdf")

def do_generate_survey(args):
    target = "tables/survey.tex"
    with chdir("bench"):
        if should_regenerate(args):
            run([*args.snakemake_command, target, "--configfile=checkpoint-config.yaml"],
                           check=True)
        # Read survey.tex.
        content = ResultPath(target).read_text()

    tex = Path("figure-1.tex")
    text = Path("templates/figure-1.tex.in").read_text()
    text = text.replace("@body@", content)
    tex.write_text(text)
    pdflatex(tex)

    print("DONE: See figure-1.pdf")

def do_format_text(text, subs):
    while m := re.search(r"@(.*?)@", text):
        key = m.group(1)
        value = subs[key]
        text = text.replace(m.group(0), value)
    assert "@" not in text
    return text
    
def do_format_table_i(subs):
    text = Path("templates/table-i.tex.in").read_text()
    text = do_format_text(text, subs)
    Path("table-i.tex").write_text(text)
    
def do_generate_protean_amulet(args):
    # First, run all the experiments.
    obs_gen_pairs = [
        ("prot", "llvm.prot"),
        ("arch", "llvm.arch"),
        ("cts", "llvm.cts"),
        ("ct", "llvm.ct"),
        ("ct", "llvm.unr"),
    ]
    defenses = ["none", "prottrack", "protdelay"]
    adversaries = ["commit", "cache"]
    fuzz_targets = []
    triage_targets = []
    for observer, generator in obs_gen_pairs:
        for defense in defenses:
            for adversary in adversaries:
                d = f"{defense}-{observer}-{generator}-{adversary}"
                fuzz_targets.append(os.path.join(d, "all"))
                triage_targets.append(os.path.join(d, "triage"))


    with chdir("amulet"):
        if should_regenerate(args):
            # Fuzz step.
            run([*args.snakemake_command, *fuzz_targets],
                           check=True)

            # Triage step.
            run([*args.snakemake_command, *triage_targets, "-j1"],
                           check=True)

        # Read the results, per defense.
        results = {}
        for observer, generator in obs_gen_pairs:
            l = results[f"{observer}-{generator}"] = []
            for defense in defenses:
                l.append({
                    "true-positive": 0,
                    "false-positive": 0,
                })
                for adversary in adversaries:
                    p = ResultPath(f"{defense}-{observer}-{generator}-{adversary}/triage")
                    j = json.loads(p.read_text())
                    for result in j.values():
                        l[-1][result["result"]] += 1

    # Substitute into the template.    
    subs = defaultdict(lambda: "- & - & -")
    for key, result in results.items():
        def format_result(result):
            tps = result["true-positive"]
            fps = result["false-positive"]
            return r"\textbf{" + str(tps) + r"} (" + str(fps) + ")"
        subs[key] = " & ".join(map(format_result, result))

    do_format_table_i(subs)

    # Generate the PDF.
    pdflatex("table-i.tex")

    print("DONE: See table-i.pdf")
    
    
def do_generate(args):
    if args.name == "table-iv":
        do_generate_class_specific_table(args)
    elif args.name == "section-ix-a":
        do_generate_results_text(args)
    elif args.name == "figure-5":
        do_generate_ablation(args)
    elif args.name == "figure-1":
        do_generate_survey(args)
    elif args.name == "table-i":
        do_generate_protean_amulet(args)
    else:
        raise NotImplementedError()
    
if args.command == "run":
    do_run(args)
elif args.command == "generate":
    do_generate(args)
else:
    raise NotImplementedError(f"subcommand {args.command}")
