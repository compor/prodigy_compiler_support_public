#!/usr/bin/env bash

# LLVM/Clang should be in the PATH for this to work
export CC=clang
export CXX=clang++
export LLVMCONFIG=llvm-config

#COMPILER_VERSION=

# or picked up from a system package install
if [[ ! -z ${COMPILER_VERSION} ]]; then
  export CC=${CC}-${COMPILER_VERSION}
  export CXX=${CXX}-${COMPILER_VERSION}
  export LLVMCONFIG=${LLVMCONFIG}-${COMPILER_VERSION}
fi

BUILD_TYPE=RelWithDebInfo
export BUILD_TYPE

SHARED_LIBS=ON
export SHARED_LIBS

CXX_FLAGS=
CXX_FLAGS="${CXX_FLAGS} -O1"
#CXX_FLAGS="${CXX_FLAGS} -stdlib=libc++"
CXX_FLAGS="${CXX_FLAGS} -pedantic -Wall -Wextra"
CXX_FLAGS="${CXX_FLAGS} -Wno-unused-parameter -Wno-unused-function"
export CXX_FLAGS

LINKER_FLAGS=
LINKER_FLAGS="${LINKER_FLAGS} -Wl,-L$(${LLVMCONFIG} --libdir)"
#LINKER_FLAGS="${LINKER_FLAGS} -lc++ -lc++abi"
export LINKER_FLAGS

# find LLVM's cmake dir
LLVM_DIR=$(${LLVMCONFIG} --cmakedir)
export LLVM_DIR

CMAKE_OPTIONS=""

CMAKE_OPTIONS="${CMAKE_OPTIONS} -DPCS_SNIPER_ROOT_DIR=FILLMEIN"

CMAKE_OPTIONS="${CMAKE_OPTIONS} -DLLVM_DIR=${LLVM_DIR}"

# CXX lang standard options
CMAKE_OPTIONS="${CMAKE_OPTIONS} -DCMAKE_CXX_STANDARD=14"
CMAKE_OPTIONS="${CMAKE_OPTIONS} -DCXX_STANDARD_REQUIRED=ON"
CMAKE_OPTIONS="${CMAKE_OPTIONS} -DCMAKE_CXX_EXTENSIONS=OFF"

export CMAKE_OPTIONS
