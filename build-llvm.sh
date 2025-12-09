#!/bin/bash
#
# This script builds LLVM.

set -eu

root="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
cd "$root"

if [[ -x llvm/build/bin/clang ]]; then
    echo "[*] clang exists, assuming LLVM already built" >&2
    exit 0
fi

echo "[*] building LLVM" >&2

cmake -G Ninja -S llvm/llvm -B llvm/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_ENABLE_PROJECTS='clang;flang' \
      -DLLVM_TARGETS_TO_BUILD='X86'
ninja -C llvm/build -k0 -j1
strip llvm/build/bin/* 2>/dev/null || true
strip llvm/build/lib/*.{a,so} 2>/dev/null || true
