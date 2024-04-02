#!/bin/bash

set -e
set -o pipefail
set -u

usage() {
    cat <<EOF
usage: $0 [-h] [-j <jobs>] [-l] [-p <pass>]... <llsct-root> <outdir>
EOF
}

num_jobs=$(nproc)
use_lto=0
passes=()
while getopts "hj:lp:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	j)
	    num_jobs="$OPTARG"
	    ;;
	l)
	    use_lto=1
	    ;;
	p)
	    passes+=("$OPTARG")
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -ne 2 ]]; then
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


if [[ -d "$OUTDIR" ]]; then
    echo "$0: output directory $OUTDIR already exists; resuming from previous build." >&2
fi

mkdir -p "$OUTDIR"
cd "$OUTDIR"


# Flags preprocessing
base_cflags=("-O3" "-mno-avx" "-mno-avx2")
base_ldflags=("-fuse-ld=lld" "-v")
if [[ ${use_lto} -ne 0 ]]; then
    base_cflags+=("-flto")
    base_ldflags+=("-flto")
fi
for pass in "${passes[@]}"; do
    if [[ ${use_lto} -eq 0 ]]; then
	base_cflags+=("-fpass-plugin=${LLSCT}/passes/build/lib${pass}.so")
    else
	base_ldflags+=("-Wl,--load-pass-plugin=${LLSCT}/passes/build/lib${pass}.so")
    fi
done


# Build glibc
mkdir -p glibc
pushd glibc
glibc_cflags=("${base_cflags[@]}")
glibc_ldflags=("${base_ldflags[@]}")
${LLSCT}/glibc/configure \
	--prefix=${OUTDIR} \
	CC=${CLANG} \
	CXX=${CLANGXX} \
	CFLAGS="${base_cflags[*]}" \
	CXXFLAGS="${base_cflags[*]}" \
	LDFLAGS="${base_ldflags[*]}"
make -s -j${num_jobs}
make -s -j${num_jobs} install
popd
glibc_flags=(-L ${OUTDIR}/lib -isystem ${OUTDIR}/include -Wl,--rpath=${OUTDIR}/lib -Wl,--dynamic-linker=${OUTDIR}/lib/ld-linux-x86-64.so.2)


# Configure test-suite
test_suite_cflags=("${base_cflags[@]}" -isystem "${OUTDIR}/include")
test_suite_ldflags=("${base_ldflags[@]}" -L "${OUTDIR}/lib" -Wl,--rpath="${OUTDIR}/lib" -Wl,--dynamic-linker="${OUTDIR}/lib/ld-linux-x86-64.so.2")
cmake -S ${LLSCT}/test-suite \
      -B test-suite \
      -DCMAKE_C_COMPILER="${CLANG}" \
      -DCMAKE_CXX_COMPILER="${CLANGXX}" \
      -DCMAKE_C_FLAGS="${test_suite_cflags[*]}" \
      -DCMAKE_CXX_FLAGS="${test_suite_cflags[*]}" \
      -DCMAKE_EXE_LINKER_FLAGS="${test_suite_ldflags[*]}" \
      -DCMAKE_SHARED_LINKER_FLAGS="${test_suite_ldflags[*]}" \
      -DTEST_SUITE_SPEC2017_ROOT=${LLSCT}/cpu2017 \
      -DTEST_SUITE_SUBDIRS=External \
      -DTEST_SUITE_COLLECT_STATS=Off \
      -DTEST_SUITE_COLLECT_CODE_SIZE=Off \
      -DTEST_SUITE_RUN_TYPE=${RUN_TYPE}
cd test-suite
cmake --build . --parallel ${num_jobs}
