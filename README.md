# RipTide_compiler

### TODO:

*  <span style="color:green;">&#10004;</span> Use RIPTIDE semantics to build a control flow graph
*  <span style="color:green;">&#10004;</span> Control flow operators (get offloaded to NoC):
*  <span style="color:green;">&#10004;</span> Carry, invariant, T&F steer, stream , order, merge
*  <span style="color:green;">&#10004;</span> Don't just use register dependecies, use the semantics that the RipTide paper uses

* Carry op not looking correct
* Verify dataflow graph with more test cases
  * Does stream work correct?
  * Does this work for more complex functions?
  * Where does it break?
* Write middle end to enforce memory ordering
  * Directly modifies llvm ir
* Map dataflow graph to cgra hardware

## Getting started

**Build LLVM for pass dev on Windows/Wsl for this project:**  

__Within WSL2 and the directory: /mnt/c/__

```bash
sudo apt-get update
sudo apt-get install ninja lld
git clone  ~~depth 1 https://github.com/llvm/llvm-project.git
cd llvm-project
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_ENABLE_PLUGINS=ON \
  -DLLVM_ENABLE_PROJECTS="clang"

 # optional to set install location: -DCMAKE_INSTALL_PREFIX=~/llvm-install

ninja -C build
sudo ninja -C build install
```
Without specifying -DCMAKE_INSTALL_PREFIX=..., it will install to:  
Binaries: /usr/local/bin/  
Libraries: /usr/local/lib/  
Headers: /usr/local/include/  
CMake configs: /usr/local/lib/cmake/llvm/ or similar 

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

LLVM IR Generated:

```llvm
; ModuleID = '../test/simple_ops.c'
source_filename = "../test/simple_ops.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable
define dso_local void @example(ptr noundef captures(none) %A, i32 noundef %n, i32 noundef %m) local_unnamed_addr #0 {
entry:
  %idxprom = sext i32 %m to i64
  %arrayidx = getelementptr inbounds i32, ptr %A, i64 %idxprom
  store i32 1, ptr %arrayidx, align 4, !tbaa !5
  %cmp18 = icmp sgt i32 %n, 0
  br i1 %cmp18, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext nneg i32 %n to i64
  %min.iters.check = icmp ult i32 %n, 8
  br i1 %min.iters.check, label %for.body.preheader22, label %vector.ph

vector.ph:                                        ; preds = %for.body.preheader
  %n.vec = and i64 %wide.trip.count, 2147483640
  br label %vector.body

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ]
  %vec.ind = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %vector.ph ], [ %vec.ind.next, %vector.body ]
  %step.add = add <4 x i32> %vec.ind, splat (i32 4)
  %0 = getelementptr inbounds nuw i32, ptr %A, i64 %index
  %1 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %wide.load = load <4 x i32>, ptr %0, align 4, !tbaa !5
  %wide.load21 = load <4 x i32>, ptr %1, align 4, !tbaa !5
  %2 = icmp sgt <4 x i32> %wide.load, splat (i32 42)
  %3 = icmp sgt <4 x i32> %wide.load21, splat (i32 42)
  %4 = select <4 x i1> %2, <4 x i32> zeroinitializer, <4 x i32> %wide.load
  %5 = select <4 x i1> %3, <4 x i32> zeroinitializer, <4 x i32> %wide.load21
  %6 = add nsw <4 x i32> %wide.load, %vec.ind
  %7 = add nsw <4 x i32> %wide.load21, %step.add
  %8 = add nsw <4 x i32> %6, %4
  %9 = add nsw <4 x i32> %7, %5
  store <4 x i32> %8, ptr %0, align 4, !tbaa !5
  store <4 x i32> %9, ptr %1, align 4, !tbaa !5
  %index.next = add nuw i64 %index, 8
  %vec.ind.next = add <4 x i32> %vec.ind, splat (i32 8)
  %10 = icmp eq i64 %index.next, %n.vec
  br i1 %10, label %middle.block, label %vector.body, !llvm.loop !9

middle.block:                                     ; preds = %vector.body
  %cmp.n = icmp eq i64 %n.vec, %wide.trip.count
  br i1 %cmp.n, label %for.cond.cleanup, label %for.body.preheader22

for.body.preheader22:                             ; preds = %for.body.preheader, %middle.block
  %indvars.iv.ph = phi i64 [ 0, %for.body.preheader ], [ %n.vec, %middle.block ]
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %middle.block, %entry
  ret void

for.body:                                         ; preds = %for.body.preheader22, %for.body
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.body ], [ %indvars.iv.ph, %for.body.preheader22 ]
  %arrayidx2 = getelementptr inbounds nuw i32, ptr %A, i64 %indvars.iv
  %11 = load i32, ptr %arrayidx2, align 4, !tbaa !5
  %cmp3 = icmp sgt i32 %11, 42
  %spec.select = select i1 %cmp3, i32 0, i32 %11
  %12 = trunc nuw nsw i64 %indvars.iv to i32
  %add = add nsw i32 %11, %12
  %add8 = add nsw i32 %add, %spec.select
  store i32 %add8, ptr %arrayidx2, align 4, !tbaa !5
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body, !llvm.loop !13
}
```

**Output:**

![Data flow graph](/dfg.png)
