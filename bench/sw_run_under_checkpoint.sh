#!/bin/bash

set -e
set -u
set -o pipefail
set -x

script_path=${BASH_SOURCE[0]}
bench_dir=$(dirname ${script_path})
LLSCT=$(dirname ${bench_dir})
GEM5_BIN=${LLSCT}/gem5/build/X86/gem5.fast
GEM5_SE_PY=${LLSCT}/gem5/configs/deprecated/example/se.py
BENCH=$(basename $(dirname ${PWD}))
MEM_SIZE=8589934592

CMD="$1"
shift 1

# 0. Identify which run we're on.
## Scan for existing run directories.
I=0
while [[ -d m5out-cpt-$I ]]; do
    ((++I))
done
echo "Starting checkpoint run ${I} for SPEC benchmark ${BENCH}" >&2


# 3. Taking SimPoint Checkpoints in gem5

M5OUT_BBV=m5out-bbv-$I
M5OUT_CPT=m5out-cpt-$I
${GEM5_BIN} --outdir=${M5OUT_CPT} ${GEM5_SE_PY} --take-simpoint-checkpoint=${M5OUT_BBV}/simpoint,${M5OUT_BBV}/weight,10000000,10000 \
	    --cpu-type=X86NonCachingSimpleCPU --mem-size=${MEM_SIZE} --cmd="${CMD}" --options="$*" >&2

echo "${BENCH}-${I}: found" >&2
# TODO: Create log for all these.
