#!/bin/bash

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
iso="$root/cpu2017.iso"

if [[ ! -f "$iso" ]]; then
    echo "ERROR: missing $iso" >&2
    exit 1
fi

cd "$root"
rm -rf cpu2006 cpu2006-install
mkdir cpu2006-install
cd cpu2006-install
bsdtar -xf "$iso"
chmod -R u+w .
./install.sh -f -d ../cpu2006
cd ..
rm -r cpu2006-install
