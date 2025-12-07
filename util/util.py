from pathlib import Path
import re
import subprocess
import math
import json
import shlex
import sys

g_args = None

def set_args(args):
    global g_args
    g_args = args

# TODO: Remove.
def comma_list(s):
    return s.split(",")

def add_common_arguments(parser):
    parser.add_argument(
        "--dry-run", "-n",
        action="store_true",
        help="Don't (re)run results; just generate figures/tables.",
    )
    parser.add_argument(
        "--expected", "-x",
        action="store_true",
        help="Generate the expected figure/table, for comparison.",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose output."
    )
    parser.add_argument(
        "snakemake_command",
        nargs="*",
        default=["snakemake", "--cores=all"],
        help="Override snakemake binary/args with this."),
    
    
def should_regenerate(args):
    return not args.dry_run and not args.expected

def run_if_requested(args_, *args, **kwargs):
    if should_regenerate(args_):
        run(*args, **kwargs)

def ResultPath(path):
    path = Path(path)
    if g_args.expected:
        assert path.parts[0] != "reference"
        path = Path("reference") / path
    return path

def do_format_text(text, subs):
    while m := re.search(r"@(.*?)@", text):
        key = m.group(1)
        value = subs[key]
        text = text.replace(m.group(0), value)
    assert "@" not in text
    return text

def pdflatex(tex):
    pdflatex_args = []
    if not g_args.verbose:
        pdflatex_args.append("--interaction=batchmode")
    run(["pdflatex", *pdflatex_args, str(tex)])

def run(cmd, *args, **kwargs):
    if g_args.verbose:
        cmd_str = shlex.join(cmd)
        print(f"INFO: running subprocess: {cmd_str}", file=sys.stderr)
    subprocess.run(cmd, *args, check=True, **kwargs)

def format_tex(name, subs):
    text = Path(f"templates/{name}.tex.in").read_text()
    text = do_format_text(text, subs)
    Path(f"{name}.tex").write_text(text)

def format_and_render_tex(name, subs):
    format_tex(name, subs)
    pdflatex(f"{name}.tex")

def geomean(l):
    return math.prod(l) ** (1 / len(l))

def json_cycles(path):
    j = json.loads(ResultPath(path).read_text())
    return j["stats"]["cycles"]

def stats_seconds(path):
    l = []
    with ResultPath(path).with_name("stats.txt").open() as f:
        for line in f:
            if m := re.match(r"simSeconds\s+([0-9.]+)", line):
                l.append(float(m.group(1)))
    assert len(l) >= 1
    return l[-1]    

def rgbtohex(r, g, b):
    return "#" + "".join(map(lambda x: f"{x:02x}", [r, g, b]))

def _make_name(type, id):
    return (
        f"{type.lower().capitalize()} {id.upper()}",
        f"{type.lower()}-{id.lower()}",
    )

def make_name():
    exe = sys.argv[0]
    assert exe.endswith(".py")
    stem = Path(exe).stem
    type, id = stem.split("-")
    return _make_name(type, id)
