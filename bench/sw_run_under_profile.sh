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
I=0
while [[ -d m5out-bbv-${I} ]]; do
    ((++I))
done
echo "Starting profiling (BBV) run ${I} for SPEC benchmark ${BENCH}" >&2

# 1. Profiling and Generating BBV.
## Here, we need to redirect the program's stdout to file and then cat it once we're done.
## We also redirect the programs simout.
M5OUT_BBV=m5out-bbv-${I}
STDOUT="${PWD}/stdout"
rm -f ${STDOUT}
rm -rf ${M5OUT_BBV}
if ! ${GEM5_BIN} --outdir=${M5OUT_BBV} ${GEM5_SE_PY} --cpu-type=X86NonCachingSimpleCPU --mem-size=${MEM_SIZE} --simpoint-profile --output=${STDOUT} >&2 \
     --cmd="${CMD}" --options="$*" ; then
    cat ${STDOUT}
    exit 1
fi
cat ${STDOUT}

## Check that we got simpoint.bb.gz
if [[ ! -f ${M5OUT_BBV}/simpoint.bb.gz ]]; then
    echo "${BENCH}-0: Profiling and generating fail to produce simpoint.bb.gz" >&2
    exit 1
fi

# 2. SimPoint Analysis
${LLSCT}/simpoint/bin/simpoint -loadFVFile ${M5OUT_BBV}/simpoint.bb.gz -maxK 30 -saveSimpoints ${M5OUT_BBV}/simpoint -saveSimpointWeights ${M5OUT_BBV}/weight -inputVectorsGzipped >&2
