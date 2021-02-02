# build config

#
# handle policies

cmake_policy(SET CMP0077 NEW)

cmake_minimum_required(VERSION 3.7)

project(prodigy_compiler_support C CXX)

find_package(LLVM 11 REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(HandleLLVMOptions)
include(AddLLVM)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

add_definitions(${LLVM_DEFINITIONS})

include_directories(${LLVM_INCLUDE_DIRS})

include_directories(../sniper6.1/include/)

add_subdirectory(prefetcher)
add_subdirectory(runtime)
