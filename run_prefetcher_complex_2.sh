#!/bin/bash

clang++ -O0 test/array_complex_indirection_2.cpp -o test_array_func -c -emit-llvm
opt -S -load build/prefetcher/LLVMPrefetcher.so -prefetcher-codegen test_array_func -o bubbles.ll
clang++ -O0 bubbles.ll -Lbuild/runtime/dummy/ -lprefetcher_dummy_rt
