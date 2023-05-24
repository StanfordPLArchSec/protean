#!/bin/bash

# Generate simpoints for an already-built+configured llvm-test-suite directory.

usage() {
    cat <<EOF
usage: $0 [-h] <test-suite-binary-dir>
EOF
}

script_path=$(realpath ${BASH_SOURCE[0]})
root_dir=$(dirname ${script_path})
llvm_dir=${root_dir}/llvm/build
llvm_lit=${llvm_dir}/bin/llvm-lit


while getopts "h" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))
if [[ $# -ne 1 ]]; then
    usage >&2
    exit 1
fi

test_suite_dir=$1

# Reconfigure to run under Profiling and Generating BBV
