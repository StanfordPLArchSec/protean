#!/bin/bash

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
iso="$root/cpu2017.iso"

cd "$root"
rm -rf cpu2017 cpu2017-install
mkdir cpu2017-install
cd cpu2017-install
bsdtar -xf "$iso"
chmod -R u+w .
./install.sh -f -d ../cpu2017
cd ..
rm -r cpu2017-install
