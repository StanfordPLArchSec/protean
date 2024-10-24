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
parser.add_argument('--run-type', required=True, choices=['ref', 'train', 'test'])
parser.add_argument('--sim-depend', required=True, choices=[0,1], type=int)
parser.add_argument('--sw-depend', required=True, choices=[0,1], type=int)
parser.add_argument('--cpt-depend', required=True, choices=[0,1], type=int)
args = parser.parse_args()

def get_helper(*path_components):
    return os.path.join(os.path.dirname(__file__), 'helpers', *path_components)

with open(args.config) as f:
    config = json.load(f)
config = util.process_config(config, os.path.dirname(args.config))

with open(args.benchspec) as f:
    benchspec = json.load(f)
benchspec = util.process_benchspec(benchspec)

def get_mem(bench_name):
    if 'mem' in benchspec[bench_name].__dict__:
        return benchspec[bench_name].mem
    else:
        return config.vars.memsize

def get_ss(bench_name):
    if 'ss' in benchspec[bench_name].__dict__:
        return benchspec[bench_name].ss
    else:
        return config.vars.ss

f = open('build.ninja', 'w')
ninja = ninja_syntax.Writer(f)


# Define rules

## test-suite-build
test_suite_build_targets = ['fpcmp-target', 'imagevalidate_625-target', *benchspec]
test_suite_build_command = ['cmake', '--build', '$build']
for target in test_suite_build_targets:
    test_suite_build_command.extend(['--target', target])
test_suite_build_command.extend(['--', '--quiet'])
rule_test_suite_build = 'test-suite-build'
ninja.rule(
    name = rule_test_suite_build,
    command = ' '.join(test_suite_build_command),
    description = '($id) Build the LLVM test suite',
    restat = True,
)

## gem5-build
ninja.pool('sim', 1)
gem5_build_command = [ 'scons', '--quiet', '-C', '$src', '$out', '$sconsopts']
rule_gem5_build = 'gem5-build'
ninja.rule(
    name = rule_gem5_build,
    command = ' '.join(gem5_build_command),
    description = '($id) Build gem5',
    restat = True,
    pool = 'sim',
)
ninja.pool('mem', 100) # for 100 GB

