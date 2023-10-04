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

f = open('build.ninja', 'w')
ninja = ninja_syntax.Writer(f)


# Define rules
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

test_suite_build_command = [
    'cmake',
    '--build', '$build',
]
for bench_name in benchspec:
    test_suite_build_command.extend(['--target', bench_name])
rule_test_suite_build = 'test-suite-build'
ninja.rule(
    name = rule_test_suite_build,
    command = ' '.join(test_suite_build_command),
    description = '($id) Build the LLVM test suite',
)
    

# Define builds
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
    ninja.build(
        outputs = os.path.join(build_dir, 'build.ninja'),
        rule = rule_test_suite_configure,
        inputs = [sw_config.cc, sw_config.cxx],
        variables = variables,
    )

    outputs = [os.path.join(build_dir, 'External', 'SPEC', 'CINT2017speed', bench_name, bench_name) for bench_name in benchspec]
    ninja.build(
        outputs = outputs,
        rule = rule_test_suite_build,
        inputs = os.path.join(build_dir, 'build.ninja'),
        variables = {
            'build': build_dir,
            'id': f'sw->{sw_name}',
        },
    )
