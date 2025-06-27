#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Check if CMakeCache.txt exists in the current directory
if [ ! -f "CMakeLists.txt" ]; then
  echo "Error: CMakeCache.txt not found in the current directory."
  echo "Please run this script from your home directory."
  exit 1
fi

echo "Clearing build dir"
rm -rf build
mkdir build && cd build

echo "Running CMake..."
cmake -G Ninja .. -DLLVM_DIR=/usr/local/lib/cmake/llvm

echo "Building with Ninja..."
ninja

echo "Regenerating LLVM IR"
clang -Os -fno-vectorize -S -emit-llvm ../test/test_active.c -o ../test/test_active.ll

echo "Running custom memory ordering enforcement pass, as well as the standard instcombine and dce optimization passes.."
/usr/local/bin/opt -load-pass-plugin ./EnforceMemOrderPass.so -passes="EnforceMemOrderPass,instcombine,dce,loop-simplify" -S ../test/test_active.ll -o ../test/test_active_mem_enforced.ll

echo "Running DataflowGraph generation pass..."
/usr/local/bin/opt -load-pass-plugin ./DataflowGraph.so -passes=DataflowGraph -disable-output ../test/test_active_mem_enforced.ll

echo "Generating DFG image..."
dot -Tpng dfg.dot -o dfg.png

echo "Displaying DFG image"
# Using xdg-open is good for Linux. For macOS, 'open' is used.
# Let's add a simple check for the OS.
case "$(uname -s)" in
   Darwin)
     open dfg.png
     ;;
   Linux)
     xdg-open dfg.png
     ;;
   CYGWIN*|MSYS*|MINGW*)
     # Attempt to open for Windows-like environments (might need adjustment)
     cmd.exe /c start dfg.png
     ;;
   *)
     echo "Unsupported OS for automatic image display. Please open dfg.png manually."
     ;;
esac


echo "Process finished. DFG image saved as dfg.png"