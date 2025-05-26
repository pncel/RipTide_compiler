#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h" // For predecessor_iterator, successor_iterator
#include "llvm/Transforms/Utils/BasicBlockUtils.h" // For general block utilities (though not directly used for the core merge here)
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h" // For iterating over a snapshot of blocks

using namespace llvm;

namespace {

// Define the pass structure
struct UnconditionalBranchRemoverPass : PassInfoMixin<UnconditionalBranchRemoverPass> {

    // Main entry point for the pass
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        bool GlobalChanged = false; // Flag to track if any change was made in the function
        bool MadeChangeInIteration;   // Flag to track if a change was made in the current iteration

        // Loop a_until no more changes are made in a full pass over the function.
        // This iterative approach is necessary because merging blocks can create
        // new opportunities for further merges.
        do {
            MadeChangeInIteration = false;

            // It's safer to iterate over a snapshot of basic block pointers,
            // as merging and erasing blocks can invalidate iterators over F.getBasicBlockList().
            SmallVector<BasicBlock*, 16> BlocksToProcess;
            for (BasicBlock &BB_ref : F) {
                BlocksToProcess.push_back(&BB_ref);
            }

            for (BasicBlock *BB : BlocksToProcess) {
                // If BB was already merged and erased in this iteration, its parent would be null.
                if (!BB->getParent()) {
                    continue;
                }

                Instruction *Terminator = BB->getTerminator();
                if (!Terminator) { // Should not happen for a well-formed block not yet processed
                    continue;
                }

                // Check if the terminator is an unconditional branch
                if (auto *Branch = dyn_cast<BranchInst>(Terminator)) {
                    if (Branch->isUnconditional()) {
                        BasicBlock *TargetBB = Branch->getSuccessor(0);

                        // --- Safety Check 1: Cannot merge a block with itself ---
                        if (BB == TargetBB) {
                            continue;
                        }

                        // --- Safety Check 2: TargetBB must not be the entry block ---
                        // We are effectively eliminating TargetBB by merging it.
                        // The entry block cannot be eliminated this way.
                        if (TargetBB == &F.getEntryBlock()) {
                            // Optionally print why a merge is skipped:
                            // errs() << "UnconditionalBranchRemover: Skipping merge in " << F.getName()
                            //        << ": Target block '" << TargetBB->getName() << "' is the entry block.\n";
                            continue;
                        }

                        // --- Safety Check 3: TargetBB must have BB as its *only* predecessor ---
                        // getSinglePredecessor() returns nullptr if there isn't exactly one predecessor.
                        BasicBlock *SinglePred = TargetBB->getSinglePredecessor();
                        if (!SinglePred || SinglePred != BB) {
                            // Optionally print why a merge is skipped:
                            // if (SinglePred) {
                            //     errs() << "UnconditionalBranchRemover: Skipping merge in " << F.getName()
                            //            << ": Target block '" << TargetBB->getName() << "' has single predecessor '"
                            //            << SinglePred->getName() << "', but expected '" << BB->getName() << "'.\n";
                            // } else {
                            //     errs() << "UnconditionalBranchRemover: Skipping merge in " << F.getName()
                            //            << ": Target block '" << TargetBB->getName()
                            //            << "' does not have a single predecessor (or has multiple).\n";
                            // }
                            continue;
                        }

                        // If all checks pass, it's safe to merge TargetBB into BB.
                        errs() << "UnconditionalBranchRemover: Merging block '" << TargetBB->getName()
                               << "' into '" << BB->getName() << "' in function '" << F.getName() << "'.\n";

                        // 1. Remove the unconditional branch from BB.
                        //    BB will temporarily have no terminator.
                        Branch->eraseFromParent();

                        // 2. Splice all of TargetBB's instructions (including its terminator)
                        //    into the end of BB. BB now inherits TargetBB's instructions and terminator.
                        BB->getInstList().splice(BB->end(), TargetBB->getInstList());

                        // 3. Update all uses of TargetBB to now refer to BB.
                        //    This is crucial for PHI nodes in successors of the original TargetBB,
                        //    which will now correctly list BB as the incoming block.
                        TargetBB->replaceAllUsesWith(BB);

                        // 4. Erase the now-empty (and unreferenced) TargetBB.
                        //    This invalidates any pointers or references to TargetBB.
                        TargetBB->eraseFromParent();

                        MadeChangeInIteration = true;
                        GlobalChanged = true;

                        // Since the CFG was modified (a block was erased),
                        // it's safest to restart the scan of the function to ensure
                        // all decisions are based on the updated CFG.
                        // The outer do-while loop handles this by re-iterating if MadeChangeInIteration is true.
                        // We break from this inner loop over BlocksToProcess to trigger the re-scan.
                        goto restart_function_scan;
                    }
                }
            }
        // Label for restarting the scan of the current function if a change was made.
        restart_function_scan:;
        } while (MadeChangeInIteration);

        if (GlobalChanged) {
            // F.dump(); // Useful for debugging: prints the function's IR after changes.
            errs() << "UnconditionalBranchRemover: Function '" << F.getName() << "' was modified.\n";
            return PreservedAnalyses::none(); // CFG was changed.
        }

        return PreservedAnalyses::all(); // No changes were made.
    }
};

} // end anonymous namespace

// Boilerplate for registering the pass with the new Pass Manager
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "UnconditionalBranchRemover", "v0.1",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    // This allows the pass to be called with -passes=remove-unconditional-branches
                    if (Name == "UnconditionalBranchRemoverPass") {
                        FPM.addPass(UnconditionalBranchRemoverPass());
                        return true;
                    }
                    return false;
                }
            );
        }
    };
}
