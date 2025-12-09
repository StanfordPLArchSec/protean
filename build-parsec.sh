#!/bin/bash

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root"

# Prepare PARSEC.
# Some benchmarks fail on modern systems, unless you regenerate the ./configure
# scripts with autotools.

# Download the simsmall inputs.
url=https://github.com/cirosantilli/parsec-benchmark/releases/download/3.0/parsec-3.0-input-sim.tar.gz

wget -O- "$url" | tar -C parsec --gzip -x --strip-components=1
