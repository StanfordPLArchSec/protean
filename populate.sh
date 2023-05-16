#!/bin/bash

set -e
set -o pipefail

usage() {
    cat <<EOF
usage: $0 [-h] [--shallow] [<rootdir>="."]
EOF
}

git_clone_flags=()
while [[ $# -gt 0 && "$1" ~= ^- ]]; do
    optc="$1"
    shift 1
    case "${optc}" in
	-h)
	    usage
	    exit
	    ;;
	--shallow)
	    git_clone_flags+=("--depth=1")
	    ;;
	--)
	    break
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done

root="."
case $# in
    0) ;;
    1)
	root="$1"
	shift 1
	;;
    *)
	usage >&2
	exit 1
	;;
esac
root="$(realpath "${root}")"

should_clone() {
    dir="$1"
    shift 1
    if [[ -f "${dir}" ]]; then
	echo "${dir} is a file!" >&2
	exit 1
    elif [[ -d "${dir}" ]]; then
	echo "Skipping ${dir} since it already exists" >&2
	return 1
    else
	echo "Cloning ${dir}..." >&2
	return 0
    fi
}

# gem5
if should_clone gem5; then
    git clone "${git_clone_flags[@]}" git@github.com:StanfordPLArchSec/llsct2-gem5.git gem5
fi

# llvm
if should_clone llvm; then
    git clone "${git_clone_flags[@]}" git@github.com:StanfordPLArchSec/llsct2-llvm.git llvm
fi

# passes
if should_clone passes; then
    git clone "${git_clone_flags[@]}" git@github.com:StanfordPLArchSec/llsct2-passes.git passes
fi

# glibc
if should_clone glibc; then
    mkdir glibc
    cd glibc
    wget -O- https://ftp.gnu.org/gnu/libc/glibc-2.37.tar.gz | tar -x --gzip --strip-components=1
fi

# test-suite
if should_clone test-suite; then
    git clone "${git_clone_flags[@]}" --depth=1 https://github.com/llvm/llvm-test-suite.git -b release/16.x test-suite
fi

echo 'Done!' >&2
