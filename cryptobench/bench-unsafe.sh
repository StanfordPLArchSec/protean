#!/bin/bash

set -e

. shared.sh

# build benchmarks
build

# run benchmarks
for bench in ${benchmarks[@]}; do
    bench_exe=${bench}_exe
    bench_args=${bench}_args
    ${!bench_exe} "${!bench_args[@]}"
done
