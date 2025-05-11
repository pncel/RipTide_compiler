/*
What This Pass Does:
 - Inserts fence seq_cst before and after each memory operation (load/store/atomic).
 - Upgrades atomic instructions to sequential consistency.
 - Uses SyncScope::System to ensure the strictest global memory visibility.

To run on llvm ir:
clang++ -fPIC -shared -o libEnforce.so compiler/EnforceMem.cpp   `llvm-config --cxxflags --ldflags --system-libs`
opt -load-pass-plugin ./libEnforce.so -passes=EnforceMem -S test_mem.ll -o test_mem_after.ll
*/

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

namespace {
class EnforceMem : public PassInfoMixin<EnforceMem> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    std::vector<Instruction*> memOps;

    // Collect memory operations
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (isa<LoadInst>(&I) || isa<StoreInst>(&I) ||
            isa<AtomicRMWInst>(&I) || isa<AtomicCmpXchgInst>(&I)) {
          memOps.push_back(&I);
        }
      }
    }

    for (Instruction *I : memOps) {
      IRBuilder<> builder(I);

      // Insert fence before
      builder.CreateFence(AtomicOrdering::SequentiallyConsistent);

      // Insert fence after (if not at end of block)
      auto nextIt = std::next(BasicBlock::iterator(I));
      if (nextIt != I->getParent()->end()) {
        IRBuilder<> afterBuilder(&*nextIt);
        afterBuilder.CreateFence(AtomicOrdering::SequentiallyConsistent);
      }

      // Upgrade atomic operations to seq_cst
      if (auto *AI = dyn_cast<AtomicRMWInst>(I)) {
        AI->setOrdering(AtomicOrdering::SequentiallyConsistent);
        AI->setSyncScopeID(SyncScope::System);
      } else if (auto *CI = dyn_cast<AtomicCmpXchgInst>(I)) {
        CI->setSuccessOrdering(AtomicOrdering::SequentiallyConsistent);
        CI->setFailureOrdering(AtomicOrdering::SequentiallyConsistent);
        CI->setSyncScopeID(SyncScope::System);
      } else if (auto *LI = dyn_cast<LoadInst>(I)) {
        if (LI->isAtomic()) {
          LI->setOrdering(AtomicOrdering::SequentiallyConsistent);
          LI->setSyncScopeID(SyncScope::System);
        }
      } else if (auto *SI = dyn_cast<StoreInst>(I)) {
        if (SI->isAtomic()) {
          SI->setOrdering(AtomicOrdering::SequentiallyConsistent);
          SI->setSyncScopeID(SyncScope::System);
        }
      }
    }

    return PreservedAnalyses::none();
  }
};
} // namespace

// Plugin registration for new pass manager
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "EnforceMem", "v1.0",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "EnforceMem") {
            FPM.addPass(EnforceMem());
            return true;
          }
          return false;
        });
    }
  };
}
