import sys
import argparse
import collections
import os
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--gem5-binary', required = True, type = str)
parser.add_argument('--gem5-se-py', required = True, type = str)
parser.add_argument('--input-rundir', required = True, type = str)
parser.add_argument('--output-rundir', required = True, type = str) # must be able to format with an integer
parser.add_argument('--test-suite-binary-dir', required=  True, type = str)
parser.add_argument('--test', required = True, type = str)
parser.add_argument('--no-verify', action = argparse.BooleanOptionalAction)
parser.add_argument('--no-redirect', action = argparse.BooleanOptionalAction)
parser.add_argument('se_args', nargs = '*')
args = parser.parse_args()

with open(args.test) as f:
    lines = f.read().splitlines()

line_by_cmd = collections.defaultdict(list)
line_by_last = collections.defaultdict(list)
for line in lines:
    tokens = line.split()
    line_by_cmd[tokens[0]].append(line)
    line_by_last[tokens[-1]].append(line)

runs = line_by_cmd['RUN:']
verifies = line_by_cmd['VERIFY:']
if len(verifies) > len(runs):
    verifies = verifies[:len(runs)]
assert len(runs) <= len(verifies)


def do_substitutions(line, output_rundir):
    input_rundir_name = os.path.basename(args.input_rundir)
    output_rundir_name = os.path.basename(output_rundir)
    line = line.replace('%S', f'{output_rundir}/..')
    line = line.replace(input_rundir_name, output_rundir_name)
    line = line.replace('%b', f'{args.test_suite_binary_dir}/tools')
    return line

def get_cmd_argv(cmdline, outrundir, i):
    tokens = cmdline.split()

    final_cmd = [args.gem5_binary, f'--outdir=$m5out', args.gem5_se_py]
    for se_arg in args.se_args:
        se_arg = se_arg.replace('%i', str(i))
        final_cmd.append(se_arg)

    if '>' in tokens:
        stdout_idx = tokens.index('>')
        stdout_file = tokens[stdout_idx+1]
        if not args.no_redirect:
            final_cmd.append(f'--output={stdout_file}')
        del tokens[stdout_idx:stdout_idx+2]

    if '2>' in tokens:
        stderr_idx = tokens.index('2>')
        stderr_file = tokens[stderr_idx+1]
        if not args.no_redirect:
            final_cmd.append(f'--errout={stderr_file}')
        del tokens[stderr_idx:stderr_idx+2]

    final_cmd.append(f'--cmd={tokens[0]}')
    argstr = ' '.join(tokens[1:])
    final_cmd.append(f'--options="{argstr}"')

    return final_cmd
    
# group into separate commands
testname = os.path.splitext(os.path.basename(args.test))[0]
groups = []
identifiers = []
for i, run in enumerate(runs):
    # make directory
    outrundir = args.output_rundir.replace('%i', str(i))
    if os.path.isdir(outrundir):
        shutil.rmtree(outrundir)
    shutil.copytree(args.input_rundir, outrundir)
    # m5out = f'{outrundir}/m5out'
    # os.mkdir(m5out)
    outrunsh = f'{outrundir}/run.sh'
    with open(outrunsh, 'w') as f:
        # initial stuff

        preamble = '''
        set -e

        m5out="{outrundir}/m5out"
        while getopts "ho:c:" optc; do
            case $optc in
               h)
                  usage
                  exit
                  ;;
               o)
                  m5out="$OPTARG"
                  ;;
               c)
                  checkpoint="$OPTARG"
                  ;;
               *)
                  usage >&2
                  exit 1
                  ;;
            esac
        done
        
        cd {outrundir}
        rm -rf $m5out
        mkdir -p $m5out
        '''.format(outrundir = outrundir)

        print(preamble, file = f)
        
        # emit run
        last = run.split()[-1]
        run = do_substitutions(run, outrundir)
        run = run.removeprefix('RUN: ')
        run_cmdlines = run.split(';')
        assert len(run_cmdlines) == 2
        run_cmdline = run_cmdlines[-1]
        run_cmd = get_cmd_argv(run_cmdline, outrundir, i)
        print(*run_cmd, file = f)
        # print(f'{args.gem5_binary} --outdir={outrundir}/m5out {args.gem5_se_py} {se_argstr} --cmd={run_cmd} --options=\'{run_argstr}\' --output={outrundir}/m5out/stdout --errout={outrundir}/stderr >{outrundir}/simout 2>{outrundir}/simerr', file = f)
        # print(f'cat {outrundir}/m5out/stdout', file = f)
        # print(f'cat {outrundir}/m5out/stderr >&2', file = f)

        if args.no_verify:
            continue

        for line in line_by_last[last]:
            if not line.startswith('VERIFY:'):
                continue
            verify = line.removeprefix('VERIFY: ')
            verify = do_substitutions(verify, outrundir)
            print(verify, file = f)
    identifiers.append(i)

print(';'.join(map(str, identifiers)), end = '')
