from pathlib import Path
import re
import subprocess

g_args = None

def set_args(args):
    global g_args
    g_args = args

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
    
def should_regenerate(args):
    return not args.dry_run and not args.expected

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
    run(["pdflatex",
         *pdflatex_args,
         str(tex)],
        check=True)

def run(cmd, *args, **kwargs):
    if g_args.verbose:
        cmd_str = shlex.join(cmd)
        print(f"INFO: running subprocess: {cmd_str}", file=sys.stderr)
    subprocess.run(cmd, *args, **kwargs)
