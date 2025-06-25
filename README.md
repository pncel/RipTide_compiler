# RipTide_compiler

### TODO:

* Check memory ordering pass
* Implement carry
* Implement order 
* Implement invariant
* Remove need for Stream operator
* Implement Mapper

## Getting started

**Build LLVM for pass dev on Windows/Wsl for this project:**  

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

**Run a pass:**

```bash
git clone https://github.com/pncel/RipTide_compiler.git
cd RipTide_compiler
mkdir build && cd build
cmake -G Ninja .. -DLLVM_DIR=/usr/local/lib/cmake/llvm
ninja
/usr/local/bin/opt -load-pass-plugin ./ControlflowGraph.so -passes=ControlflowGraph -disable-output ../test/test_cfg.ll
```

### Bonus:

**Generate LLVM IR from C:**

```bash
clang -O2 -S -emit-llvm <source.c> -o <output.ll>
```

**Visualize Data Flow Graph:**

```bash
sudo apt-get install graphviz xdg-utils desktop-file-utils eog
dot -Tpng dfg.dot -o dfg.png
sudo xdg-open dfg.png
```

### Example C function to DataflowGraph conversion:

**Input:**

```C
int example(int n, int m, int p) {
  int A = 1;
  int mm = -5;

  A += m * p;
  mm *= m;
  A += n+mm*p;

  if (A >= 0){
    return A;
  } else {
    return (A * -1) + mm;
  }
  
}
```

**LLVM IR Generated:**

```llvm
define dso_local range(i32 -2147483647, -2147483648) i32 @example(i32 noundef %n, i32 noundef %m, i32 noundef %p) local_unnamed_addr #0 {
entry:
  %mul1 = mul nsw i32 %m, -5
  %reass.add = mul i32 %m, -4
  %reass.mul = mul i32 %reass.add, %p
  %add = add i32 %n, 1
  %add4 = add i32 %add, %reass.mul
  %add6 = sub nsw i32 %mul1, %add4
  %cmp16 = icmp slt i32 %add4, 0
  %retval.0 = select i1 %cmp16, i32 %add6, i32 %add4
  ret i32 %retval.0
}
```

**Output:**

![Data flow graph](/dfg.png)
