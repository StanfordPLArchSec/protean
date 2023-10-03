#!/usr/bin/python3

import argparse
import os
import json
import sys
import types

parser = argparse.ArgumentParser()
parser.add_argument('config', help='Path to JSON configuration file')
args = parser.parse_args()


with open(args.config) as f:
    config = json.load(f)

def resolve_inheritance(d: dict) -> dict:
    resolved = dict()

    def check_resolved(key) -> bool:
        return 'inherit' not in d[key]

    found_unresolved = True
    while found_unresolved:
        found_unresolved = False
        resolved_one = False
        for key in d:
            if not check_resolved(key):
                super = d[key]['inherit']
                if check_resolved(super):
                    del d[key]['inherit']
                    d[key] = {**d[super], **d[key]}

                    extend_prefix = '+'
                    extended = True
                    while extended:
                        extended = False
                        for key in d:
                            if key.startswith(extend_prefix):
                                assert type(d[key]) is list
                                base_key = key.removeprefix(extend_prefix)
                                assert base_key in d and type(d[base_key]) is list
                                d[base_key].extend(d[key])
                                del d[key]
                                extended = True
                                break
                    
                    resolved_one = True
                else:
                    found_unresolved = True
        if found_unresolved and not resolved_one:
            print('fatal error: detected cycle in inheritance', file=sys.stderr)
            exit(1)

    return dict(filter(lambda p: not p[0].startswith('_'), d.items()))


def abs_config_path(path: str) -> str:
    if path.startswith('/'):
        return path
    return os.path.abspath(os.path.join(os.path.dirname(args.config), path))

def prepare_sw_config(sw_config_in: dict) -> types.SimpleNamespace:
    for key in ['spec2017', 'test-suite', 'cc', 'cxx']:
        sw_config_in[key] = abs_config_path(sw_config_in[key])
    sw_config_in = dict(map(lambda p: (p[0].replace('-', '_'), p[1]), sw_config_in.items()))
    sw_config_out = types.SimpleNamespace(**sw_config_in)
    return sw_config_out

def process_config(config_in: dict) -> types.SimpleNamespace:
    sw_configs = resolve_inheritance(config_in['sw'])
    sw_configs = dict([(p[0], prepare_sw_config(p[1])) for p in sw_configs.items()])
    return types.SimpleNamespace(sw=sw_configs)

config = process_config(config)

# Generate CMake hierarchy
def generate_top_cmakelists():
    content = \
'''cmake_minimum_required(VERSION 3.20)
project(llsct-results)
include(ExternalProject)
add_subdirectory(sw)
'''
    with open('CMakeLists.txt', 'w') as f:
        f.write(content)
    
def generate_sw_cmakelists(sw_configs):
    with open('sw/CMakeLists.txt', 'w') as f:
        for sw_config in sw_configs:
            f.write(f'add_subdirectory({sw_config})\n')

def generate_sw_subdir(name, config):
    dir = 'sw/' + name
    os.makedirs(dir, exist_ok=True)
    with open(dir + '/CMakeLists.txt', 'w') as f:
        cflags = ' '.join(config.cflags)
        ldflags = ' '.join(config.ldflags)
        content = f'''
ExternalProject_Add({name}-test-suite
        SOURCE_DIR {config.test_suite}
        STAMP_DIR stamp
        BINARY_DIR build
        CMAKE_ARGS
        -DCMAKE_C_COMPILER={config.cc}
        -DCMAKE_CXX_COMPILER={config.cxx}
        -DCMAKE_BUILD_TYPE={config.cmake_build_type}
        "-DCMAKE_C_FLAGS={cflags}"
        "-DCMAKE_CXX_FLAGS={cflags}"
        "-DCMAKE_EXE_LINKER_FLAGS={ldflags}"
        -DTEST_SUITE_SUBDIRS=External
        -DTEST_SUITE_SPEC2017_ROOT={config.spec2017}
        -DTEST_SUITE_RUN_TYPE={config.test_suite_run_type}
)
'''
        f.write(content)

generate_top_cmakelists()
os.makedirs('sw', exist_ok=True)
generate_sw_cmakelists(config.sw)
for name, sw_config in config.sw.items():
    generate_sw_subdir(name, sw_config)
