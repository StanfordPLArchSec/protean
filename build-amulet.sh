#!/bin/bash
#
# Simple script for building Amulet*'s LLVM sandboxing pass.
# Make sure to run ./build-llvm.sh first, since it depends on
# that.

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root/amulet"

LLVM_DIR="$root/llvm/build" cmake -G Ninja -S . -B build
ninja -C build

