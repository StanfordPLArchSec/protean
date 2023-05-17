#!/bin/bash

usage() {
    cat <<EOF
usage: $0 <gem5-binary> <gem5-se-py> [gem5-se-py-arg...] -- command [arg...]
EOF
}


# run under host
run_host() {
    while [[ $# -gt 0 && "$1" != '--' ]]; do
	shift 1
    done
    shift 1
    if [[ $# -eq 0 ]]; then
	usage >&2
	exit 1
    fi
    "$@"
}

if [[ "$HOST" = "1" ]]; then
    run_host "$@"
    exit 
fi

if [[ $# -lt 4 ]]; then
    usage >&2
    exit 1
fi

gem5_bin="$1"
shift 1

gem5_args=()
while [[ $# -gt 0 && "$1" != "--" ]]; do
    gem5_args+=("$1")
    shift 1
done
if [[ $# -lt 2 ]]; then
    usage >&2
    exit 1
fi
shift 1

bin="$1"
shift 1

stdout="${PWD}/stdout"
stderr="${PWD}/stderr"
m5out=m5out-$(basename $(dirname $(pwd)))
"${gem5_bin}" --outdir=${m5out} "${gem5_args[@]}" --cmd="$bin" --options="$*" --output "${stdout}" --errout "${stderr}" > ${m5out}/simout 2> ${m5out}/simerr

cat_file() {
    if [[ -f "$1" ]]; then
	cat "$1"
    fi
}
cat_file "${stdout}"
cat_file "${stderr}" >&2
