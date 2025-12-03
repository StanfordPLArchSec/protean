#!/bin/bash
#
# This script builds all 12 gem5 instances quickly using ccache and some preprocessor path tricks.

set -eu

verbose=
# verbose=--verbose
suffix=opt

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root"

scons="$(realpath "$root/gem5/scons")"

# Make sure we have ccache installed.
which ccache >/dev/null

# Don't hash directories, since we want to be able to get ccache hits when compiling
# different gem5 repos.
saved_ccache_conf="$(ccache --get-conf=hash_dir)"
ccache --set-conf=hash_dir=false
trap "ccache --set-conf=hash_dir=$saved_ccache_conf" EXIT

perf_gem5_srcs=(gem5/pincpu gem5/base gem5/base-se gem5/protean gem5/protean-se gem5/stt gem5/spt gem5/spt-se)
sec_gem5_srcs=(amulet/gem5/base amulet/gem5/protean amulet/gem5/stt amulet/gem5/spt)

build_gem5() {
    # # Skip building if it already exists.
    # if [[ -x $1/build/$2/gem5.opt ]]; then
    # 	echo "[*] skipping building $1/build/$2/gem5.opt (exists)"
    # 	return 0
    # fi

    target=$1/build/$2/gem5.$suffix

    if [[ -x $target ]]; then
	echo "[*] skipping building $target (exists)"
	return 0
    fi

    echo "[*] building $target"
    cd $1

    # Get Pin if needed.
    if [[ -x ext/pin/configure && ! -x ext/pin/pin ]]; then
	./ext/pin/configure
    fi

    # Set the build cache (optional).
    if [[ "$M5_BUILD_CACHE" ]]; then
	"$scons" setconfig build/$2 M5_BUILD_CACHE=$M5_BUILD_CACHE
    fi

    # Build gem5.
    scons_opts=(
	# CPPFLAGS="-fmacro-prefix-map=$PWD=/gem5-build -ffile-prefix-map=$PWD=/gem5-build"
	$verbose
    )
    if ! "$scons" $target "${scons_opts[@]}"; then
	echo "[!] failed to build $target"
	exit 1
    fi

    # Remove intermediate .o files.
    find build -name '*.o' -exec rm {} \;

    # Strip binary.
    strip $target

    cd "$root"
}

for gem5_src in ${perf_gem5_srcs[@]}; do
    build_gem5 $gem5_src X86_MESI_Three_Level
done

for gem5_src in ${sec_gem5_srcs[@]}; do
    build_gem5 $gem5_src X86
done
