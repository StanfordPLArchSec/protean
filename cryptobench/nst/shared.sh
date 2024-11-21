build() {
    make -C ctaes -j`nproc` || return 1
    make -C bearssl CONF=PTeX -j`nproc` || return 1
    (cd djbsort && ./build && ./test) || return 1
}

# paths
gem5_exe=../../bench-ninja/sim/pincpu/build/X86_MESI_Three_Level/gem5.opt
gem5_dir=../../gem5/pincpu

pin_exe=../../pin/pin

simpoint_exe=../../simpoint/bin/simpoint
simpoints_py=../../bench-ninja/helpers/simpoints.py

# benchmarks
ctaes_exe=ctaes/bench
ctaes_args=()

bearssl_exe=bearssl/build/testspeed
bearssl_args=(chacha20_ct)

djbsort_exe=djbsort/link-install/command/int32-speed
djbsort_args=()

benchmarks=(bearssl ctaes djbsort)

# config
interval=50000000
warmup=10000000
num_simpoints=10

# experiments
## unsafe baseline
unsafe_exe=../../bench-ninja/sim/base/build/X86_MESI_Three_Level/gem5.opt
unsafe_config=../gem5/base/configs/AlderLake/se.py
unsafe_args=()

## TPT
tpt_exe=../../bench-ninja/sim/tpt/build/X86_MESI_Three_Level/gem5.opt
tpt_config=../../gem5/tpt/configs/AlderLake/se.py
tpt_args=(--tpt --implicit-channel=Lazy --tpt-reg --tpt-mem --tpt-xmit --tpt-mode=YRoT)

tpt_atret_exe=$tpt_exe
tpt_atret_config=$tpt_config
tpt_atret_args=(${tpt_args[@]} --speculation-model=AtRet)

tpt_ctrl_exe=$tpt_exe
tpt_ctrl_config=$tpt_config
tpt_ctrl_args=(${tpt_args[@]} --speculation-model=Ctrl)

## SPT
spt_exe=../../bench-ninja/sim/spt/build/X86_MESI_Three_Level/gem5.opt
spt_config=../../gem5/spt/configs/AlderLake/se.py
spt_args=(--spt --fwdUntaint=1 --bwdUntaint=1 --enableShadowL1=1)

spt_atret_exe=$spt_exe
spt_atret_config=$spt_config
spt_atret_args=(${spt_args[@]} --speculation-model=AtRet)

spt_ctrl_exe=$spt_exe
spt_ctrl_config=$spt_config
spt_ctrl_args=(${spt_args[@]} --speculation-model=AtRet)


experiments=(tpt_atret tpt_ctrl)
