#!/bin/bash

set -eu

. shared.sh

# build benchmarks
# build

renumber_simpoints() {
    awk '
BEGIN {
  i = 0;
}
{
  print $1, i;
  i += 1;
}
'
}

# run benchmarks
for bench in ${benchmarks[@]}; do
    bench_exe=${bench}_exe
    bench_args="${bench}_args[*]"

    # Make frontend results directories.
    spt=${PWD}/${bench}/spt
    cpt=${PWD}/${bench}/cpt
    for dir in $bbv $spt $cpt; do
        rm -rf $dir
        mkdir -p $dir
    done

    # native test
    echo "========== $bench native =========="
    ${!bench_exe} ${!bench_args}
    echo "==================================="

    # profile with pinpoints
    echo "========== $bench pinpoint =========="
    bbv_out=../base/${bench}/bbv.out.gz
    echo "====================================="

    # run simpoint
    echo "========== $bench simpoint =========="
    spt_out=../base/${bench}/spt/simpoints.out
    spt_wts=../base/${bench}/spt/weights.out
    echo "====================================="

    # run f2i
    echo "======== $bench f2i =========="
    # [7/10] simpoints.py --simpoints /home/nmosier/llsct2/bench-ninja/cpt/base/600.simpoints.out --weights /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/weights.out --bbv /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/bbv/bbv.out.gz --funcs /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/funcs.out --insts /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/insts.out --output /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/cpt/simpoints.json -- /home/nmosier/llsct2/bench-ninja/sim/pincpu/build/X86_MESI_Three_Level/gem5.opt -r -e /home/nmosier/llsct2/gem5/pincpu/configs/pin-f2i.py --pin /home/nmosier/llsct2/pin/pin --pin-kernel /home/nmosier/llsct2/gem5/pincpu/pintool/build/kernel --pin-tool /home/nmosier/llsct2/gem5/pincpu/pintool/build/libclient.so --f2i-input /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/funcs.out --f2i-output /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/insts.out --mem-size 1GB --max-stack-size 8MB --symbol-blacklist /dev/null -- -- /home/nmosier/llsct2/bench-ninja/sw/ptex-nst/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out > stdout 2> stderr
    ${simpoints_py} --simpoints=${spt_out} --weights=${spt_wts} --bbv=${bbv_out} --funcs=${spt}/funcs.out --insts=${spt}/insts.out --output=${cpt}/simpoints.json -- \
                    ${gem5_exe} --outdir=${spt}/m5out -r -e ${gem5_dir}/configs/pin-f2i.py --pin=${pin_exe} --pin-kernel=${gem5_dir}/pintool/build/kernel --pin-tool=${gem5_dir}/pintool/build/libclient.so \
                    --f2i-input=${spt}/funcs.out --f2i-output=${spt}/insts.out --symbol-blacklist=/dev/null -- -- \
                    ${!bench_exe} ${!bench_args}
    echo "=============================="
    
    # create checkpoints in gem5
    echo "========== $bench checkpoint =========="
    ${gem5_exe} --outdir=${cpt} ${gem5_config} \
		        --cpu-type=X86KvmCPU --take-simpoint-checkpoints=${spt_out},${spt_wts},${interval},${warmup} \
		        --cmd=${!bench_exe} --options="${!bench_args}"
    # taskset --cpu-list 0-15 gem5.opt -r -e configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --mem-size=1GB --max-stack-size=8MB --simpoints-json=cpt/simpoints.json --simpoints-warmup=10000000 -- sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out && touch run.stamp
    taskset --cpu-list 0-15 \
            ${gem5_exe} --outdir=${cpt}/m5out ${gem5_dir}/configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --simpoints-json=${cpt}/simpoints.json --simpoints-warmup=${warmup} -- \
            ${!bench_exe} ${!bench_args}
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
                # m5.opt --debug-file=dbgout.txt.gz -r -e /home/nmosier/llsct2/gem5/base/configs/AlderLake/se.py --cmd=/home/nmosier/llsct2/bench-ninja/sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s --options="-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1" --cpu-type=X86O3CPU --mem-size=1GB --max-stack-size=8MB --checkpoint-dir=/home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/cpt/m5out --restore-simpoint-checkpoint --checkpoint-restore=1 --ruby --enable-prefetch --errout=stderr.txt --output=stdout.txt --pcore ; fi && touch run.stamp\
	        done
	    done
    done

done
