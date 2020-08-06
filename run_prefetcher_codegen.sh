#!/bin/bash

clang++ -O0 test/array_2_func.c -o test_array_func
opt -S -load build/prefetcher/LLVMPrefetcher.so -prefetcher-codegen array_2_func -o bubbles.ll
clang++ -O0 bubbles.ll -Lbuild/runtime/dummy/ -lprefetcher_dummy_rt
