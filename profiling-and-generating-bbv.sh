#!/bin/bash

script_path=$(realpath ${BASH_SOURCE[0]})
root_dir=$(dirname ${script_path})

gem5_bin=${root_dir}/gem5/build/X86/gem5.fast
se_py=${root_dir}/gem5/configs/deprecated/exmaple/se.py
