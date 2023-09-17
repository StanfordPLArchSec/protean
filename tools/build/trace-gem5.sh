#!/bin/bash

GEM5=$HOME/llsct2/gem5

$GEM5/build/X86/gem5.opt --debug-flag=ExecEnable,ExecMacro,ExecUser --debug-file=dbgout $GEM5/configs/deprecated/example/se.py --cmd ~/llsct2/.tmp/bug-gcc-ref/502.gcc_r/502.gcc_r --options="$HOME/llsct2/.tmp/bug-gcc-ref/gcc-smaller.c -finline-limit=0 -o tmp.s"

awk '{print $3}' < m5out/dbgout > trace.ref

