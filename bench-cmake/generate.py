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

def prepare_subconfig(config: dict, path_keys) -> types.SimpleNamespace:
    for key in path_keys:
        if key not in config:
            print(config)
        assert key in config
        config[key] = abs_config_path(config[key])
    config = dict(map(lambda p: (p[0].replace('-', '_'), p[1]), config.items()))
    config = types.SimpleNamespace(**config)
    return config

def process_subconfigs(configs, path_keys):
    configs = resolve_inheritance(configs)
    for name, config in configs.items():
        config = prepare_subconfig(config, path_keys)
        configs[name] = config
    return configs

def process_config(config_in: dict) -> types.SimpleNamespace:
    sw_configs = process_subconfigs(config_in['sw'], ['spec2017', 'test-suite', 'cc', 'cxx'])
    sim_configs = process_subconfigs(config_in['sim'], ['src'])
    hwmode_configs = process_subconfigs(config_in['hwmode'], [])
    vars = prepare_subconfig(config_in['vars'], ['benchspec', 'llsct'])
    return types.SimpleNamespace(sw=sw_configs, sim=sim_configs, hwmode=hwmode_configs, vars=vars)

config = process_config(config)

# Generate CMake hierarchy
def generate_top_cmakelists():
    content = \
'''cmake_minimum_required(VERSION 3.20)
project(llsct-results)
include(ExternalProject)
add_subdirectory(sw)
add_subdirectory(sim)
add_subdirectory(cpt)
'''
    with open('CMakeLists.txt', 'w') as f:
        f.write(content)

def generate_intermediate_cmakelists(dir, subdirs):
    path = os.path.join(dir, 'CMakeLists.txt')
    with open(path, 'w') as f:
        for subdir in subdirs:
            f.write(f'add_subdirectory({subdir})\n')
    
        
def generate_sw_subdir(name, config):
    dir = os.path.join('sw', name)
    os.makedirs(dir, exist_ok=True)
    with open(os.path.join(dir, 'CMakeLists.txt'), 'w') as f:
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
        INSTALL_COMMAND ""
)
'''
        f.write(content)

        content = f'''
ExternalProject_Add_Step({name}-test-suite clean
        COMMAND cmake --build ${{CMAKE_CURRENT_BINARY_DIR}}/build --target clean
        DEPENDEES configure
        DEPENDERS build
)
'''
        # f.write(content)

        content = f'''
add_executable(cc IMPORTED)
add_executable(cxx IMPORTED)
set_property(TARGET cc PROPERTY IMPORTED_LOCATION {config.cc})
set_property(TARGET cxx PROPERTY IMPORTED_LOCATION {config.cxx})
add_dependencies({name}-test-suite cc cxx)
'''
        # f.write(content)


def generate_sim_subdir(name, config):
    dir = os.path.join('sim', name)
    os.makedirs(dir, exist_ok=True)
    with open(os.path.join(dir, 'CMakeLists.txt'), 'w') as f:
        sconsopts = ' '.join(config.sconsopts)
        content = f'''
add_custom_command(
        OUTPUT {config.target}
        COMMAND scons -C {config.src} {config.target} {sconsopts}
)
add_custom_target(sim-{name} ALL
        DEPENDS {config.target}
)
'''
        f.write(content)
        
def generate_cpt_subdir(name, config):
    dir = os.path.join('cpt', name)
    os.makedirs(dir, exist_ok=True)

    sim_name = config.vars.checkpoint_sim
    sim_config = config.sim[sim_name]

    gem5_exe = f'${{CMAKE_BINARY_DIR}}/sim/{sim_name}/{sim_config.target}'
    se_py = f'{sim_config.src}/{sim_config.script}'

    test_suite = f'${{CMAKE_BINARY_DIR}}/sw/{name}/build'
    
    with open(os.path.join(dir, 'CMakeLists.txt'), 'w') as f:
        content = f'''
make_directory(${{CMAKE_CURRENT_BINARY_DIR}}/stamp)
add_custom_command(
  OUTPUT stamp/clean
  COMMAND rm -rf cpt
  COMMAND touch stamp/clean
  DEPENDS {gem5_exe} {se_py} {config.vars.benchspec}
)
add_custom_command(
  OUTPUT stamp/run
  COMMAND python3 {config.vars.llsct}/bench/create_checkpoints.py --benchspec {config.vars.benchspec} --test-suite {test_suite} --outdir cpt --llsct {config.vars.llsct} --gem5 {gem5_exe} --se {se_py}
  COMMAND touch stamp/run
  DEPENDS {gem5_exe} {se_py} {config.vars.benchspec} stamp/clean
)
add_custom_target(cpt-{name} DEPENDS stamp/run)
'''
        f.write(content)
        
def generate_experiments_cmakelist(config):
    with open('exp/CMakeLists.txt', 'w') as f:
        for name in config.experiments:
            f.write(f'add_subdirectory({name})\n')
        
def generate_exp_subdir(exp_name, config):
    exp = config.experiments[exp_name]
    sw = config.sw[exp.sw]
    sim = config.sim[exp.sim]
    hwmode = config.hwmode[exp.hwmode]
    vars = config.vars

    with open(f'exp/{exp_name}/CMakeLists.txt', 'w') as f:
        test_suite = f'${{CMAKE_BINARY_DIR}}/sw/{exp.sw}/build'
        gem5_exe = f'${{CMAKE_BINARY_DIR}}/sim/{exp.sim}/{sim.target}'
        script_py = f'{sim.src}/{sim.script}'
        
        content = f'''
make_directory(stamp)

add_custom_command(
  OUTPUT cpt/stamp
  COMMAND python3 {vars.llsct}/bench/create_checkpoints.py --benchspec {vars.benchspec} --test-suite {test_suite} --outdir cpt --llsct {vars.llsct} --gem5 {gem5_exe} --se {script_py}'
  DEPENDS {vars.llsct}/bench/create_checkpoints.py {vars.benchspec} {test_suite} {gem5_exe} {script_py}
'''
        f.write(content)

generate_top_cmakelists()
os.makedirs('sw', exist_ok=True)
generate_intermediate_cmakelists('sw', config.sw)
for name, sw_config in config.sw.items():
    generate_sw_subdir(name, sw_config)

os.makedirs('sim', exist_ok=True)
generate_intermediate_cmakelists('sim', config.sim)
for name, sim_config in config.sim.items():
    generate_sim_subdir(name, sim_config)

os.makedirs('cpt', exist_ok=True)
generate_intermediate_cmakelists('cpt', config.sw)
for name in config.sw:
    generate_cpt_subdir(name, config)
    
# os.makedirs('exp', exist_ok=True)
# generate_experiments_cmakelists(config)
