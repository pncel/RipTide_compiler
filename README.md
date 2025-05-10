# RipTide_compiler

### TODO:

* Use RIPTIDE semantics to build a control flow graph
  * Control flow operators (get offloaded to NoC):
    * Carry, invariant, T&F steer, stream , order, merge
* Don't just use register dependecies, use the semantics that the RipTide paper uses

## Getting started

**Build LLVM on Windows/Wsl for this project:**

Within WSL2 and the directory: /mnt/c/ 

```bash
sudo apt-get install ninja lld
git clone --depth 1 https://github.com/llvm/llvm-project.git
cd llvm-project
cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_USE_LINKER=lld -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_TARGETS_TO_BUILD= DLLVM_INCLUDE_TESTS=OFF
ninja -C build
cd build
sudo ninja install
```

**Run**

```bash
git clone https://github.com/pncel/RipTide_compiler.git
cd RipTide_compiler
mkdir build && cd build
cmake ..
make
opt -load-pass-plugin ./DataflowGraph.so -passes=dfg-pass -disable-output ../example/simple_ops_ir.ll
```

**To generate LLVM IR from Clang:**

```bash
$ clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```

### Compiler flow:

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

Translates C code into a dataflow graph with nodes representing custom ISA ops


