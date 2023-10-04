#!/usr/bin/python3

import argparse
import os
import json
import sys
import types
import util
import ninja_syntax

parser = argparse.ArgumentParser()
parser.add_argument('config', help='Path to JSON configuration file')
parser.add_argument('benchspec', help='Path to benchmark specifications')
args = parser.parse_args()

with open(args.config) as f:
    config = json.load(f)
config = util.process_config(config, os.path.dirname(args.config))

with open(args.benchspec) as f:
    benchspec = json.load(f)
benchspec = util.process_benchspec(benchspec)

f = open('build.ninja', 'w')
ninja = ninja_syntax.Writer(f)


# Define rules

## test-suite-configure
test_suite_cmake_command = [
    'cmake',
    '-S', '$src',
    '-B', '$build',
    '-DCMAKE_C_COMPILER=$cc',
    '-DCMAKE_CXX_COMPILER=$cxx',
    '-DCMAKE_BUILD_TYPE=$build_type',
    '-DCMAKE_C_FLAGS="$cflags"',
    '-DCMAKE_CXX_FLAGS="$cflags"',
    '-DCMAKE_EXE_LINKER_FLAGS="$ldflags"',
    '-DTEST_SUITE_SUBDIRS=External',
    '-DTEST_SUITE_SPEC2017_ROOT=$spec2017',
    '-DTEST_SUITE_RUN_TYPE=$run_type',
    # '-DCMAKE_NINJA_OUTPUT_PATH_PREFIX=$build',
]
rule_test_suite_configure = 'test-suite-configure'
ninja.rule(
    name = rule_test_suite_configure,
    command = ' '.join(test_suite_cmake_command),
    description = '($id) Configure the LLVM test suite',
)

## test-suite-build
test_suite_build_command = ['cmake', '--build', '$build', '--target', 'fpcmp-target']
for bench_name in benchspec:
    test_suite_build_command.extend(['--target', bench_name])
test_suite_build_command.extend(['--', '--quiet'])
rule_test_suite_build = 'test-suite-build'
ninja.rule(
    name = rule_test_suite_build,
    command = ' '.join(test_suite_build_command),
    description = '($id) Build the LLVM test suite',
    restat = True,
)

## gem5-build
gem5_build_command = [ 'scons', '--quiet', '-C', '$src', '$out', '$sconsopts']
rule_gem5_build = 'gem5-build'
ninja.rule(
    name = rule_gem5_build,
    command = ' '.join(gem5_build_command),
    description = '($id) Build gem5',
    restat = True,
)

## copy-resource-dir
ninja.rule(
    name = 'copy-resource-dir',
    command = 'rm -r $dst && mkdir -p $dst && rmdir $dst && cp -r $src $dst && touch $stamp',
    description = '($id) Copy resource directory',
)

## stamped custom command -- cmd, stamp, id, desc
ninja.rule(
    name = 'stamped-custom-command',
    command = '$cmd && touch $stamp',
    description = '($id) $desc',
)

## custom command -- cmd, id, desc
ninja.rule(
    name = 'custom-command',
    command = '$cmd',
    description = '($id) $desc',
)

# Define builds

# dummy output
ninja.build(
    outputs = 'dummy',
    rule = 'phony',
)


## sw builds
for sw_name, sw_config in config.sw.items():
    build_dir = os.path.join('sw', sw_name)
    variables = {
        'src': sw_config.test_suite,
        'build': build_dir,
        'cc': sw_config.cc,
        'cxx': sw_config.cxx,
        'build_type': sw_config.cmake_build_type,
        'cflags': ' '.join(sw_config.cflags),
        'ldflags': ' '.join(sw_config.ldflags),
        'spec2017': sw_config.spec2017,
        'run_type': sw_config.test_suite_run_type,
        'id': f'sw->{sw_name}',
    }

    ## test-suite-build
    ninja.build(
        outputs = os.path.join(build_dir, 'build.ninja'),
        rule = rule_test_suite_configure,
        inputs = [sw_config.cc, sw_config.cxx],
        variables = variables,
    )

    ## test-suite-configure
    outputs = [os.path.join(build_dir, 'External', 'SPEC', 'CINT2017speed', bench_name, bench_name) for bench_name in benchspec]
    outputs.append(os.path.join(build_dir, 'tools', 'fpcmp-target'))
    ninja.build(
        outputs = outputs,
        rule = rule_test_suite_build,
        inputs = os.path.join(build_dir, 'build.ninja'),
        variables = variables,
    )

# sim builds
for sim_name, sim_config in config.sim.items():
    dir = os.path.join('sim', sim_name)
    exe = os.path.join(dir, sim_config.target)
    variables = {
        'src': sim_config.src,
        'sconsopts': ' '.join(sim_config.sconsopts),
        'id': f'sim->{sim_name}',
    }

    ## gem5-build
    ninja.build(
        outputs = exe,
        rule = rule_gem5_build,
        inputs = 'dummy',
        variables = variables,
    )


