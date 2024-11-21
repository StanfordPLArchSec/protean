#!/bin/bash

set -e

. shared.sh

# build benchmarks
# build

# run benchmarks
for bench in ${benchmarks[@]}; do
    bench_exe=${bench}_exe
    bench_args="${bench}_args[*]"

    # native test
    echo "========== $bench native =========="
    ${!bench_exe} ${!bench_args}
    echo "==================================="

    # profile with pinpoints
    echo "========== $bench pinpoint =========="
    bbv=${PWD}/${bench}/bbv.out
    ${pin_exe} -t ${pinpoints_tool} -o ${bbv} -interval-size ${interval} -- \
	       ${!bench_exe} ${!bench_args}
    echo "====================================="

    # run simpoint
    echo "========== $bench simpoint =========="
    spt_out=${PWD}/${bench}/spt.out
    spt_wts=${PWD}/${bench}/spt.wts
    ${simpoint_exe} -loadFVFile ${bbv} -maxK ${num_simpoints} -saveSimpoints ${spt_out} -saveSimpointWeights ${spt_wts}
    echo "====================================="

    # create checkpoints in gem5
    echo "========== $bench checkpoint =========="
    cpt=${PWD}/${bench}/cpt
    rm -rf ${cpt}
    ${gem5_exe} --outdir=${cpt} ${gem5_config} \
		--cpu-type=X86KvmCPU --take-simpoint-checkpoints=${spt_out},${spt_wts},${interval},${warmup} \
		--cmd=${!bench_exe} --options="${!bench_args}"
    echo "======================================="

    # resume from checkpoints
    for core_type in pcore ecore; do
	for exp in ${experiments[@]}; do
	    exp_dir=${bench}/${core_type}/${exp}
	    exp_args="${exp}_args[*]"
	    rm -rf ${exp_dir}
	    mkdir -p ${exp_dir}
	    exp_gem5_exe=${exp}_exe
	    exp_gem5_config=${exp}_config
	    for checkpoint in ${bench}/cpt/cpt.simpoint_*; do
		cpt_name=$(basename ${checkpoint})
		cpt_id=$(grep -o 'simpoint_[[:digit:]]\+' <<< $cpt_name | grep -o -e '[1-9][0-9]*$' -e '0\+$')
		${!exp_gem5_exe} --outdir=${exp_dir}/${cpt_name} ${!exp_gem5_config} \
				 --cpu-type=X86O3CPU --caches \
				 --l1d_size=32kB --l1d_assoc=8 \
				 --l1i_size=32kB --l1i_assoc=8 \
				 --l2_size=256kB --l2_assoc=16 \
				 --l3_size=2mB --l3_assoc=16 \
				 --checkpoint-dir=$(dirname ${checkpoint}) \
				 --restore-simpoint-checkpoint \
				 --checkpoint-restore=$((cpt_id+1)) \
				 --ruby \
				 ${!exp_args} \
				 --cmd=${!bench_exe} \
				 --options="${!bench_args}" \
				 --${core_type} \
				 --enable-prefetch
	    done
	done
    done

done
