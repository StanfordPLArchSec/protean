#!/bin/bash

set -eu

. shared.sh

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

    # Make frontend results directories.
    host=${bench}/host
    spt=${bench}/spt
    cpt=${bench}/cpt
    for dir in $host $spt $cpt; do
        if [[ ! -f $dir/skip ]]; then
            rm -rf $dir
        fi
        mkdir -p $dir
    done

    # native test
    log "RUN: $bench native"
    ${!bench_exe} ${!bench_args}
    log "DONE: $bench native"

    # profile with pinpoints
    log "RUN: $bench bbv"
    bbv_out=../base/${bench}/bbv.out.gz
    [[ -f $bbv_out ]]
    log "DONE: $bench bbv"

    # run simpoint
    log "RUN: $bench simpoint"
    spt_out=../base/${bench}/spt/simpoints.out
    spt_wts=../base/${bench}/spt/weights.out
    log "DONE: $bench simpoint"

    # run f2i
    # [7/10] simpoints.py --simpoints /home/nmosier/llsct2/bench-ninja/cpt/base/600.simpoints.out --weights /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/spt/weights.out --bbv /home/nmosier/llsct2/bench-ninja/cpt/base/600.perlbench_s/bbv/bbv.out.gz --funcs /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/funcs.out --insts /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/insts.out --output /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/cpt/simpoints.json -- /home/nmosier/llsct2/bench-ninja/sim/pincpu/build/X86_MESI_Three_Level/gem5.opt -r -e /home/nmosier/llsct2/gem5/pincpu/configs/pin-f2i.py --pin /home/nmosier/llsct2/pin/pin --pin-kernel /home/nmosier/llsct2/gem5/pincpu/pintool/build/kernel --pin-tool /home/nmosier/llsct2/gem5/pincpu/pintool/build/libclient.so --f2i-input /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/funcs.out --f2i-output /home/nmosier/llsct2/bench-ninja/cpt/ptex-nst/600.perlbench_s/spt/insts.out --mem-size 1GB --max-stack-size 8MB --symbol-blacklist /dev/null -- -- /home/nmosier/llsct2/bench-ninja/sw/ptex-nst/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out > stdout 2> stderr
    if [[ -f ${spt}/skip ]]; then
        log "SKIP: $bench f2i"
    else
        log "RUN: $bench f2i"
        ${simpoints_py} --simpoints=${spt_out} --weights=${spt_wts} --bbv=${bbv_out} --funcs=${spt}/funcs.out --insts=${spt}/insts.out --output=${cpt}/simpoints.json -- \
                        ${gem5_exe} --outdir=${spt}/m5out -r -e ${gem5_dir}/configs/pin-f2i.py --pin=${pin_exe} --pin-kernel=${gem5_dir}/pintool/build/kernel --pin-tool=${gem5_dir}/pintool/build/libclient.so \
                        --f2i-input=${spt}/funcs.out --f2i-output=${spt}/insts.out --symbol-blacklist=/dev/null -- -- \
                        ${!bench_exe} ${!bench_args}
        log "DONE: $bench f2i"
    fi
    
    # create checkpoints in gem5
    if [[ -f ${cpt}/skip ]]; then
        log "SKIP: $bench checkpoint"
    else
        log "RUN: $bench checkpoint"
        # taskset --cpu-list 0-15 gem5.opt -r -e configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --mem-size=1GB --max-stack-size=8MB --simpoints-json=cpt/simpoints.json --simpoints-warmup=10000000 -- sw/base/test-suite/External/SPEC/CINT2017speed/600.perlbench_s/600.perlbench_s -I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1 > checkspam.2500.5.25.11.150.1.1.1.1.out && touch run.stamp
        taskset --cpu-list 0-15 \
                ${gem5_exe} --outdir=${cpt}/m5out ${gem5_dir}/configs/se-kvm-cpt.py --cpu-type=X86KvmCPU --simpoints-json=${cpt}/simpoints.json --simpoints-warmup=${warmup} -- \
                ${!bench_exe} ${!bench_args}
        log "DONE: $bench checkpoint"
    fi
}

log "RUN: $0"
for bench in ${benchmarks[@]}; do
    run_bench $bench &
done
wait
log "DONE: $0"
