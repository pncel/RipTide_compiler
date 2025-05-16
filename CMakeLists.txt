cmake_minimum_required(VERSION 3.13)
project(MyPass LANGUAGES CXX)

# Point to your custom LLVM install
set(LLVM_DIR "/usr/local/lib/cmake/llvm")
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
set(CMAKE_CXX_STANDARD 17 CACHE STRING "")

# Enable LLVM pass plugin system
include(AddLLVM)

add_llvm_pass_plugin(DataflowGraph
  compiler/DataflowGraph.cpp
)
