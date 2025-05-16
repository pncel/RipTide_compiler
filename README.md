# RipTide_compiler

### TODO:

* Use RIPTIDE semantics to build a control flow graph
  * Control flow operators (get offloaded to NoC):
    * Carry, invariant, T&F steer, stream , order, merge
* Don't just use register dependecies, use the semantics that the RipTide paper uses

## Getting started

**Build LLVM for pass dev on Windows/Wsl for this project:**
Without specifying -DCMAKE_INSTALL_PREFIX=..., it will install to:
Binaries: /usr/local/bin/
Libraries: /usr/local/lib/
Headers: /usr/local/include/
CMake configs: /usr/local/lib/cmake/llvm/ or similar

__Within WSL2 and the directory: /mnt/c/__

```bash
sudo apt-get update
sudo apt-get install ninja lld
git clone --depth 1 https://github.com/llvm/llvm-project.git
cd llvm-project
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_ENABLE_PLUGINS=ON \
  -DLLVM_ENABLE_PROJECTS="clang" \

# optional to set install location: -DCMAKE_INSTALL_PREFIX=~/llvm-install
ninja -C build
sudo ninja -C build install
```

**Run a pass**
```bash
git clone https://github.com/pncel/RipTide_compiler.git
cd RipTide_compiler
mkdir build && cd build
cmake -G Ninja .. -DLLVM_DIR=/usr/local/lib/cmake/llvm
ninja
/usr/local/bin/opt -load-pass-plugin ./ControlflowGraph.so -passes=ControlflowGraph -disable-output ../test/test_cfg.ll
```

**Bonus:**

Generate LLVM IR from C:
```bash
clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```

Visualize Data Flow Graph:
```bash
sudo apt-get install graphviz xdg-utils desktop-file-utils eog
dot -Tpng dfg.dot -o dfg.png
sudo xdg-open dfg.png
```

### Compiler flow:

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

Translates C code into a dataflow graph with nodes representing custom ISA ops

### Example C function to DataflowGraph conversion:

**Input:**

```C
void example(int*A, int n, int m) {
  A[m] = 1;
  for (int i = 0; i < n; i++){
    int foo = A[i];
    if(foo > 42) {
      A[i] = 0;
    }
    A[i] += foo + i;
  }
}
```
**Output:**

![Data flow graph](/dfg.png)
