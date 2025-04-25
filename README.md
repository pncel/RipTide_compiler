# RipTide_compiler

Compiler flow:

C code -> Clang -> LLVM IR -> LLVM optimizer? -> optimized LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 
