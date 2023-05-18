#!/bin/bash

set -e
set -o pipefail

usage() {
    cat <<EOF
usage: $0 <llsct-root> <outdir>
EOF
}

if [[ $# -ne 2 ]]; then
    usage >&2
    exit 1
fi

LLSCT="$(realpath "$1")"
OUTDIR="$(realpath "$2")"
CLANG="${LLSCT}/llvm/build/bin/clang"
CLANGXX="${CLANG}++"
RUN_TYPE=test

if [[ -d "$OUTDIR" ]]; then
    echo "$0: output directory $OUTDIR already exists; resuming from previous build." >&2
fi

mkdir -p "$OUTDIR"
cd "$OUTDIR"

# <software>
#   - glibc
#   - test-suite
#   - run.sh # for test-suite

# Build glibc
if [[ ! -d glibc ]]; then
    mkdir glibc
    pushd glibc
    ${LLSCT}/glibc/configure \
	    --prefix=${OUTDIR} \
	    CC=${CLANG} \
	    CXX=${CLANGXX}
    make -s -j$(nproc)
    make -s -j$(nproc) install
    popd
fi

# Configure test-suite
if [[ ! -d test-suite ]]; then
    test_suite_flags=(-static -L ${OUTDIR}/lib -isystem ${OUTDIR}/include -Wl,--rpath=${OUTDIR}/lib -Wl,--dynamic-linker=${OUTDIR}/lib/ld-linux-x86-64.so.2)
    cmake -S ${LLSCT}/test-suite \
	  -B test-suite \
	  -DCMAKE_C_COMPILER=${CLANG} \
	  -DCMAKE_CXX_COMPILER=${CLANGXX} \
	  -DCMAKE_C_FLAGS="${test_suite_flags[*]}" \
	  -DCMAKE_CXX_FLAGS="${test_suite_flags[*]}" \
	  -DTEST_SUITE_SPEC2017_ROOT=${LLSCT}/cpu2017 \
	  -DTEST_SUITE_SUBDIRS=External \
	  -DTEST_SUITE_COLLECT_STATS=Off \
	  -DTEST_SUITE_COLLECT_CODE_SIZE=Off \
	  -DTEST_SUITE_RUN_TYPE=${RUN_TYPE} \
	  -DTEST_SUITE_RUN_UNDER=${LLSCT}/bench/sw_run_under.sh
fi
cd test-suite
cmake --build .

# Remove results from previous run.
find . -name 'm5out-*' -type d | while read -r dir; do
    rm -rf "${dir}"
done


# Remove ignored tests.
echo 'Removing ignored tests...' >&2
cat ${LLSCT}/bench/ignore.txt | while read IGNORE; do
    find . -name "${IGNORE}.test" -exec rm {} \;
done

${LLSCT}/llvm/build/bin/llvm-lit . -vva
