# RipTide_compiler

Compiler flow:

C code -> Clang -> LLVM IR -> dfg_generator -> dfg+CGRA description -> mapper -> CGRA mapping -> bitstream generator -> bitsream 

Example llvm ir to dfg:

llvm ir:

define i32 @my_func(i32 %a, i32 %b) {  
entry:  
  %1 = mul i32 %a, 5       ; Instruction 1: Node N1 (mul)  
  %2 = add i32 %b, 10      ; Instruction 2: Node N2 (add)  
  %3 = add i32 %1, %2      ; Instruction 3: Node N3 (add)  
  ret i32 %3               ; Instruction 4: (ret) - Uses %3  
}  

dfg:  
      Arg(a)   Const(5)       Arg(b)   Const(10)  
         \       /                 \       /  
            +-----+                   +-----+  
          | N1  |                   | N2  |  
          | mul |                   | add |  
          +-----+                   +-----+  
             \                       /  
              +---------------------+  
              |         N3          |  
              |         add         |  
              +---------------------+  
                        |  
                      (ret)  
