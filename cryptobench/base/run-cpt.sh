#!/bin/bash

set -eu

. shared.sh

# build benchmarks
# build

log() {
    echo "$@" >&2
}

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
run_bench() {
    bench=$1
    bench_exe=${bench}_exe
    bench_args="${bench}_args[*]"
    echo "bench_args=$bench_args"

    # Make frontend results directories.
    host=${PWD}/${bench}/host
    bbv=${PWD}/${bench}/bbv
    spt=${PWD}/${bench}/spt
    cpt=${PWD}/${bench}/cpt
    for dir in $host $bbv $spt $cpt; do
        if [[ ! -f $dir/skip ]]; then
            rm -rf $dir
        fi
        mkdir -p $dir
    done

    # native test
    log "RUN: $bench native"
    ${!bench_exe} ${!bench_args} >${host}/stdout 2>${host}/stderr
    log "DONE: $bench native"
    
    # profile with pinpoints
    log "RUN: $bench bbv"
    if [[ -f ${bbv}/skip ]]; then
        echo '(skipped)'
    else
        bbv_out=${bbv}/bbv.out.gz
        # ${pin_exe} -t ${pinpoints_tool} -o ${bbv} -interval-size ${interval} -- \
	        #               ${!bench_exe} ${!bench_args}
        # gem5.opt -r -e /home/nmosier/llsct2/gem5/pincpu/configs/pin-bbv.py --pin /home/nmosier/llsct2/pin/pin --pin-kernel /home/nmosier/llsct2/gem5/pincpu/pintool/build/kernel --pin-tool /home/nmosier/llsct2/gem5/pincpu/pintool/build/libclient.so --interval-size 50000000 --mem-size 1GB --max-stack-size 8MB --output >(gzip > bbv.out.gz) --symbol-blacklist /dev/null -- /home/nmosier/llsct2/bench-ninja/sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out > stdout 2> stderr
        ${gem5_exe} --outdir=${bbv}/m5out -r -e ${gem5_dir}/configs/pin-bbv.py --pin ${pin_exe} --pin-kernel ${gem5_dir}/pintool/build/kernel --pin-tool ${gem5_dir}/pintool/build/libclient.so \
             --interval-size=${interval} --output >(gzip > ${bbv_out}) --symbol-blacklist=/dev/null -- ${!bench_exe} ${!bench_args} >${bbv}/stdout 2>${bbv}/stderr
    fi
    log "DONE: $bench bbv"

    # run simpoint
    log "RUN: $bench simpoint"
    if [[ -f ${spt}/skip ]]; then
        echo '(skipped)'
    else
        spt_out=${spt}/simpoints.out
        spt_wts=${spt}/weights.out
        ${simpoint_exe} -loadFVFile ${bbv_out} -maxK ${num_simpoints} -saveSimpoints >(renumber_simpoints > ${spt_out}) -saveSimpointWeights >(renumber_simpoints > ${spt_wts}) -inputVectorsGzipped \
                        >${spt}/stdout.0 2>${spt}/stderr.0
        # simpoint -loadFVFile cpt/base/600.perlbench_s/bbv/bbv.out.gz -maxK 10 -saveSimpoints >(awk 'BEGIN { i = 0; } { print $1, i; i += 1; }' > cpt/base/600.perlbench_s/spt/simpoints.out) -saveSimpointWeights >(awk 'BEGIN { i = 0; } { print $1, i; i += 1; }' > cpt/base/600.perlbench_s/spt/weights.out) > cpt/base/600.perlbench_s/spt/stdout 2> cpt/base/600.perlbench_s/spt/stderr -inputVectorsGzipped
    fi
    log "DONE: $bench simpoint"
        
    # run f2i
    log "RUN: $bench f2i"
    if [[ -f ${spt}/skip ]]; then
        echo '(skipped)'
    else
        # [5/27] simpoints.py --simpoints /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/simpoints.out --weights /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/weights.out --bbv /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/bbv/bbv.out.gz --funcs /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/funcs.out --insts /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/insts.out --output /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/cpt/simpoints.json -- /home/nmosier/llsct2/bench-ninja/sim/pincpu/build/X86_MESI_Three_Level/gem5.opt -r -e /home/nmosier/llsct2/gem5/pincpu/configs/pin-f2i.py --pin /home/nmosier/llsct2/pin/pin --pin-kernel /home/nmosier/llsct2/gem5/pincpu/pintool/build/kernel --pin-tool /home/nmosier/llsct2/gem5/pincpu/pintool/build/libclient.so --f2i-input /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/funcs.out --f2i-output /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/insts.out --mem-size 1GB --max-stack-size 8MB --symbol-blacklist /dev/null -- -- /home/nmosier/llsct2/bench-ninja/sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out > stdout 2> stderr
        ${simpoints_py} --simpoints=${spt_out} --weights=${spt_wts} --bbv=${bbv_out} --funcs=${spt}/funcs.out --insts=${spt}/insts.out --output=${cpt}/simpoints.json -- \
                        ${gem5_exe} --outdir=${spt}/m5out -r -e ${gem5_dir}/configs/pin-f2i.py --pin=${pin_exe} --pin-kernel=${gem5_dir}/pintool/build/kernel --pin-tool=${gem5_dir}/pintool/build/libclient.so \
                        --f2i-input=${spt}/funcs.out --f2i-output=${spt}/insts.out --symbol-blacklist=/dev/null -- -- \
                        ${!bench_exe} ${!bench_args} \
                        >${spt}/stdout.1 2>${spt}/stderr.1
    fi
    log "DONE: $bench f2i"
    
    # create checkpoints in gem5
    log "RUN: $bench checkpoint"
    if [[ -f ${cpt}/skip ]]; then
        echo '(skipped)'
    else
        # taskset --cpu-list 0-15 gem5.opt -r -e configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --mem-size=1GB --max-stack-size=8MB --simpoints-json=cpt/simpoints.json --simpoints-warmup=10000000 -- sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out && touch run.stamp
        taskset --cpu-list 0-15 \
                ${gem5_exe} --outdir=${cpt}/m5out ${gem5_dir}/configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --simpoints-json=${cpt}/simpoints.json --simpoints-warmup=${warmup} -- \
                ${!bench_exe} ${!bench_args} \
                >${cpt}/stdout 2>${cpt}/stderr
    fi
    log "DONE: $bench checkpoint"
}

for bench in ${benchmarks[@]}; do
    run_bench $bench &
done
wait

echo "DONE: $0"
