#!/bin/bash

set -eu

# Privileged for --bind mounts, to speed up
# the gem5 build process.
docker run --platform=linux/amd64 -it \
       -v $PWD:/protean \
       -v $PWD/ccache:/ccache \
       -v $PWD/m5cache:/m5cache \
       --privileged \
       protean
