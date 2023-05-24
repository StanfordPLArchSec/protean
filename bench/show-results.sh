#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 [-a|-r] <results>...
EOF
}

mode=absolute
while getopts "har" optc; do
    case $optc in
	h)
	    usage
	    exit
	    ;;
	a)
	    mode=absolute
	    ;;
	r)
	    mode=relative
	    ;;
	*)
	    usage >&2
	    exit 1
	    ;;
    esac
done
shift $((OPTIND-1))

if [[ $# -eq 0 ]]; then
    usage >&2
    exit 1
fi

title=("bench")

acc=$(mktemp)
tmp=$(mktemp)
trap "rm -f $acc $tmp" EXIT

do_sort() {
    sort -t' ' -k1
}

do_sort < "$1" > "$acc"
title+=($(basename $1))
shift 1
for file in "$@"; do
    join -t' ' -j1 $acc <(do_sort < $file) > $tmp
    cp $tmp $acc
    title+=($(basename $file))
done

echo ${title[@]}
cat $acc