## copy-resource-dir
## NOTE: We are just copying symbolic links now!!!!
ninja.rule(
    name = 'copy-resource-dir',
    command = 'rm -r $dst && mkdir -p $dst && rmdir $dst && cp -rs $$(realpath $src) $$(realpath $dst) && touch $stamp',
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

## restat'ed custom command -- cmd, id, desc
ninja.rule(
    name = 'restated-custom-command',
    command= '$cmd',
    description = '($id) $desc',
    restat = True,
)   

## generate-leaf-ipc
ninja.rule(
    name = 'generate-leaf-ipc',
    command = 'if [ -s $stats ]; then grep -F system.switch_cpus.ipc $stats | tail -1 | awk \'{print $$2}\'; else echo 0.0; fi > $out',
    description = '($id) Generating ipc.txt',
)

## generate-leaf-weight
ninja.rule(
    name = 'generate-leaf-weight',
    command = 'if [ $$(wc -l $weights) -gt $cpt ]; then head -$$(($cpt + 1)) | tail -1; else echo 0.0 > $out',
    description = '($id) Generating weight.txt',
)

## generate-leaf-insts
# FIXME: Combine with generate-leaf-ipc.
ninja.rule(
    name = 'generate-leaf-insts',
    command = 'if [ -s $stats ]; then grep -F system.switch_cpus.commitStats0.numInsts $stats | tail -1 | awk \'{print $$2}\'; else echo 0; fi > $out',
    description = '($id) Generating insts.txt',
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

    ## libc -- build LLVM's libc first
    libc_build_dir = os.path.join(build_dir, 'libc')

    ### Configure libc
    libc_configure_deps = [sw_config.cc, sw_config.cxx, *sw_config.deps]
    libc_configure_cflags = ' '.join(sw_config.cflags + sw_config.libc_cflags)
    libc_configure_ldflags = ' '.join(sw_config.ldflags)
    libc_configure_cmd = [
        'cmake', '-S', os.path.join(sw_config.llvm, 'llvm'), '-B', libc_build_dir,
        f'-DCMAKE_BUILD_TYPE={sw_config.cmake_build_type}',
        f'-DCMAKE_C_COMPILER={sw_config.cc}',
        f'-DCMAKE_CXX_COMPILER={sw_config.cxx}',
        f'-DCMAKE_C_FLAGS="{libc_configure_cflags}"',
        f'-DCMAKE_CXX_FLAGS="{libc_configure_cflags}"',
        f'-DLLVM_ENABLE_PROJECTS=libc',
        '&&', 'ninja', '--quiet', '-C', libc_build_dir, 'clean', # really, should just remove the directory
    ]
    libc_build_file = os.path.join(libc_build_dir, 'build.ninja')
    ninja.build(
        outputs = libc_build_file,
        rule = 'custom-command',
        inputs = libc_configure_deps,
        variables = {
            'cmd': ' '.join(libc_configure_cmd),
            'id': f'{sw_name}->libc->configure',
            'desc': 'Configure libc',
        }
    )
    ninja.build(
        outputs = os.path.join(libc_build_dir, 'configure'),
        rule = 'phony',
        inputs = libc_build_file
    )

    ### Build libc
    libc_build_cmd = ['ninja', '--quiet', '-C', libc_build_dir, 'llvmlibc']
    libc_library = os.path.join(libc_build_dir, 'projects', 'libc', 'lib', 'libllvmlibc.a')
    ninja.build(
        outputs = libc_library,
        rule = 'restated-custom-command',
        inputs = libc_build_file,
        variables = {
            'cmd': ' '.join(libc_build_cmd),
            'id': f'{sw_name}->libc->build',
            'desc': 'Build libc',
        }
    )
    ninja.build(
        outputs = os.path.join(libc_build_dir, 'build'),
        rule = 'phony',
        inputs = libc_library
    )

    ## libc++ -- build LLVM's libc++ and libc++abi
    libcxx_build_dir = os.path.join(build_dir, 'libcxx')

    ### Configure libcxx
    libcxx_configure_deps = [sw_config.cc, sw_config.cxx, *sw_config.deps]
    libcxx_configure_cflags = ' '.join(sw_config.cflags + sw_config.libcxx_cflags)
    libcxx_configure_ldflags = ' '.join(sw_config.ldflags)
    libcxx_configure_cmd = [
        'cmake', '-S', os.path.join(sw_config.llvm, 'runtimes'), '-B', libcxx_build_dir,
        f'-DCMAKE_BUILD_TYPE={sw_config.cmake_build_type}',
        f'-DCMAKE_C_COMPILER={sw_config.cc}',
        f'-DCMAKE_CXX_COMPILER={sw_config.cxx}',
        f'-DCMAKE_C_FLAGS="{libcxx_configure_cflags}"',
        f'-DCMAKE_CXX_FLAGS="{libcxx_configure_cflags}"',
        f'-DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi"',
        '&&', 'ninja', '--quiet', '-C', libcxx_build_dir, 'clean',
    ]
    libcxx_build_file = os.path.join(libcxx_build_dir, 'build.ninja')
    ninja.build(
        outputs = libcxx_build_file,
        rule = 'custom-command',
        inputs = libcxx_configure_deps,
        variables = {
            'cmd': ' '.join(libcxx_configure_cmd),
            'id': f'{sw_name}->libcxx->configure',
            'desc': 'Configure libcxx',
        }
    )
    ninja.build(
        outputs = os.path.join(libcxx_build_dir, 'configure'),
        rule = 'phony',
        inputs = libcxx_build_file
    )

    ### Build libcxx
    libcxx_libraries = [os.path.join(libcxx_build_dir, 'lib', f'lib{s}.a') for s in ['c++', 'c++abi']]
    libcxx_build_cmd = ['ninja', '-C', libcxx_build_dir, 'cxx', 'cxxabi']
    ninja.build(
        outputs = libcxx_libraries,
        rule = 'restated-custom-command',
        inputs = libcxx_build_file,
        variables = {
            'cmd': ' '.join(libcxx_build_cmd),
            'id': f'{sw_name}->libcxx->build',
            'desc': 'Build libcxx'
        }
    )
    ninja.build(
        outputs = os.path.join(libcxx_build_dir, 'build'),
        rule = 'phony',
        inputs = libcxx_libraries
    )
    libcxx_include_dir = os.path.realpath(os.path.join(libcxx_build_dir, 'include', 'c++', 'v1'))
    libcxx_library_dir = os.path.realpath(os.path.join(libcxx_build_dir, 'lib'))

    ## test-suite -- build LLVM's test suite
    ### Configure test-suite
    test_suite_cflags = [*sw_config.cflags, '-nostdinc++', '-nostdlib++',
                         '-isystem', libcxx_include_dir]
    test_suite_ldflags = [
        *sw_config.ldflags, # base
        '-L', os.path.realpath(os.path.dirname(libc_library)), '-lllvmlibc', # libc
        '-nostdlib++', '-L', libcxx_library_dir, '-lc++', '-lc++abi'] # libcxx
    test_suite_build_dir = os.path.join(build_dir, 'test-suite')
    test_suite_configure_deps = [sw_config.cc, sw_config.cxx, sw_config.fc, *sw_config.deps, libc_library, *libcxx_libraries]
    test_suite_build_file = os.path.join(test_suite_build_dir, 'build.ninja')
    test_suite_configure_cmd = [
        'cmake',
        '-S', sw_config.test_suite,
        '-B', test_suite_build_dir,
        f'-DCMAKE_C_COMPILER={sw_config.cc}',
        f'-DCMAKE_CXX_COMPILER={sw_config.cxx}',
        '-DCMAKE_CXX_STANDARD=11',
        f'-DCMAKE_BUILD_TYPE={sw_config.cmake_build_type}',
        '-DCMAKE_C_FLAGS="{}"'.format(' '.join(test_suite_cflags)),
        '-DCMAKE_CXX_FLAGS="{}"'.format(' '.join(test_suite_cflags)),
        '-DCMAKE_EXE_LINKER_FLAGS="{}"'.format(' '.join(test_suite_ldflags)),
        f'-DCMAKE_Fortran_COMPILER={sw_config.fc}',
        '-DCMAKE_Fortran_FLAGS="{}"'.format(' '.join(sw_config.fflags)),
        '-DTEST_SUITE_FORTRAN=1',
        '-DTEST_SUITE_SUBDIRS=External',
        '-DTEST_SUITE_SPEC2017_ROOT=' + sw_config.spec2017,
        '-DTEST_SUITE_RUN_TYPE=' + args.run_type,
        '&&', 'ninja', '--quiet', '-C', test_suite_build_dir, 'clean',
    ]
    ninja.build(
        outputs = test_suite_build_file,
        rule = 'custom-command',
        inputs = test_suite_configure_deps,
        variables = {
            'cmd': ' '.join(test_suite_configure_cmd),
            'id': f'{sw_name}->test-suite->configure',
            'desc': 'Configure test-suite',
        }
    )
    ninja.build(
        outputs = os.path.join(test_suite_build_dir, 'configure'),
        rule = 'phony',
        inputs = test_suite_build_file
    )

    ### Build test-suite 
    test_suite_build_outputs = [os.path.join(test_suite_build_dir, 'External', 'SPEC', f'C{benchspec[bench_name].type}2017speed', bench_name, bench_name) for bench_name in benchspec]
    test_suite_build_cmd = [
        'ninja', '--quiet', '-C', test_suite_build_dir, 'fpcmp-target', 'imagevalidate_625-target', *benchspec,
    ]
    ninja.build(
        outputs = test_suite_build_outputs,
        rule = 'restated-custom-command',
        inputs = test_suite_build_file,
        variables = {
            'cmd': ' '.join(test_suite_build_cmd),
            'id': f'{sw_name}->test-suite->build',
            'desc': 'Build test-suite',
        },
    )
    ninja.build(
        outputs = os.path.join(test_suite_build_dir, 'build'),
        rule = 'phony',
        inputs = test_suite_build_outputs
    )

    # 'all' target
    ninja.build(
        outputs = os.path.join(build_dir, 'all'),
        rule = 'phony',
        inputs = [
            os.path.join(libc_build_dir, 'build'),
            os.path.join(libcxx_build_dir, 'build'),
            os.path.join(test_suite_build_dir, 'build'),
        ]
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

    ninja.build(
        outputs = os.path.join(dir, 'all'),
        rule = 'phony',
        inputs = exe,
    )

# cpt builds
for sw_name, sw_config in config.sw.items():
    # all
    ninja.build(
        outputs = [os.path.join('cpt', sw_name, 'all')],
        rule = 'phony',
        inputs = [os.path.join('cpt', sw_name, bench_name, 'cpt', 'verify.stamp')
                  for bench_name in benchspec],
    )
    
    for bench_name, bench_spec in benchspec.items():
        bench_dir = os.path.join('sw', sw_name, 'test-suite', 'External', 'SPEC', f'C{bench_spec.type}2017speed', bench_name)
        exe = os.path.join(bench_dir, bench_name)
        rsrc_dir = os.path.join(bench_dir, f'run_{args.run_type}')

        for subdir in ['bbv', 'cpt']:
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

        # shared variables
        sim_name = config.vars.checkpoint_sim
        sim_config = config.sim[sim_name]
        gem5_exe = os.path.join('sim', sim_name, sim_config.target)
        se_kvm_py = os.path.join(sim_config.src, sim_config.script_kvm)
        sim_run_args = ' '.join(bench_spec.litargs)        
        verify_path = os.path.abspath(os.path.join('sw', sw_name, 'test-suite', 'tools'))
        verify_rawcmds = ' && '.join(bench_spec.verify)
        verify_rawcmds = verify_rawcmds.replace('%b', verify_path).replace('%S', os.path.abspath(os.path.dirname(exe)))

        # ## host
        # host_subdir = os.path.join('cpt', sw_name, bench_name, 'host')
        # ### host run
        # host_outputs = [os.path.join(host_subdir, output) for output in bench_spec.outputs]
        # if len(host_outputs) == 0:
        #     print(f'zero outputs for {host_subdir}', file=sys.stderr)
        # assert len(host_outputs) > 0
        # host_run_args = ' '.join(bench_spec.args)
        # host_run_cmd = f'cd {host_subdir} && {os.path.abspath(exe)} {host_run_args} 2> stderr'
        # if not bench_spec.stdout:
        #     host_run_cmd += ' > stdout'
        # 
        # ninja.build(
        #     outputs = host_outputs,
        #     rule = 'custom-command',
        #     inputs = [exe, os.path.join(host_subdir, 'copy.stamp')],
        #     variables = {
        #         'cmd': host_run_cmd,
        #         'id': f'{sw_name}->{bench_name}->host->run',
        #         'desc': 'Run benchmark on host',
        #         'pool': 'mem', 'weight': get_mem(bench_name),
        #     },
        # )
        # 
        # ### host verify
        # host_verify_stamp = os.path.join(host_subdir, 'verify.stamp')
        # host_verify_cmd = f'cd {host_subdir} && {verify_rawcmds}'
        # ninja.build(
        #     outputs = host_verify_stamp,
        #     rule = 'stamped-custom-command',
        #     inputs = host_outputs,
        #     variables = {
        #         'cmd': host_verify_cmd,
        #         'stamp': 'verify.stamp',
        #         'id': f'{sw_name}->{bench_name}->host->verify',
        #         'desc': 'Verify benchmark on host',
        #     }
        # )
        # 
        # ## kvm
        # kvm_subdir = os.path.join('cpt', sw_name, bench_name, 'kvm')
        
        ### kvm run
        # kvm_run_stamp = os.path.join(kvm_subdir, 'run.stamp')
        # kvm_run_outputs = [os.path.join(kvm_subdir, output) for output in bench_spec.outputs]
        # kvm_outputs = [kvm_run_stamp, *kvm_run_outputs]
        # assert len(kvm_outputs) >= 1
        # kvm_run_cmd = [
        #     'cd', kvm_subdir, '&&',
        #     'taskset', '--cpu-list', '0-15', # To avoid weird KVM issues.
        #     os.path.abspath(gem5_exe),
        #     '-r', '-e',
        #     # '--debug-flag=KvmRun,Kvm',
        #     se_py,
        #     '--cpu-type=X86KvmCPU',
        #     f'--mem-size={get_mem(bench_name)}',
        #     f'--max-stack-size={get_ss(bench_name)}',
        #     f'--options="{sim_run_args}"',
        #     f'--cmd={os.path.abspath(exe)}',
        #     f'--errout=stderr',
        # ]
        # if bench_spec.stdin:
        #     kvm_run_cmd.append(f'--input={os.path.abspath(os.path.join(kvm_subdir, bench_spec.stdin))}')
        # else:
        #     kvm_run_cmd.append(f'--input=/dev/null')
        # if bench_spec.stdout:
        #     kvm_run_cmd.append(f'--output={os.path.abspath(os.path.join(kvm_subdir, bench_spec.stdout))}')
        # else:
        #     kvm_run_cmd.append(f'--output=stdout')
        # inputs = [
        #     os.path.join(kvm_subdir, 'copy.stamp'),
        #     os.path.join(host_subdir, 'verify.stamp'),
        # ]
        # if args.sw_depend:
        #     inputs.append(exe)
        # if args.sim_depend:
        #     inputs.extend([gem5_exe, se_py])
        # ninja.build(
        #     outputs = kvm_outputs,
        #     rule = 'stamped-custom-command',
        #     inputs = inputs,
        #     variables = {
        #         'stamp': 'run.stamp',
        #         'cmd': ' '.join(kvm_run_cmd),
        #         'id': f'{sw_name}->{bench_name}->kvm->run',
        #         'desc': 'Run under KVM',
        #         'pool': 'mem', 'weight': get_mem(bench_name),
        #     },
        # )
        # 
        # ### kvm verify
        # kvm_verify_stamp = os.path.join(kvm_subdir, 'verify.stamp')
        # kvm_verify_cmd = f'cd {kvm_subdir} && {verify_rawcmds}'
        # ninja.build(
        #     outputs = kvm_verify_stamp,
        #     rule = 'stamped-custom-command',
        #     inputs = kvm_outputs,
        #     variables = {
        #         'cmd': kvm_verify_cmd,
        #         'stamp': 'verify.stamp',
        #         'id': f'{sw_name}->{bench_name}->kvm->verify',
        #         'desc': 'Verify KVM benchmark results',
        #     },
        # )

        
        ## bbv
        bbv_subdir = os.path.join('cpt', sw_name, bench_name, 'bbv')

        ### bbv run
        bbv_file = os.path.join(bbv_subdir, 'bbv.out.gz')
        bbv_run_outputs = [os.path.join(bbv_subdir, output) for output in bench_spec.outputs]
        bbv_outputs = [bbv_file, *bbv_run_outputs]
        assert len(bbv_outputs) >= 2
        bbv_script = os.path.join(sim_config.src, sim_config.script_bbv)
        bbv_pin_exe = config.vars.pin
        bbv_pin_tool = config.vars.pin_tool
        bbv_pin_kernel = config.vars.pin_kernel
        bbv_inputs = [gem5_exe, bbv_script, bbv_pin_exe, bbv_pin_tool, bbv_pin_kernel]
        bbv_run_args = ' '.join(bench_spec.args)
        bbv_run_cmd = [
            'cd', bbv_subdir, '&&',
            os.path.abspath(gem5_exe), '-r', '-e',
            os.path.abspath(bbv_script),
            '--pin', bbv_pin_exe,
            '--pin-kernel', bbv_pin_kernel,
            '--pin-tool', bbv_pin_tool,
            '--interval-size', str(config.vars.interval),
            '--output', '>(gzip > bbv.out.gz)',
            '--',
            os.path.abspath(exe),
            *bench_spec.args,
            '>', 'stdout', '2>', 'stderr',
            # config.vars.pin, '-t', config.vars.pinpoints, '-o', '>(gzip > bbv.out.gz)', '-interval-size', str(config.vars.interval),
            # '--', os.path.abspath(exe), *bench_spec.args,
            # '2>', 'stderr',
        ]
        
        if not bench_spec.stdout:
            bbv_run_cmd.extend(['>', 'stdout'])
        ninja.build(
            outputs = bbv_outputs,
            rule = 'custom-command',
            inputs = [
                exe,
                os.path.join('cpt', sw_name, bench_name, 'host', 'verify.stamp'),
                os.path.join('cpt', sw_name, bench_name, 'bbv', 'copy.stamp'),
                *bbv_inputs,
            ],
            variables = {
                'cmd': ' '.join(bbv_run_cmd),
                'id': f'{sw_name}->{bench_name}->bbv->run',
                'desc': 'Profile using Intel Pin',
                'pool': 'mem', 'weight': get_mem(bench_name),
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
        spt_cmd = [
            config.vars.simpoint, '-loadFVFile', bbv_file, '-maxK', str(config.vars.num_simpoints),
            '-saveSimpoints', spt_simpoints, '-saveSimpointWeights', spt_weights,
            '>', os.path.join(spt_subdir, 'stdout'),
            '2>', os.path.join(spt_subdir, 'stderr'),
            '-inputVectorsGzipped',
        ]

        ### simpoints: generate simpoints.out, weights.out
        if 'parent' not in sw_config.__dict__:
            ninja.build(
                outputs = [spt_simpoints, spt_weights],
                rule = 'custom-command',
                inputs = bbv_file,
                variables = {
                    'cmd': ' '.join(spt_cmd),
                    'id': f'{sw_name}->{bench_name}->spt',
                    'desc': 'Compute SimPoints',
                }
            )
        else:
            # Set spt_simpoints, spt_weights to be parent's.
            spt_parent_subdir = os.path.join('cpt', sw_config.parent, bench_name, 'spt')
            spt_simpoints = os.path.join(spt_parent_subdir, 'simpoints.out')
            spt_weights = os.path.join(spt_parent_subdir, 'weights.out')
            bbv_file = os.path.join('cpt', sw_config.parent, bench_name, 'bbv', 'bbv.out.gz')

        ## checkpoints
        cpt_subdir = os.path.join('cpt', sw_name, bench_name, 'cpt')

        # FIXME: Rejigger.
        ### simpoints: generate simpoints.json
        spt_json = os.path.join(cpt_subdir, 'simpoints.json')
        spt_json_py = get_helper('simpoints.py')
        spt_insts = os.path.abspath(os.path.join(spt_subdir, 'insts.out'))
        spt_funcs = os.path.abspath(os.path.join(spt_subdir, 'funcs.out'))
        f2i_script = os.path.join(sim_config.src, sim_config.script_f2i)
        f2i_pin_cmd = [
            os.path.abspath(gem5_exe), '-r', '-e',
            os.path.abspath(f2i_script),
            '--pin', config.vars.pin,
            '--pin-kernel', config.vars.pin_kernel,
            '--pin-tool', config.vars.pin_tool,
            '--f2i-input', spt_funcs,
            '--f2i-output', spt_insts,
            '--', '--', os.path.abspath(exe), *bench_spec.args,
            # '>', 'stdout', '2>', 'stderr',
        ]
        spt_json_cmd = [
            'cd', cpt_subdir, '&&',
            spt_json_py,
            '--simpoints', os.path.abspath(spt_simpoints),
            '--weights', os.path.abspath(spt_weights),
            '--bbv', os.path.abspath(bbv_file),
            '--funcs', os.path.abspath(os.path.join(spt_subdir, 'funcs.out')),
            '--insts', os.path.abspath(os.path.join(spt_subdir, 'insts.out')),
            '--output', os.path.abspath(spt_json),
            # '--', os.path.abspath(exe), *bench_spec.args,
            '--', *f2i_pin_cmd,
            '>', 'stdout', '2>', 'stderr',
        ]
        ninja.build(
            outputs = [spt_json],
            rule = 'custom-command',
            inputs = [spt_simpoints, spt_weights, spt_json_py, exe,
                      os.path.join(cpt_subdir, 'copy.stamp'), config.vars.pin,
                      config.vars.pin_kernel, config.vars.pin_tool, f2i_script,
                ],
            variables = {
                'cmd': ' '.join(spt_json_cmd),
                'id': f'{sw_name}->{bench_name}->spt->simpoints.json',
                'desc': 'Compute SimPoint JSON',
            },
        )
        spt_outputs = [spt_json]
        

        ### checkpoint run/collect
        # cpt_run_outputs = [os.path.join(cpt_subdir, output) for output in bench_spec.outputs]
        cpt_run_outputs = [] # FIXME: Remove.
        cpt_run_stamp = os.path.join(cpt_subdir, 'run.stamp')
        cpt_outputs = [cpt_run_stamp, *cpt_run_outputs]
        cpt_run_cmd = [
            'cd', cpt_subdir, '&&',
            'rm -rf m5out &&',
            'taskset', '--cpu-list', '0-15',
            os.path.abspath(gem5_exe),
            '-r', '-e', # redirect to simout and simerr
            se_kvm_py,
            '--cpu-type=X86KvmCPU',
            f'--mem-size={get_mem(bench_name)}',
            f'--max-stack-size={get_ss(bench_name)}',
            # f'--take-simpoint-checkpoint={os.path.abspath(spt_simpoints)},{os.path.abspath(spt_weights)},{config.vars.interval},{config.vars.warmup}',
            f'--simpoints-json={os.path.abspath(spt_json)}',
            f'--simpoints-warmup={config.vars.warmup}',
            '--', os.path.abspath(exe), *bench_spec.args,
        ]
        
        inputs = [
            spt_json,
            os.path.join('cpt', sw_name, bench_name, 'cpt', 'copy.stamp'),
        ]
        if args.sw_depend:
            inputs.append(exe)
        if args.sim_depend:
            inputs.extend([gem5_exe, se_kvm_py])

        ninja.build(
            outputs = cpt_outputs,
            rule = 'stamped-custom-command',
            inputs = inputs,
            variables = {
                'stamp': 'run.stamp',
                'cmd': ' '.join(cpt_run_cmd),
                'id': f'{sw_name}->{bench_name}->cpt',
                'desc': 'Take checkpoints',
                'pool': 'mem', 'weight': get_mem(bench_name),
            },
        )
        
        ### checkpoint verify
        # cpt_verify_stamp = os.path.join(cpt_subdir, 'verify.stamp')
        # cpt_verify_cmd = f'cd {cpt_subdir} && {verify_rawcmds}'
        # ninja.build(
        #     outputs = cpt_verify_stamp,
        #     rule = 'stamped-custom-command',
        #     inputs = cpt_run_outputs,
        #     variables = {
        #         # 'cmd': cpt_verify_cmd,
        #         'cmd': f'touch {cpt_verify_stamp}',
        #         'stamp': 'verify.stamp',
        #         'id': f'{sw_name}->{bench_name}->cpt->verify',
        #         'desc': 'Verify checkpoint results',
        #     },
        # )

        # checkpoint validation -- ensure we got the right number of checkpoints
        # cpt_count_stamp = os.path.join(cpt_subdir, 'count.stamp')
        # ninja.build(
        #     outputs = cpt_count_stamp,
        #     rule = 'stamped-custom-command',
        #     inputs = [
        #         spt_simpoints,
        #         *cpt_outputs,
        #     ],
        #     variables = {
        #         'stamp': cpt_count_stamp,
        #         'cmd': f'[ $$(wc -l < {spt_simpoints}) -eq $$(echo {cpt_subdir}/m5out/cpt.simpoint_* | wc -w) ]',
        #         'id': f'{sw_name}->{bench_name}->cpt',
        #         'desc': 'Validating checkpoint counts'
        #     }
        # )
        
        # NOTE: Need to copy checkpoints. We will create them directly under cpt/{sw_name}/{bench_name}/cpt.
        # Create phony checkpoints. These will required a DEPFILE.
        
        # for i in range(config.vars.num_simpoints):
        #     prefix = f'cpt.simpoint_{i:02}_'
        #     stamp = os.path.join(cpt_subdir, str(i), 'copy.stamp')
        #     ninja.build(
        #         outputs = stamp,
        #         rule = 'copy-checkpoint',
        #         inputs = cpt_m5_output,
        #         variables = {
        #             'stamp': stamp,
        #             'dst': cpt_subdir,
        #             'i': str(i),
        #             'src': os.path.join(cpt_subdir, 'm5out'),
        #             'id': f'{sw_name}->{bench_name}->cpt->{i}',
        #             'prefix': prefix,
        #         },
        #     )

        ## bench all
        cpt_subdir = os.path.join('cpt', sw_name, bench_name, 'cpt')
        ninja.build(
            outputs = os.path.join('cpt', sw_name, bench_name, 'all'),
            rule = 'phony',
            inputs = cpt_run_stamp,
        )
            


# run experiments
for core_type in ['pcore', 'ecore']:
    for bench_type in ['int', 'fp']:
        for exp_name, exp_config in config.exp.items():
            exp_dir = os.path.join('exp', core_type, bench_type, exp_name)
            sw_name = exp_config.sw
            sw_config = config.sw[sw_name]
            hw_name = exp_config.hwmode
            hw_config = config.hwmode[hw_name]
            sim_name = exp_config.sim
            sim_config = config.sim[sim_name]
            gem5_exe = os.path.join('sim', sim_name, sim_config.target)
            se_py = os.path.join(sim_config.src, sim_config.script_o3)

            for bench_name, bench_spec in benchspec.items():
                if bench_spec.type.lower() != bench_type:
                    continue
                bench_dir = os.path.join('sw', sw_name, 'test-suite', 'External', 'SPEC', f'C{bench_spec.type}2017speed', bench_name)
                bench_exe = os.path.join(bench_dir, bench_name)
                rsrc_dir = os.path.join(bench_dir, f'run_{args.run_type}')
                cpt_dir = os.path.join('cpt', sw_name, bench_name, 'cpt', 'm5out')
                sim_run_args = ' '.join(bench_spec.litargs)

                for cpt_idx in range(config.vars.num_simpoints):
                    subdir = os.path.join(exp_dir, bench_name, str(cpt_idx))
                    copy_stamp = os.path.join(subdir, 'copy.stamp')

                    # Copy resource dir
                    inputs = []
                    if args.sw_depend:
                        inputs.append(bench_exe)
                    ninja.build(
                        outputs = copy_stamp,
                        rule = 'copy-resource-dir',
                        inputs = inputs,
                        variables = {
                            'src': rsrc_dir,
                            'dst': subdir,
                            'stamp': copy_stamp,
                            'id': f'exp->{exp_name}->copy',
                        },
                    )

                    # Resume from checkpoint
                    run_stamp = os.path.join(subdir, 'run.stamp')
                    exp_run_cmd = [
                        f'if [ -d {cpt_dir}/cpt.simpoint_{cpt_idx:02}_* ]; then '
                        f'cd {subdir} && rm -rf m5out &&',
                        os.path.abspath(gem5_exe),
                        *hw_config.gem5_opts,
                        '-r', '-e',
                        se_py,
                        f'--cmd={os.path.abspath(bench_exe)}',
                        f'--options="{sim_run_args}"',
                        '--cpu-type=X86O3CPU',
                        f'--mem-size={get_mem(bench_name)}',
                        f'--max-stack-size={get_ss(bench_name)}',
                        f'--checkpoint-dir={os.path.abspath(cpt_dir)}',
                        '--restore-simpoint-checkpoint',
                        f'--checkpoint-restore={cpt_idx + 1}',
                        *hw_config.script_opts,
                        '--errout=stderr.txt',
                        '--output=stdout.txt',
                        f'--{core_type}',
                        f'; fi && touch run.stamp'
                    ]


                    inputs = [
                        copy_stamp,
                    ]
                    if args.cpt_depend:
                        inputs.append(os.path.join(cpt_dir, '..', 'run.stamp'))
                    if args.sw_depend:
                        inputs.append(bench_exe)
                    if args.sim_depend:
                        inputs.extend([gem5_exe, se_py])
                    ninja.build(
                        outputs = run_stamp,
                        rule = 'custom-command',
                        inputs = inputs,
                        variables = {
                            'cmd': ' '.join(exp_run_cmd),
                            'id': f'exp->{exp_name}->{bench_name}->{cpt_idx}',
                            'desc': 'Resume from checkpoints',
                            'pool': 'mem', 'weight': get_mem(bench_name),
                        },
                    )

                    # Generate ipc.txt
                    ninja.build(
                        outputs = os.path.join(subdir, 'ipc.txt'),
                        rule = 'generate-leaf-ipc',
                        inputs = run_stamp,
                        variables = {'stats': os.path.join(subdir, 'm5out', 'stats.txt') },
                    )

                    # Generate weight.txt
                    ninja.build(
                        outputs = os.path.join(subdir, 'weight.txt'),
                        rule = 'generate-leaf-weight',
                        inputs = run_stamp,
                        variables = {
                            'cpt': str(cpt_idx),
                            'weights': os.path.join('cpt', sw_name, bench_name, 'spt', 'weights.out'),
                            'id': f'exp->{exp_name}->{bench_name}->{cpt_idx}',
                        },
                    )

                    # Generate results.json
                    results_json = os.path.join(subdir, 'results.json')
                    generate_results_py = get_helper('generate-leaf-results.py')
                    stats_txt = os.path.join(subdir, 'm5out', 'stats.txt')
                    simpoints_json = os.path.join('cpt', sw_name, bench_name, 'cpt', 'simpoints.json')
                    # TODO: Add stats.txt as dependency?
                    results_json_cmd = [
                        generate_results_py,
                        '--stats', stats_txt,
                        '--simpoints-json', simpoints_json,
                        '--simpoint-idx', str(cpt_idx),
                        '--output', results_json,
                    ]
                    ninja.build(
                        outputs = [results_json],
                        rule = 'custom-command',
                        inputs = [generate_results_py, run_stamp, simpoints_json],
                        variables = {
                            'cmd': ' '.join(results_json_cmd),
                            'id': f'exp->{core_type}->{bench_type}->{exp_name}->{bench_name}->{cpt_idx}->results.json',
                            'desc': 'Generating results.json',
                        },
                    )

                    

                # Generate ipc.txt for benchmark
                generate_bench_ipc_py = get_helper('bench-ipc.py')
                generate_bench_ipc_inputs = [os.path.join(exp_dir, bench_name, str(cpt_idx), 'ipc.txt') for cpt_idx in range(config.vars.num_simpoints)]
                generate_bench_ipc_output = os.path.join(exp_dir, bench_name, 'ipc.txt')
                generate_bench_ipc_cmd = ['python3', generate_bench_ipc_py, cpt_dir, *generate_bench_ipc_inputs, '>', generate_bench_ipc_output]
                ninja.build(
                    outputs = generate_bench_ipc_output,
                    rule = 'custom-command',
                    inputs = [
                        *generate_bench_ipc_inputs,
                        generate_bench_ipc_py,
                    ],
                    variables = {
                        'cmd': ' '.join(generate_bench_ipc_cmd),
                        'id': f'exp->{exp_name}->{bench_name}->ipc',
                        'desc': 'Generating benchmark ipc.txt',
                    },
                )

                generate_bench_results_py = get_helper('generate-bench-results.py')
                leaf_result_jsons = [os.path.join(exp_dir, bench_name, str(cpt_idx), 'results.json') for cpt_idx in range(config.vars.num_simpoints)]
                bench_result_json = os.path.join(exp_dir, bench_name, 'results.json')
                ninja.build(
                    outputs = [bench_result_json],
                    rule = 'custom-command',
                    inputs = [generate_bench_results_py, *leaf_result_jsons],
                    variables = {
                        'cmd': ' '.join([generate_bench_results_py, '--output', bench_result_json, *leaf_result_jsons]),
                        'id': f'exp->{core_type}->{bench_type}->{bench_name}->results.json',
                    },
                )

            # Make phony 'all' target
            exp_all = []
            for bench_name in benchspec:
                if benchspec[bench_name].type.lower() != bench_type:
                    continue
                # FIXME: Remove ipc.txt
                for filename in ['ipc.txt', 'results.json']:
                    exp_all.append(os.path.join(exp_dir, bench_name, filename))
            ninja.build(
                outputs = os.path.join(exp_dir, 'all'),
                rule = 'phony',
                inputs = exp_all,
            )

# Make phony exp/{pcore,ecore}/all target
for core_type in ['pcore', 'ecore']:
    ninja.build(
        outputs = os.path.join('exp', core_type, 'all'),
        rule = 'phony',
        inputs = [os.path.join('exp', core_type, exp_name, 'all') for exp_name in config.exp]
    )

# Make phony 'exp/all' target
ninja.build(
    outputs = os.path.join('exp', 'all'),
    rule = 'phony',
    inputs = ['exp/pcore/all', 'exp/ecore/all']
)    
    
    


# Timed host runs
for sw_name, sw_config in config.sw.items():
    test_suite_build_dir = os.path.join('sw', sw_name, 'test-suite')
    llvm_lit_exe = os.path.join(os.path.dirname(sw_config.cc), 'llvm-lit')

    all_jsons = []
    
    for bench_name in benchspec:
        bench_dir = os.path.join(test_suite_build_dir, 'External', 'SPEC', f'C{benchspec[bench_name].type}2017speed', bench_name)
        json = os.path.join(bench_dir, f'{bench_name}.json')
        lit_cmd = ['taskset', '-c', '0', llvm_lit_exe, bench_dir, '-o', json]
        ninja.build(
            outputs = json,
            rule = 'custom-command',
            inputs = os.path.join(bench_dir, bench_name),
            variables = {
                'cmd': ' '.join(lit_cmd),
                'id': f'sw->{sw_name}->time',
                'desc': 'Timing benchmark on host',
            },
        )
        all_jsons.append(json)


    ninja.build(
        outputs = os.path.join('sw', sw_name, 'time'),
        rule = 'phony',
        inputs = all_jsons,
    )
