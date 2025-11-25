# paths
gem5_exe=../gem5/pincpu/build/X86_MESI_Three_Level/gem5.opt
gem5_config=../gem5/pincpu/configs/deprecated/example/se.py

pin_exe=../pin/pin
pinpoints_tool=../tools/build/libbbtrace.so

simpoint_exe=../simpoint/bin/simpoint

# benchmarks
mcf_exe=./mcf-sbox
mcf_args=(inp.in)

x264_exe=./x264-sbox
x264_args=(--dumpyuv 50 --frames 156 -o BuckBunny_New.264 BuckBunny.yuv 1280x720)

xz_exe=./xz-sbox
xz_args=(cpu2006docs.tar.xz 4 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 1548636 1555348 0)

benchmarks=(mcf x264 xz)

# config
interval=50000000
warmup=10000000
num_simpoints=10

# unsafe_args=(--cpu-type=X86O3CPU --caches --l1d_size=32kB --l1d_assoc=8 --l1i_size=32kB --l1i_assoc=8 --l2_size=256kB --l2_assoc=16 --l3_size=2mB --l3_assoc=16)
unsafe_exe=../gem5/base/build/X86_MESI_Three_Level/gem5.opt
unsafe_config=../gem5/base/configs/AlderLake/se.py
unsafe_args=()

tpe_exe=../gem5/tpe/build/X86_MESI_Three_Level/gem5.opt
tpe_config=../gem5/tpe/configs/AlderLake/se.py
tpe_args=(--tpe-mem --tpe-reg --tpe-xmit)

tpt_exe=../gem5/tpt/build/X86_MESI_Three_Level/gem5.opt
tpt_config=../gem5/tpt/configs/AlderLake/se.py
tpt_args=(--tpt --implicit-channel=Lazy --tpt-reg --tpt-mem --tpt-xmit --tpt-mode=YRoT)

stt_exe=../stt/build/X86_MESI_Three_Level/gem5.fast
stt_config=../stt/configs/AlderLake/se.py
stt_args=(--stt --implicit-channel=Lazy)

experiments=(unsafe tpe tpt stt)
