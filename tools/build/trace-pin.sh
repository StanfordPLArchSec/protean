#!/bin/bash

setarch -R ../../pin/pin -t Test.so -x trace.out -H 0 -- ~/llsct2/.tmp/bug-gcc-ref/502.gcc_r/502.gcc_r ~/llsct2/.tmp/bug-gcc-ref/gcc-smaller.c -finline-limit=0 -o tmp.s 
