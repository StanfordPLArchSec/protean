#!/bin/bash

set -e
set -o pipefail
set -u

usage() {
    cat <<EOF
usage: $0 [-h] [-j <jobs>] <llsct-root> <outdir> [flag...]
EOF
}

num_jobs=$(nproc)
while getopts "hj:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	j)
	    num_jobs="$OPTARG"
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -lt 2 ]]; then
    usage >&2
    exit 1
fi
LLSCT="$(realpath "$1")"
OUTDIR="$(realpath "$2")"
shift 2
CLANG="${LLSCT}/llvm/build/bin/clang"
CLANGXX="${CLANG}++"
RUN_TYPE=test
LLVM_LIT=${LLSCT}/llvm/build/bin/llvm-lit

extra_flags=("$@")
base_flags=(-O3 -static)

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
	    CXX=${CLANGXX} \
	    CFLAGS="${base_flags[*]} ${extra_flags[*]}" \
	    CXXFLAGS="${base_flags[*]} ${extra_flags[*]}"
    make -s -j${num_jobs}
    make -s -j${num_jobs} install
    popd
fi
glibc_flags=(-L ${OUTDIR}/lib -isystem ${OUTDIR}/include -Wl,--rpath=${OUTDIR}/lib -Wl,--dynamic-linker=${OUTDIR}/lib/ld-linux-x86-64.so.2)

# Configure test-suite
if [[ ! -d test-suite ]]; then
    cmake -S ${LLSCT}/test-suite \
	  -B test-suite \
	  -DCMAKE_C_COMPILER=${CLANG} \
	  -DCMAKE_CXX_COMPILER=${CLANGXX} \
	  -DCMAKE_C_FLAGS="${base_flags[*]} ${extra_flags[*]} ${glibc_flags[*]}" \
	  -DCMAKE_CXX_FLAGS="${base_flags[*]} ${extra_flags[*]} ${glibc_flags[*]}" \
	  -DTEST_SUITE_SPEC2017_ROOT=${LLSCT}/cpu2017 \
	  -DTEST_SUITE_SUBDIRS=External \
	  -DTEST_SUITE_COLLECT_STATS=Off \
	  -DTEST_SUITE_COLLECT_CODE_SIZE=Off \
	  -DTEST_SUITE_RUN_TYPE=${RUN_TYPE}
else
    cmake -S ${LLSCT}/test-suite -B test-suite -UTEST_SUITE_RUN_UNDER
fi
cd test-suite
cmake --build . --parallel ${num_jobs}
