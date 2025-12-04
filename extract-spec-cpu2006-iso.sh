#!/bin/bash

set -eu

usage() {
    cat <<EOF
usage: $0 /path/to/cpu2017-1.1.0.iso
EOF
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 1
fi

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
iso="$(realpath "$1")"

cd "$root"
rm -rf cpu2006 cpu2006-install
mkdir cpu2006-install
cd cpu2006-install
bsdtar -xf "$iso"
chmod -R u+w .
./install.sh -f -d ../cpu2006
cd ..
rm -r cpu2006-install
