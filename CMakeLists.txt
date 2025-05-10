cmake_minimum_required(VERSION 3.13)
project(DataflowGraph LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)
enable_language(CXX)
enable_language(C)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
include(AddLLVM)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

add_llvm_pass_plugin(DataflowGraph
  compiler/DataflowGraph.cpp
)