# cpt builds
for sw_name, sw_config in config.sw.items():
    for bench_name, bench_spec in benchspec.items():
        bench_dir = os.path.join('sw', sw_name, 'External', 'SPEC', 'CINT2017speed', bench_name)
        exe = os.path.join(bench_dir, bench_name)
        rsrc_dir = os.path.join(bench_dir, f'run_{sw_config.test_suite_run_type}')

        for subdir in ['host', 'bbv', 'kvm']:
            dir = os.path.join('cpt', sw_name, bench_name, subdir)
            stamp = os.path.join(dir, 'copy.stamp')
            ninja.build(
                outputs = stamp,
                rule = 'copy-resource-dir',
                inputs = exe,
                variables = {
                    'src': rsrc_dir,
                    'dst': dir,
                    'stamp': stamp,
                    'id': f'{sw_name}->{bench_name}->{subdir}'
                },
            )

        # shared verification stuff
        verify_path = os.path.abspath(os.path.join('sw', sw_name, 'tools'))
        verify_rawcmds = ' && '.join(bench_spec.verify)
        verify_rawcmds = verify_rawcmds.replace('%b', verify_path)

        ## host
        host_subdir = os.path.join('cpt', sw_name, bench_name, 'host')
        ### host run
        host_outputs = [os.path.join(host_subdir, output) for output in bench_spec.outputs]
        assert len(host_outputs) > 0
        host_run_args = ' '.join(bench_spec.args)
        host_run_cmd = f'cd {host_subdir} && {os.path.abspath(exe)} {host_run_args}'
        ninja.build(
            outputs = host_outputs,
            rule = 'custom-command',
            inputs = [exe, os.path.join(host_subdir, 'copy.stamp')],
            variables = {
                'cmd': host_run_cmd,
                'id': f'{sw_name}->{bench_name}->host->run',
                'desc': 'Run benchmark on host',
            },
        )

        ### host verify
        host_verify_stamp = os.path.join(host_subdir, 'verify.stamp')
        host_verify_cmd = f'cd {host_subdir} && {verify_rawcmds}'
        ninja.build(
            outputs = host_verify_stamp,
            rule = 'stamped-custom-command',
            inputs = host_outputs,
            variables = {
                'cmd': host_verify_cmd,
                'stamp': 'verify.stamp',
                'id': f'{sw_name}->{bench_name}->host->verify',
                'desc': 'Verify benchmark on host',
            }
        )

        ## bbv
        bbv_subdir = os.path.join('cpt', sw_name, bench_name, 'bbv')

        ### bbv run
        bbv_file = os.path.join(bbv_subdir, 'bbv.out')
        bbv_run_outputs = [os.path.join(bbv_subdir, output) for output in bench_spec.outputs]
        bbv_outputs = [bbv_file, *bbv_run_outputs]
        assert len(bbv_outputs) >= 2
        bbv_run_args = ' '.join(bench_spec.args)
        bbv_run_cmd = [
            'cd', bbv_subdir, '&&',
            config.vars.valgrind, '--tool=exp-bbv', '--bb-out-file=bbv.out', f'--interval-size={config.vars.interval}',
            '--quiet',
            '--', os.path.abspath(exe), *bench_spec.args,
        ]
        ninja.build(
            outputs = bbv_outputs,
            rule = 'custom-command',
            inputs = [
                exe,
                os.path.join('cpt', sw_name, bench_name, 'host', 'verify.stamp'),
                os.path.join('cpt', sw_name, bench_name, 'bbv', 'copy.stamp'),
            ],
            variables = {
                'cmd': ' '.join(bbv_run_cmd),
                'id': f'{sw_name}->{bench_name}->bbv->run',
                'desc': 'Profile using valgrind',
            },
        )

        ### bbv verify
        bbv_verify_stamp = os.path.join(bbv_subdir, 'verify.stamp')
        bbv_verify_cmd = f'cd {bbv_subdir} && {verify_rawcmds}'
        ninja.build(
            outputs = bbv_verify_stamp,
            rule = 'stamped-custom-command',
            inputs = bbv_run_outputs,
            variables = {
                'cmd': bbv_verify_cmd,
                'stamp': 'verify.stamp',
                'id': f'{sw_name}->{bench_name}->bbv->verify',
                'desc': 'Verify valgrind benchmark results',
            }
        )

        ## simpoints
        spt_subdir = os.path.join('cpt', sw_name, bench_name, 'spt')
        spt_simpoints = os.path.join(spt_subdir, 'simpoints.out')
        spt_weights = os.path.join(spt_subdir, 'weights.out')
        spt_outputs = [spt_simpoints, spt_weights]
        spt_cmd = [
            config.vars.simpoint, '-loadFVFile', bbv_file, '-maxK', str(config.vars.num_simpoints),
            '-saveSimpoints', spt_simpoints, '-saveSimpointWeights', spt_weights,
        ]
        ninja.build(
            outputs = spt_outputs,
            rule = 'custom-command',
            inputs = bbv_file,
            variables = {
                'cmd': ' '.join(spt_cmd),
                'id': f'{sw_name}->{bench_name}->spt',
                'desc': 'Compute SimPoints',
            }
        )
        
        
        
            
            
            
