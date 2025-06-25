/*
What This Pass Does:
 - Converts load and store instructions to custom Load Store Ordering (LSO) instructions
 - The lso.load takes the the usual load input (an address) as well as a token that must be valid before the load can fire
 - the lso.store takes in the usual store inputs (an address and value) but outputs a token that is then passed to depedendent load instructions

To run on llvm ir:
clang++ -fPIC -shared -o libEnforce.so compiler/EnforceMemOrderPass.cpp  `llvm-config --cxxflags --ldflags --system-libs`
opt -load-pass-plugin ./libEnforce.so -passes=EnforceMemOrderPass -S test_mem.ll -o test_mem_after.ll
*/
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/SmallVector.h" // For SmallVector
#include "llvm/ADT/Twine.h"
#include <map>

using namespace llvm;

namespace {
// Define the pass structure using the new Pass Manager's PassInfoMixin.
struct EnforceMemOrderPass : PassInfoMixin<EnforceMemOrderPass> {

    // Main entry point for the FunctionPass.
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
        // Skip declarations, as they have no bodies to process.
        if (F.isDeclaration()) {
            return PreservedAnalyses::all();
        }

        Module *M = F.getParent();
        LLVMContext &Context = M->getContext();
        bool Changed = false;

        // --- 1. Define and Get our Custom Intrinsics ---
        // A simple integer type (e.g., i1) will be used as the "token" type for merging.
        // The value '1' (or true) will signify an active token.
        Type *ActualTokenDataType = Type::getInt1Ty(Context); // Using i1 for the token value (true/false)

        // `lso.entry.token` is no longer needed as stores don't consume an entry token.
        // FunctionType *LsoEntryTokenType = FunctionType::get(ActualTokenDataType, {}, false);
        // FunctionCallee LsoEntryToken = M->getOrInsertFunction("lso.entry.token", LsoEntryTokenType);

        // Maps to store type-specific intrinsics
        std::map<Type *, FunctionCallee> LsoLoadIntrinsics;
        std::map<Type *, FunctionCallee> LsoStoreIntrinsics;

        // Helper to get or insert a type-specific lso.load intrinsic.
        // `lso.load.TY` now takes a pointer and a token, and returns only the loaded value.
        // It does not produce a token for subsequent instructions.
        auto getOrCreateLsoLoad = [&](Type *ValType, Type *PtrType) {
            if (LsoLoadIntrinsics.count(ValType))
                return LsoLoadIntrinsics[ValType];

            // Get a string representation for the type to use in the function name.
            std::string TypeNameStr;
            raw_string_ostream RSO(TypeNameStr);
            ValType->print(RSO);

            // `lso.load.TY` now returns only the loaded value (ValType).
            // It takes the pointer and the token that enables it to fire.
            FunctionType *LsoLoadType = FunctionType::get(ValType, {PtrType, ActualTokenDataType}, false);
            FunctionCallee LsoLoad = M->getOrInsertFunction((Twine("lso.load.") + RSO.str()).str(), LsoLoadType);
            LsoLoadIntrinsics[ValType] = LsoLoad;
            return LsoLoad;
        };

        // Helper to get or insert a type-specific lso.store intrinsic.
        // `lso.store.TY` takes a pointer and a value, and returns a new token ('1').
        // It *does not* take an incoming token as it's not dependent on one.
        auto getOrCreateLsoStore = [&](Type *ValType, Type *PtrType) {
            if (LsoStoreIntrinsics.count(ValType))
                return LsoStoreIntrinsics[ValType];

            // Get a string representation for the type to use in the function name.
            std::string TypeNameStr;
            raw_string_ostream RSO(TypeNameStr);
            ValType->print(RSO);

            // `lso.store.TY` returns an i1 token (constant true).
            // It takes ptr and value. It does NOT take an incoming token.
            FunctionType *LsoStoreType = FunctionType::get(ActualTokenDataType, {PtrType, ValType}, false);
            FunctionCallee LsoStore = M->getOrInsertFunction((Twine("lso.store.") + RSO.str()).str(), LsoStoreType);
            LsoStoreIntrinsics[ValType] = LsoStore;
            return LsoStore;
        };


        // --- 2. Process the Function ---
        // This map will store the last memory dependency token that was *produced*
        // in each basic block. This token will be passed to successor blocks via PHI nodes.
        std::map<BasicBlock *, Value *> LastProducedTokenInBlock;
        
        // This map will store the PHI nodes we create for merging tokens at block boundaries.
        std::map<BasicBlock *, PHINode *> TokenPHIs;

        // Collect blocks to process to avoid iterator invalidation issues.
        SmallVector<BasicBlock *, 16> BlocksToProcess;
        for (BasicBlock &BB : F) {
            BlocksToProcess.push_back(&BB);
        }
        
        // First, create all the necessary PHI nodes for non-entry blocks.
        // These PHIs will merge the tokens produced by predecessor blocks.
        for (BasicBlock *BB : BlocksToProcess) {
            if (BB->isEntryBlock()) {
                continue;
            }

            // Create a PHI node if the block has predecessors.
            // The PHI type is ActualTokenDataType (i1).
            if (pred_begin(BB) != pred_end(BB)) {
                IRBuilder<> PhiBuilder(&BB->front());
                PHINode *Phi = PhiBuilder.CreatePHI(ActualTokenDataType, std::distance(pred_begin(BB), pred_end(BB)), "lso.token.phi");
                TokenPHIs[BB] = Phi;
            }
        }

