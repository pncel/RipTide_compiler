# RipTide_compiler

**Build LLVM on Windows/Wsl for this project:**

Within WSL2 and the directory: /mnt/c/ 

```bash
$ sudo apt-get install ninja lld

$ git clone --depth 1 https://github.com/llvm/llvm-project.git

$ cd llvm-project

$ cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_USE_LINKER=lld -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_TARGETS_TO_BUILD= DLLVM_INCLUDE_TESTS=OFF

$ ninja -C build

$ cd build

$ sudo ninja install
```

### To Run

```bash
$ git clone https://github.com/pncel/RipTide_compiler.git
$ cd RipTide_compiler
$ mkdir build && cd build
$ cmake ..
$ make
$ opt -load-pass-plugin ./DataflowGraph.so -passes=dfg-pass -disable-output ../example/simple_ops_ir.ll
```

**To generate LLVM IR from Clang:**

```bash
$ clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```

### Compiler flow:

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

<<<<<<< HEAD
Example llvm ir to dfg:

llvm ir:

define i32 @my_func(i32 %a, i32 %b) {  
entry:  
  %1 = mul i32 %a, 5       ; Instruction 1: Node N1 (mul)  
  %2 = add i32 %b, 10      ; Instruction 2: Node N2 (add)  
  %3 = add i32 %1, %2      ; Instruction 3: Node N3 (add)  
  ret i32 %3               ; Instruction 4: (ret) - Uses %3  
}  
