#!/bin/bash

set -eu

make -C simpoint -j`nproc`