        // Now, iterate through the blocks and replace memory instructions.
        for (BasicBlock *BB : BlocksToProcess) {
            // CurrentToken represents the token *available* to fire the current memory operation.
            // It is the last token *produced* before this instruction, or the merged token for the block.
            Value *CurrentToken;

            // Determine the starting token for this block.
            if (BB->isEntryBlock()) {
                IRBuilder<> EntryBuilder(&BB->front());
                // For the entry block, the initial token is a constant 'true' (1) of ActualTokenDataType.
                // This allows the very first load in the program to fire.
                CurrentToken = ConstantInt::get(ActualTokenDataType, 1);
            } else {
                // For non-entry blocks, the token comes from the PHI node,
                // which merges tokens from all predecessors.
                assert(TokenPHIs.count(BB) && "Non-entry block must have a token PHI if reachable.");
                CurrentToken = TokenPHIs[BB];
            }

            // This variable will track the *last token produced* by a store or the entry point
            // within the current block. This is what will be passed to successor blocks.
            Value *LastTokenProducedInBlock = CurrentToken;

            // Collect memory instructions to avoid iterator invalidation during replacement.
            SmallVector<Instruction *, 32> MemInsts;
            for (Instruction &I : *BB) {
                if (isa<LoadInst>(I) || isa<StoreInst>(I) || isa<AtomicCmpXchgInst>(I) || isa<AtomicRMWInst>(I)) {
                    MemInsts.push_back(&I);
                }
            }

            // Replace each memory instruction with its corresponding intrinsic.
            for (Instruction *I : MemInsts) {
                IRBuilder<> Builder(I);
                if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
                    Type *LoadedValueType = LI->getType();
                    Type *PointerType = LI->getPointerOperand()->getType();
                    FunctionCallee LsoLoad = getOrCreateLsoLoad(LoadedValueType, PointerType);

                    // Call the lso.load intrinsic. It takes the pointer and the CurrentToken.
                    // The CurrentToken fires the load. The intrinsic directly returns the loaded value.
                    Value *LoadedVal = Builder.CreateCall(LsoLoad, {LI->getPointerOperand(), CurrentToken});
                    
                    // Replace all uses of the original load instruction with the extracted loaded value.
                    LI->replaceAllUsesWith(LoadedVal);
                    
                    // IMPORTANT: A load instruction *consumes* the token to fire but does NOT
                    // produce a new token for subsequent memory operations within the same block.
                    // The token flow continues with the LastTokenProducedInBlock, which reflects
                    // the last token generated by a store or the entry point.
                    
                    LI->eraseFromParent();
                    Changed = true;

                } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
                    Type *StoredValueType = SI->getValueOperand()->getType();
                    Type *PointerType = SI->getPointerOperand()->getType();
                    FunctionCallee LsoStore = getOrCreateLsoStore(StoredValueType, PointerType);

                    // Call the lso.store intrinsic. It takes the pointer and value.
                    // It returns a new token (constant 'true'), which becomes the
                    // LastTokenProducedInBlock and also the CurrentToken for the next instruction.
                    Value *NewToken = Builder.CreateCall(LsoStore, {SI->getPointerOperand(), SI->getValueOperand()});
                    
                    // Update CurrentToken for the *next* instruction in this block.
                    CurrentToken = NewToken; 
                    // Update LastTokenProducedInBlock to reflect the latest token produced.
                    LastTokenProducedInBlock = NewToken;
                    
                    SI->eraseFromParent();
                    Changed = true;
                } else if (AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(I)) {
                    // For atomic operations, we're not replacing them with LSO intrinsics
                    // in this pass. We just set their memory ordering to sequentially consistent
                    // to ensure strong ordering relative to other atomics and non-atomics.
                    CXI->setSyncScopeID(SyncScope::System);
                    CXI->setSuccessOrdering(AtomicOrdering::SequentiallyConsistent);
                    CXI->setFailureOrdering(AtomicOrdering::SequentiallyConsistent);
                    Changed = true;
                    // If atomics were to participate in the token chain (e.g., produce a token),
                    // they would need dedicated LSO intrinsics similar to loads and stores.
                    // For now, they implicitly respect the token flow due to their strong ordering.
                } else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I)) {
                    // Similar to AtomicCmpXchgInst, ordering is set.
                    RMWI->setSyncScopeID(SyncScope::System);
                    RMWI->setOrdering(AtomicOrdering::SequentiallyConsistent);
                    Changed = true;
                    // As with CmpXchg, if RMWs were to produce tokens, new intrinsics would be needed.
                }
            }
            // Store the final LastTokenProducedInBlock for this block. This will be used by
            // successor blocks to populate their PHI nodes.
            LastProducedTokenInBlock[BB] = LastTokenProducedInBlock;
        }
        
        // Finally, populate the PHI nodes.
        // This loop must run AFTER all blocks have had their LastProducedTokenInBlock populated,
        // ensuring that the tokens from all predecessors are available.
        for (auto const& [BB, Phi] : TokenPHIs) {
            for (BasicBlock *Pred : predecessors(BB)) {
                assert(LastProducedTokenInBlock.count(Pred) && "Predecessor token not found! Ensure blocks are processed in a suitable order or all predecessors have a token.");
                Phi->addIncoming(LastProducedTokenInBlock[Pred], Pred);
            }
        }

        if (Changed) {
            return PreservedAnalyses::none();
        }
        return PreservedAnalyses::all();
    }
};
} // end anonymous namespace

// Boilerplate for registering the pass with the new Pass Manager.
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "EnforceMemOrderPass", "v0.1",
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager &FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "EnforceMemOrderPass") {
                            FPM.addPass(EnforceMemOrderPass());
                            return true;
                        }
                        return false;
                    });
            }};
}