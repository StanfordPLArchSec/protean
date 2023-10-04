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

## copy-checkpoint -- srcdir, dst, dep
ninja.rule(
    name = 'copy-checkpoint',
    command = 'mkdir -p $dst/$i && if [ -d $src/$prefix* ]; then cp -r $src/$prefix* $dst/$i/; fi && touch $out',
    description = '($id) Copy checkpoint $i',
)

## generate-leaf-ipc
ninja.rule(
    name = 'generate-leaf-ipc',
    command = 'if [ -f $stats ]; then grep system.switch_cpus.ipc $stats | tail -1 | awk \'{print $$2}\'; else echo 0.0; fi > $out',
    description = '($id) Generating ipc.txt',
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

        for subdir in ['host', 'kvm', 'bbv', 'cpt']:
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
        se_py = os.path.join(sim_config.src, sim_config.script)
        sim_run_args = ' '.join(bench_spec.litargs)        
        verify_path = os.path.abspath(os.path.join('sw', sw_name, 'tools'))
        verify_rawcmds = ' && '.join(bench_spec.verify)
        verify_rawcmds = verify_rawcmds.replace('%b', verify_path).replace('%S', os.path.abspath(os.path.dirname(exe)))

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

        ## kvm
        kvm_subdir = os.path.join('cpt', sw_name, bench_name, 'kvm')
        
        ### kvm run
        kvm_m5_output = os.path.join(kvm_subdir, 'm5out', 'stats.txt')
        kvm_run_outputs = [os.path.join(kvm_subdir, output) for output in bench_spec.outputs]
        kvm_outputs = [kvm_m5_output, *kvm_run_outputs]
        assert len(kvm_outputs) >= 1
        kvm_run_cmd = [
            'cd', kvm_subdir, '&&',
            os.path.abspath(gem5_exe),
            se_py,
            '--cpu-type=X86KvmCPU',
            f'--mem-size={config.vars.memsize}',
            f'--options="{sim_run_args}"',
            f'--cmd={os.path.abspath(exe)}',
        ]
        if bench_spec.stdout:
            kvm_run_cmd.append(f'--output={os.path.abspath(os.path.join(kvm_subdir, bench_spec.stdout))}')
        ninja.build(
            outputs = kvm_outputs,
            rule = 'custom-command',
            inputs = [
                exe,
                os.path.join(kvm_subdir, 'copy.stamp'),
                os.path.join(host_subdir, 'verify.stamp'),
            ],
            variables = {
                'cmd': ' '.join(kvm_run_cmd),
                'id': f'{sw_name}->{bench_name}->kvm->run',
                'desc': 'Run under KVM',
            },
        )

        ### kvm verify
        kvm_verify_stamp = os.path.join(kvm_subdir, 'verify.stamp')
        kvm_verify_cmd = f'cd {kvm_subdir} && {verify_rawcmds}'
        ninja.build(
            outputs = kvm_verify_stamp,
            rule = 'stamped-custom-command',
            inputs = kvm_run_outputs,
            variables = {
                'cmd': kvm_verify_cmd,
                'stamp': 'verify.stamp',
                'id': f'{sw_name}->{bench_name}->kvm->verify',
                'desc': 'Verify KVM benchmark results',
            },
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
            '--log-file=valout',
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
        
        ## checkpoints
        cpt_subdir = os.path.join('cpt', sw_name, bench_name, 'cpt')

        ### checkpoint run/collect
        cpt_run_outputs = [os.path.join(cpt_subdir, output) for output in bench_spec.outputs]
        cpt_m5_output = os.path.join(cpt_subdir, 'm5out', 'stats.txt')
        cpt_outputs = [cpt_m5_output, *cpt_run_outputs]
        cpt_run_cmd = [
            'cd', cpt_subdir, '&&',
            'rm -r m5out &&',
            os.path.abspath(gem5_exe),
            se_py,
            '--cpu-type=X86KvmCPU',
            f'--mem-size={config.vars.memsize}',
            f'--take-simpoint-checkpoint={os.path.abspath(spt_simpoints)},{os.path.abspath(spt_weights)},{config.vars.interval},{config.vars.warmup}',
            f'--options="{sim_run_args}"',
            f'--cmd={os.path.abspath(exe)}',
        ]
        if bench_spec.stdout:
            cpt_run_cmd.append(f'--output={os.path.abspath(os.path.join(cpt_subdir, bench_spec.stdout))}')
        ninja.build(
            outputs = cpt_outputs,
            rule = 'custom-command',
            inputs = [
                exe,
                bbv_verify_stamp,
                os.path.join('cpt', sw_name, bench_name, 'cpt', 'copy.stamp'),
                *spt_outputs,
                kvm_verify_stamp,
            ],
            variables = {
                'cmd': ' '.join(cpt_run_cmd),
                'id': f'{sw_name}->{bench_name}->cpt',
                'desc': 'Take checkpoints',
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
        #         'cmd': cpt_verify_cmd,
        #         'stamp': 'verify.stamp',
        #         'id': f'{sw_name}->{bench_name}->cpt->verify',
        #         'desc': 'Verify checkpoint results',
        #     },
        # )

        # checkpoint validation -- ensure we got the right number of checkpoints
        cpt_count_stamp = os.path.join(cpt_subdir, 'count.stamp')
        ninja.build(
            outputs = cpt_count_stamp,
            rule = 'stamped-custom-command',
            inputs = [
                spt_simpoints,
                cpt_m5_output
            ],
            variables = {
                'stamp': cpt_count_stamp,
                'cmd': f'[ $$(wc -l < {spt_simpoints}) -eq $$(echo {cpt_subdir}/m5out/cpt.simpoint_* | wc -w) ]',
                'id': f'{sw_name}->{bench_name}->cpt',
                'desc': 'Validating checkpoint counts'
            }
        )
        
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


# run experiments
for exp_name, exp_config in config.exp.items():
    exp_dir = os.path.join('exp', exp_name)
    sw_name = exp_config.sw
    sw_config = config.sw[sw_name]
    hw_name = exp_config.hwmode
    hw_config = config.hwmode[hw_name]
    sim_name = exp_config.sim
    sim_config = config.sim[sim_name]
    gem5_exe = os.path.join('sim', sim_name, sim_config.target)
    se_py = os.path.join(sim_config.src, sim_config.script)

    for bench_name, bench_spec in benchspec.items():
        bench_dir = os.path.join('sw', sw_name, 'External', 'SPEC', 'CINT2017speed', bench_name)
        bench_exe = os.path.join(bench_dir, bench_name)
        rsrc_dir = os.path.join(bench_dir, f'run_{sw_config.test_suite_run_type}')
        cpt_dir = os.path.join('cpt', sw_name, bench_name, 'cpt', 'm5out')
        sim_run_args = ' '.join(bench_spec.litargs)

        for cpt_idx in range(config.vars.num_simpoints):
            subdir = os.path.join(exp_dir, bench_name, str(cpt_idx))
            copy_stamp = os.path.join(subdir, 'copy.stamp')

            # Copy resource dir
            ninja.build(
                outputs = copy_stamp,
                rule = 'copy-resource-dir',
                inputs = bench_exe,
                variables = {
                    'src': rsrc_dir,
                    'dst': subdir,
                    'stamp': copy_stamp,
                    'id': f'exp->{exp_name}->copy',
                },
            )
            
            # Resume from checkpoint
            stats = os.path.join(subdir, 'm5out', 'stats.txt')
            run_stamp = os.path.join(subdir, 'run.stamp')
            exp_run_cmd = [
                f'if [ -d {cpt_dir}/cpt.simpoint_{cpt_idx:02}_* ]; then '
                f'cd {subdir} && rm -rf m5out &&',
                os.path.abspath(gem5_exe),
                *hw_config.gem5_opts,
                se_py,
                f'--cmd={os.path.abspath(bench_exe)}',
                f'--options="{sim_run_args}"',
                '--cpu-type=X86O3CPU',
                f'--mem-size={config.vars.memsize}',
                '--caches',
                '--l1d_size=64kB',
                '--l1i_size=16kB',
                f'--checkpoint-dir={os.path.abspath(cpt_dir)}',
                '--restore-simpoint-checkpoint',
                f'--checkpoint-restore={cpt_idx + 1}',
                *hw_config.script_opts,
                f'; fi && touch run.stamp'
            ]

            ninja.build(
                outputs = run_stamp,
                rule = 'custom-command',
                inputs = [
                    gem5_exe,
                    se_py,
                    bench_exe,
                    copy_stamp,
                    os.path.join(cpt_dir, 'stats.txt'),
                    os.path.join(cpt_dir, '..', 'count.stamp'),
                ],
                variables = {
                    'cmd': ' '.join(exp_run_cmd),
                    'id': f'exp->{exp_name}->{bench_name}->{cpt_idx}',
                    'desc': 'Resume from checkpoints',
                },
            )

            # Generate ipc.txt
            ninja.build(
                outputs = os.path.join(subdir, 'ipc.txt'),
                rule = 'generate-leaf-ipc',
                inputs = run_stamp,
                variables = {'stats': stats},
            )

        # Generate ipc.txt for benchmark
        generate_bench_ipc_py = os.path.join(os.path.dirname(__file__), 'helpers', 'bench-ipc.py')
        generate_bench_ipc_inputs = [os.path.join('exp', exp_name, bench_name, str(cpt_idx), 'ipc.txt') for cpt_idx in range(config.vars.num_simpoints)]
        generate_bench_ipc_output = os.path.join('exp', exp_name, bench_name, 'ipc.txt')
        generate_bench_ipc_cmd = ['python3', generate_bench_ipc_py, cpt_dir, *generate_bench_ipc_inputs, '>', generate_bench_ipc_output]
        ninja.build(
            outputs = generate_bench_ipc_output,
            rule = 'custom-command',
            inputs = [
                *generate_bench_ipc_inputs,
                generate_bench_ipc_py,
                os.path.join(cpt_dir, 'stats.txt'),
            ],
            variables = {
                'cmd': ' '.join(generate_bench_ipc_cmd),
                'id': f'exp->{exp_name}->{bench_name}->ipc',
                'desc': 'Generating benchmark ipc.txt',
            },
        )
