#!/bin/bash

set -e
set -o pipefail
set -u

usage() {
    cat <<EOF
usage: $0 [-D arg]... <llsct-root> <test-dir> <name>
EOF
}

env=()
while getopts "hD:" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	D)
	    env+=("--env=$OPTARG")
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -ne 3 ]]; then
    usage >&2
    exit 1
fi

LLSCT=$(realpath $1)
DIR=$(realpath $2)
NAME=$3
LLVM_LIT=${LLSCT}/llvm/build/bin/llvm-lit

ipcs=$(realpath ipcs-$NAME.out)
rm -f $ipcs
cd ${DIR}/test-suite
find . -name "m5out-res-$NAME-*" -type d | while read -r RESULTS_DIR; do
    rm -r $RESULTS_DIR
done
cmake ${LLSCT}/test-suite -DTEST_SUITE_RUN_UNDER="python3 ${LLSCT}/bench/hw_run_under.py ${env[*]} --name=$NAME --llsct=$LLSCT --ipc=${ipcs} --"

run_llvm_lit() {
    no_verify=0
    if [[ "$1" = "--no-verify" ]]; then
	shift 1
	no_verify=1
    fi
    dir="$1"
    shift 1
    tests=()
    for TEST in $(find $dir -name '*.test'); do
	testname=$(basename $TEST .test)
	if ! grep -q $testname ${LLSCT}/bench/ignore.txt; then
	    tests+=($TEST)
	    if [[ $no_verify = 1 ]]; then
		sed -i '/^VERIFY:/d' $TEST
	    fi
	fi
    done
    ${LLVM_LIT} $@ ${tests[@]}
}

run_llvm_lit --no-verify . -vva
