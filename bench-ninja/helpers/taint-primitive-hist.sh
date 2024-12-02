#!/bin/bash

set -e

usage() {
    cat <<EOF
usage: $0 weights expdir
EOF
}

if [[ $# -ne 2 ]]; then
    usage >&2
    exit 1
fi

# $1 -- path to dbgout.txt.gz.
# $2 -- weight
histogram_trace() {
    gunzip -c "$1" | grep -o '^TPE [rmx]-taint 0x[[:xdigit:]]\+' | cut -d' ' -f2,3 | awk -vweight="$weight" '
{ hist[$0] += 1; }
END { for (line in hist) print int(hist[line] * weight), line; }
'
}

weights_path="$1"
expdir="$2"

pids=()
tmps=()
while read -r weight index; do
    tmp=`mktemp`
    tmps+=($tmp)
    histogram_trace $expdir/$index/m5out/dbgout.txt.gz $weight > $tmp &
done < "$weights_path"

# stdin: stuff to combine
histogram_combine() {
    awk '
{ hist[$2 " " $3] += $1; }
END { for (line in hist) print hist[line], line; }
'
}

wait
cat ${tmps[@]} | histogram_combine | sort -n -r
