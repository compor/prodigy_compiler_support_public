cmake_minimum_required(VERSION 3.7)
project(hello-transformation)

find_package(LLVM 6.0.1 REQUIRED CONFIG)
list(APPEND  CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
include(AddLLVM)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(../sniper6.1/include/)

add_subdirectory(prefetcher)
add_subdirectory(runtime)
