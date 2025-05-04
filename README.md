# RipTide_compiler

**Compiler flow:**

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

**To generate LLVM IR from Clang:**

```bash
$ clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```
**How I built LLVM on Windows/Wsl for this project:**

```bash
$ sudo apt-get install ninja lld
```

Within WSL2 and the directory: /mnt/c/ 

```bash
$ git clone --depth 1 https://github.com/llvm/llvm-project.git

$ cd llvm-project

$ cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_USE_LINKER=lld -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_TARGETS_TO_BUILD= DLLVM_INCLUDE_TESTS=OFF

$ ninja -C build

$ cd build

$ sudo ninja install
```
