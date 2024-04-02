build() {
    make -C ctaes -j`nproc` || return 1
    make -C bearssl CONF=PTeX -j`nproc` || return 1
    (cd djbsort && ./build && ./test) || return 1
}

# paths
gem5_exe=../bench-ninja/sim/base/build/X86/gem5.opt
gem5_config=../gem5/configs/deprecated/example/se.py

pin_exe=../pin/pin
pinpoints_tool=../tools/build/libbbtrace.so

simpoint_exe=../simpoint/bin/simpoint

# benchmarks
ctaes_exe=ctaes/bench
ctaes_args=()

bearssl_exe=bearssl/build/testspeed
bearssl_args=(chacha20_ct)

djbsort_exe=djbsort/link-install/command/int32-speed
djbsort_args=()

benchmarks=(bearssl ctaes djbsort)

# config
interval=10000000
warmup=50000000
num_simpoints=10

# unsafe_args=(--cpu-type=X86O3CPU --caches --l1d_size=32kB --l1d_assoc=8 --l1i_size=32kB --l1i_assoc=8 --l2_size=256kB --l2_assoc=16 --l3_size=2mB --l3_assoc=16)
unsafe_exe=../bench-ninja/sim/base/build/X86/gem5.opt
unsafe_config=../gem5-spec2017/configs/deprecated/example/se.py
unsafe_args=()

tpe_exe=../bench-ninja/sim/llsct/build/X86/gem5.opt
tpe_config=../gem5/configs/deprecated/example/se.py
tpe_args=(--tpe-mem=1 --tpe-reg=1 --llsct-declassify-impl=cache --llsct-declassify-lines=1024)

experiments=(unsafe tpe)
