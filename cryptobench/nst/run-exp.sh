#!/bin/bash

set -eu

. shared.sh

log() {
    echo "$@" >&2
}

# run benchmarks
run_bench() {
    bench=$1
    bench_exe=${bench}_exe
    bench_args="${bench}_args[*]"

    log "RUN: $bench"

    cpt=${bench}/cpt

    # resume from checkpoints
    run_core() {
        core_type=$1

        log "RUN: $bench->$core_type"

        run_exp() {
            exp=$1

            log "RUN: $bench->$core_type->$exp"
            
	        exp_dir=${bench}/${core_type}/${exp}
	        exp_args="${exp}_args[*]"
	        rm -rf ${exp_dir}
	        mkdir -p ${exp_dir}
	        exp_gem5_exe=${exp}_exe
	        exp_gem5_config=${exp}_config

            run_checkpoint() {
                checkpoint=$1
		        cpt_name=$(basename ${checkpoint})
		        cpt_id=$(grep -o 'simpoint_[[:digit:]]\+' <<< $cpt_name | grep -o -e '[1-9][0-9]*$' -e '0\+$')

                log "RUN: $bench->$core_type->$exp->$cpt_id"
                
		        ${!exp_gem5_exe} --outdir=${exp_dir}/${cpt_name} ${!exp_gem5_config} \
				                 --cpu-type=X86O3CPU
				                 --checkpoint-dir=$(dirname ${checkpoint}) \
				                 --restore-simpoint-checkpoint \
				                 --checkpoint-restore=$((cpt_id+1)) \
				                 --ruby \
				                 ${!exp_args} \
				                 --cmd=${!bench_exe} \
				                 --options="${!bench_args}" \
				                 --${core_type} \
				                 --enable-prefetch

                log "DONE: $bench->$core_type->$exp->$cpt_id"
            }

            for checkpoint in ${cpt}/m5out/cpt.simpoint_*; do
                run_checkpoint $checkpoint &
            done
            wait

            log "DONE: $bench->$core_type->$exp"
	    }

        for exp in ${experiments[@]}; do
            run_exp $exp &
        done
        wait

        log "DONE: $bench->$core_type"
    }

    for core_type in pcore ecore; do
        run_core $core_type &
    done
    wait

    log "DONE: $bench"
}

for bench in ${benchmarks[@]}; do
    run_bench $1 &
done
wait

log "DONE"
