# build config

#
# handle policies

if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

cmake_minimum_required(VERSION 3.7)

project(prodigy_compiler_support C CXX)

# detect if project is within parent repo or standalone
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/../sniper6.1/)
  set(STANDALONE_BUILD TRUE)
endif()

if(STANDALONE_BUILD)
  if(NOT PCS_SNIPER_ROOT_DIR)
    message(FATAL_ERROR "Sniper root dir not provided via -DPCS_SNIPER_ROOT_DIR")
  endif()
else()
  set(PCS_SNIPER_ROOT_DIR "${CMAKE_SOURCE_DIR}/../sniper6.1/")
endif()

# current minimum LLVM version supported is 11
find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(HandleLLVMOptions)
include(AddLLVM)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

add_definitions(${LLVM_DEFINITIONS})

include_directories(${LLVM_INCLUDE_DIRS})

include_directories("${PCS_SNIPER_ROOT_DIR}/include/")

add_subdirectory(prefetcher)
add_subdirectory(runtime)
