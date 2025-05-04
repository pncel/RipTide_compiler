# RipTide_compiler

**Compiler flow:**

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

**To generate LLVM IR from Clang:**

```bash
$ clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```
