#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/Use.h"
#include "llvm/ADT/DepthFirstIterator.h" // For traversing CFG
#include "llvm/Analysis/PostDominators.h" // To find merge points
#include <llvm/Analysis/LoopInfo.h>

#include "CustomDataflowGraph.h" // Include our new header

#include <map>
#include <set>
#include <string>
#include <list>
#include <vector>
#include <memory> // For unique_ptr
#include <algorithm> // For std::find
#include <system_error>

using namespace llvm;
using namespace llvm::sys;

namespace {
class DataflowGraph : public PassInfoMixin<DataflowGraph> {
  
  // Helper to unify steering logic
  std::pair<DataflowNode*,DataflowNode*>
  createSteers(CustomDataflowGraph &G,
             Value *Cond,
             Value *TrueVal,
             Value *FalseVal) {

    // 1) comparison node
    DataflowNode *condN = G.getOrAdd(Cond);
    condN->Type = DataflowOperatorType::BasicBinaryOp; // icmp/fcmp

    // 2) two steer nodes
    DataflowNode *tS = G.addNode(DataflowOperatorType::TrueSteer,  nullptr, "T");
    DataflowNode *fS = G.addNode(DataflowOperatorType::FalseSteer, nullptr, "F");

    // 3) wire the condition
    G.wireValueToNode(Cond, tS);
    G.wireValueToNode(Cond, fS);

    // 4) wire data values
    if (TrueVal)
      G.wireValueToNode(TrueVal, tS);
    if (FalseVal)
      G.wireValueToNode(FalseVal, fS);

    return {tS, fS};
  }

  // Map to store steer nodes created for conditional branches
  std::map<BranchInst*, std::pair<DataflowNode*,DataflowNode*>> BranchSteers;

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    // Instantiate our custom dataflow graph
    CustomDataflowGraph customGraph;
    errs() << "Building Custom DFG for function: " << F.getName() << "\n";

    // Get analysis results (LoopInfo is needed for specific DFG patterns)
    LoopInfo         &LI  = FAM.getResult<LoopAnalysis>(F);

    // Pass 1: Create nodes for all relevant LLVM values (instructions, arguments, constants)
    // This helps in mapping users/operands to graph nodes later.
    for (auto& BB : F) {
        for (auto& I : BB) {
          std::string sym;
          ICmpInst *CI_icmp = nullptr; // Use a distinct variable name for ICmpInst
          FCmpInst *FI_fcmp = nullptr; // Use a distinct variable name for FCmpInst
          if (isa<SelectInst>(&I) || isa<GetElementPtrInst>(&I)
            || (isa<BranchInst>(&I) && cast<BranchInst>(&I)->isConditional()) ||
            isa<CastInst>(&I) ||
            isa<ReturnInst>(&I) // MODIFIED: Skip ReturnInst during node creation
          )
            continue; // These are handled specially or by wireValueToNode

          DataflowOperatorType opType = DataflowOperatorType::Unknown;
          std::string label = "";

          if (I.isBinaryOp()) {
            opType = DataflowOperatorType::BasicBinaryOp;
            label = Instruction::getOpcodeName(I.getOpcode());
            switch (I.getOpcode()) {
              case Instruction::Add:      sym = "+"; break;
              case Instruction::FAdd:     sym = "+"; break;
              case Instruction::Sub:      sym = "-"; break;
              case Instruction::Mul:      sym = "*"; break;
              case Instruction::UDiv:     sym = "/"; break;
              // Add more binary operations as needed
              default:                    sym = Instruction::getOpcodeName(I.getOpcode());
            }
          } else if (CallInst *CallI = dyn_cast<CallInst>(&I)) { // MODIFIED: Use a distinct variable name for CallInst
              if (CallI->getCalledFunction()) {
                  if (CallI->getCalledFunction()->getName().contains("lso.load")) {
                      opType = DataflowOperatorType::Load;
                      label = "ld";
                  } else if (CallI->getCalledFunction()->getName().contains("lso.store")) {
                      opType = DataflowOperatorType::Store;
                      label = "st";
                  } else {
                      label = "call"; // Generic label for other calls
                  }
              }
          } else if (CI_icmp = dyn_cast<ICmpInst>(&I)) { // Use CI_icmp for ICmpInst
              // Comparisons will be linked to Steer nodes, create a node for the comparison result
              label = Instruction::getOpcodeName(I.getOpcode());
              opType = DataflowOperatorType::BasicBinaryOp;
              switch (CI_icmp->getPredicate()) { // Use CI_icmp here
                case ICmpInst::ICMP_EQ:  sym = "=="; break;
                case ICmpInst::ICMP_NE:  sym = "!="; break;
                case ICmpInst::ICMP_SLT: sym = "<";  break;
                case ICmpInst::ICMP_SLE: sym = "<="; break;
                case ICmpInst::ICMP_SGT: sym = ">";  break;
                case ICmpInst::ICMP_SGE: sym = ">="; break;
                default:                 sym = CmpInst::getPredicateName(CI_icmp->getPredicate()).str();
              }
          } else if (FI_fcmp = dyn_cast<FCmpInst>(&I)) { // Use FI_fcmp for FCmpInst
              opType = DataflowOperatorType::BasicBinaryOp;
              sym = CmpInst::getPredicateName(FI_fcmp->getPredicate()).str();
          } else if (isa<PHINode>(&I)) {
              opType = DataflowOperatorType::Merge;
              label = "M"; // Based on image
          } 
          // Add checks for other instruction types you want to represent

          // Get or create the node for the instruction
          DataflowNode* instNode = customGraph.getOrAdd(&I);

          // Update the type and label if determined
          if (instNode) {
            if (opType != DataflowOperatorType::Unknown) instNode->Type = opType;
            if (!label.empty()) instNode->Label = label; // Prioritize explicit labels
            instNode->OpSymbol = sym;       // store the symbol
          }
        }
    }
    
    // Add nodes for function arguments
    for (auto& Arg : F.args()) {
      DataflowNode* argNode = customGraph.getOrAdd(&Arg);
      // Type is set in the modified getOrAdd, but ensure label is descriptive
      if (argNode && argNode->Label.empty()) {
        std::string argLabel; raw_string_ostream ss(argLabel);
        ss << Arg; // Print arg name and type
        argNode->Type = DataflowOperatorType::FunctionInput; // Ensure type is correct
        argNode->Label = ss.str();
      }
    }

    // Add nodes for constants used
    for (auto& BB : F) {
      for (auto& I : BB) {
        for (auto &Op : I.operands()) {
          if (auto *C = dyn_cast<Constant>(Op)) {
            customGraph.getOrAdd(C);
          }
          // Also add nodes for Arguments if they are operands (e.g., ptr args)
          if (auto *A = dyn_cast<Argument>(Op)) {
            customGraph.getOrAdd(A);
          }
        }
      }
    }

    // Convert select nodes to T/F steers
    for (auto &BB : F) {
      for (auto &inst : BB) {
        if (auto *SI = dyn_cast<SelectInst>(&inst)) {
          auto [tS, fS] = createSteers(customGraph,
                                         SI->getCondition(),
                                         SI->getTrueValue(),
                                         SI->getFalseValue());

          // Now re-wire all users of the LLVM select → our two steers
          for (auto *U : SI->users()) {
            if (auto *userInst = dyn_cast<Instruction>(U)) {
              if (auto *dest = customGraph.findNodeForValue(userInst)) {
                customGraph.addEdge(tS, dest);
                customGraph.addEdge(fS, dest);
              }
            }
            // Handle cases where a SelectInst is used by an Argument or Constant (less common)
          }
        }
      }
    }


    // Pass 2: Add edges based on data dependencies and handle special instructions
    for (auto &BB : F) {
      for (auto &I : BB) {
          // MODIFIED: Handle custom load/store calls
          if (auto *CI = dyn_cast<CallInst>(&I)) {
              if (CI->getCalledFunction()) {
                  if (CI->getCalledFunction()->getName().contains("lso.load")) {
                      if (auto *loadNode = customGraph.findNodeForValue(&I)) {
                          // wire the address operand (first operand)
                          customGraph.wireValueToNode(CI->getArgOperand(0), loadNode);
                          // wire the token operand (second operand)
                          customGraph.wireValueToNode(CI->getArgOperand(1), loadNode);
                      }
                  } else if (CI->getCalledFunction()->getName().contains("lso.store")) {
                      if (auto *storeNode = customGraph.findNodeForValue(&I)) {
                          // wire the pointer operand (first operand)
                          customGraph.wireValueToNode(CI->getArgOperand(0), storeNode);
                          // wire the value operand (second operand)
                          customGraph.wireValueToNode(CI->getArgOperand(1), storeNode);
                          // The lso.store intrinsic no longer takes a third (token) operand.
                          // The token is now a return value from the store.
                          // customGraph.wireValueToNode(CI->getArgOperand(2), storeNode); // REMOVED: This line was causing the error
                      }
                      continue; // Skip generic wiring below for custom stores
                  }
              }
          }
          // wire Load pointer into the Load node (original LoadInst handling, kept for other LoadInsts)
          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            if (auto *loadNode = customGraph.findNodeForValue(&I)) {
              // wire the address operand (so GEP→ZExt→%m path reaches the load)
              customGraph.wireValueToNode(LI->getPointerOperand(), loadNode);
            }
            // No need to do any further wiring for loads, as outputs are handled by users
            //continue; // Skip generic wiring below for loads
          }
          // wire Store operands into the Store node (original StoreInst handling, kept for other StoreInsts)
          if (auto *SI = dyn_cast<StoreInst>(&I)) {
            if (auto *storeNode = customGraph.findNodeForValue(&I)) {
              // wire the *value* being stored
              customGraph.wireValueToNode(SI->getValueOperand(), storeNode);
              // wire the *pointer* you're storing into
              customGraph.wireValueToNode(SI->getPointerOperand(), storeNode);
            }
            // no need to do any further wiring for stores
            continue; // Skip generic wiring below for stores
          }
          // --- handle GEP as pure pass-through ---
          if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
            // for each user of the GEP, wire all of GEP’s operands directly to that user
            for (User *U : GEP->users()) {
              if (auto *userInst = dyn_cast<Instruction>(U)) {
                if (auto *dest = customGraph.findNodeForValue(userInst)) {
                  // wire base pointer
                  customGraph.wireValueToNode(GEP->getPointerOperand(), dest);
                  // wire any variable indices too
                  for (unsigned i = 1, e = GEP->getNumOperands(); i < e; ++i) {
                    customGraph.wireValueToNode(GEP->getOperand(i), dest);
                  }
                }
              }
            }
            continue; // skip all the rest of the generic wiring for this instruction
          }
          // --- wire *any* cast inputs directly into each of its users ---
          if (auto *CI = dyn_cast<CastInst>(&I)) {
            for (User *U : CI->users()) {
              if (auto *userInst = dyn_cast<Instruction>(U)) {
                if (auto *dest = customGraph.findNodeForValue(userInst)) {
                  customGraph.wireValueToNode(CI->getOperand(0), dest);
                }
              }
            }
            continue; // skip all other wiring for casts
          }

          // wire every constant operand into I
          if (DataflowNode *instN = customGraph.findNodeForValue(&I)) {
            // MODIFIED: Handle CallInsts specially to avoid wiring the function declaration
            if (CallInst *CI = dyn_cast<CallInst>(&I)) {
                // Iterate only over the actual arguments, skipping the callee (operand 0)
                for (Value *ArgOperand : CI->operands()) {
                    if (auto *C = dyn_cast<Constant>(ArgOperand)) {
                        customGraph.addEdge(customGraph.getOrAdd(C), instN);
                    }
                }
            } else {
                // For non-CallInst instructions, iterate over all operands as before
                for (auto &op : I.operands()) {
                  if (auto *C = dyn_cast<Constant>(op)) {
                    customGraph.addEdge(customGraph.getOrAdd(C), instN);
                  }
                }
            }
          }
          
          // skip control ops entirely or types handled explicitly above
          if (isa<BranchInst>(&I)   ||
            isa<PHINode>(&I)      ||
            isa<SelectInst>(&I)   ||
            isa<ReturnInst>(&I)   // MODIFIED: Explicitly skip ReturnInst here too
          )
            continue;
          
          DataflowNode* sourceNode = customGraph.findNodeForValue(&I);
          if (!sourceNode) continue; // Node might not exist for skipped instructions or if not yet added

          // Handle data dependencies for basic operations
          // Edges go from the definition to the use
          for (User *user : I.users()) {
            if (Instruction *dep = dyn_cast<Instruction>(user)) {
              DataflowNode* destNode = customGraph.findNodeForValue(dep);
              if (destNode) {
                // Add data dependency edge
                // Avoid duplicating edges that are handled by steers or PHIs
                bool isSteerSource = isa<ICmpInst>(&I) || isa<FCmpInst>(&I);
                bool isSteerDest = destNode->Type == DataflowOperatorType::TrueSteer || destNode->Type == DataflowOperatorType::FalseSteer;
                bool isPHIDest = isa<PHINode>(dep);

                if (!isPHIDest && (!isSteerSource || !isSteerDest)) {
                    customGraph.addEdge(sourceNode, destNode);
                }
              }
            }
            // For Argument users, they should already be handled by the FunctionInput wiring.
          }
        }
      }

      llvm::Value *const_duplicate = nullptr;

      // Pass 3: Add edges specifically for PHI nodes (Merges)
      for (auto &BB : F) {
        for (auto &I : BB) {
            if (auto *PN = dyn_cast<PHINode>(&I)) {
                DataflowNode *phiNode = customGraph.findNodeForValue(PN);
                if (!phiNode) continue;

                BasicBlock *phiBlock = PN->getParent();
                Loop *L = LI.getLoopFor(phiBlock);
                bool isLoopCarried = false;

                // Check if this is a loop-header PHI with a back-edge dependency
                if (L && L->getHeader() == phiBlock) {
                    for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                        BasicBlock *inBB = PN->getIncomingBlock(i);
                        // A back-edge is an edge from a block inside the loop to the header
                        if (L->contains(inBB)) {
                             isLoopCarried = true;
                             break;
                        }
                    }
                }

                if (isLoopCarried) {
                    // --- This is a loop-carried dependency, create a CARRY node ---
                    phiNode->Type = DataflowOperatorType::Carry;
                    phiNode->Label = "C";
                    phiNode->OpSymbol = ""; // Carry doesn't have a simple symbol

                    // Robustly find the loop's governing condition using LoopInfo.
                    // This is the condition that determines whether to continue or exit the loop.
                    
                    Value *loopCondition = nullptr;
                    
                    // First, find the loop's preheader.
                    BasicBlock *preheader = L->getLoopPredecessor();

                    if (preheader) {
                        // Now, find the block that branches into the preheader.
                        // In your example IR, this is the 'entry' block.
                        // The `getSinglePredecessor()` method is a safe way to get the predecessor
                        // if there's only one.
                        if (BasicBlock *pre_preheader = preheader->getSinglePredecessor()) {
                            // Check the terminator instruction of the 'pre_preheader' block.
                            if (auto *BI = dyn_cast<BranchInst>(pre_preheader->getTerminator())) {
                                // If the terminator is a conditional branch, get its condition.
                                if (BI->isConditional()) {
                                    loopCondition = BI->getCondition();
                                }
                            }
                        }
                    }

                    BasicBlock *exitingBlock = L->getExitingBlock();
                    
                    if (exitingBlock && !loopCondition) {
                        if (auto *BI = dyn_cast<BranchInst>(exitingBlock->getTerminator())) {
                            if (BI->isConditional()) {
                                loopCondition = BI->getCondition();
                            }
                        }
                    }

                    // Wire D (decider) input to the Carry node
                    if (loopCondition) {
                        customGraph.wireValueToNode(loopCondition, phiNode);
                        for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                          if (isa<Constant>(PN->getIncomingValue(i))) {
                              // This adds an edge from the phi node to the loop condition node.
                              // The loop condition is the 'decider' input.
                              customGraph.addEdge(phiNode, customGraph.findNodeForValue(loopCondition));
                              // Remove the now spare edge from the const input to the conditional node
                              if (auto *cmpInst = dyn_cast<ICmpInst>(loopCondition)) {
                                  // Get the constant operand.
                                  // The operand '0' is at index 1 for the 'gt' predicate.
                                  // For 'sgt' (signed greater than), the constant will be the second operand.
                                  const_duplicate = cmpInst->getOperand(1);
                              }
                              break; // Found one, no need to check others.
                          }
                        }
                    } else {
                        errs() << "Warning: Could not determine loop condition for Carry node created from PHI in " << phiBlock->getName() << "\n";
                    }

                    // Wire A (initial value) and B (carried value) inputs
                    for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                        Value *inVal = PN->getIncomingValue(i);
                        customGraph.wireValueToNode(inVal, phiNode);
                    }

                } else {
                    // --- This is a standard MERGE node ---
                    phiNode->Type = DataflowOperatorType::Merge;
                    phiNode->Label = "M";

                    for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                        Value *inVal = PN->getIncomingValue(i);
                        BasicBlock *inBB = PN->getIncomingBlock(i);
                        auto *term = inBB->getTerminator();
                        
                        // Find the conditional branch that led to this PHI
                        if (auto* BI = dyn_cast<BranchInst>(term)) {
                            if (BI->isConditional()) {
                                // MODIFIED: "Get or create" steer nodes on-demand.
                                auto it = BranchSteers.find(BI);
                                if (it == BranchSteers.end()) {
                                    // Steers for this branch don't exist, create them now.
                                    auto [tS, fS] = createSteers(customGraph, BI->getCondition(), nullptr, nullptr);
                                    it = BranchSteers.insert({BI, {tS, fS}}).first;
                                }

                                // Determine if this is the true or false path for this PHI.
                                DataflowNode *steerNode;
                                if (BI->getSuccessor(0) == phiBlock) {
                                    steerNode = it->second.first;  // TrueSteer
                                } else {
                                    assert(BI->getSuccessor(1) == phiBlock && "PHI block is not a successor of the conditional branch");
                                    steerNode = it->second.second; // FalseSteer
                                }
                                    
                                // Wire the data value to the steer, and the steer to the merge.
                                customGraph.wireValueToNode(inVal, steerNode);
                                customGraph.addEdge(steerNode, phiNode);

                            } else {
                               // This path is from an unconditional branch, wire it directly.
                               customGraph.wireValueToNode(inVal, phiNode);
                            }
                        } else {
                           // Terminator is not a BranchInst (e.g. InvokeInst), wire directly.
                           customGraph.wireValueToNode(inVal, phiNode);
                        }
                    }
                }
                
                // Wire outputs from the Merge/Carry node to its users
                for (User *U : PN->users()) {
                    if (auto *userInst = dyn_cast<Instruction>(U)) {
                        if (auto *dest = customGraph.findNodeForValue(userInst)) {
                            customGraph.addEdge(phiNode, dest);
                        }
                    }
                }
            }
        }
    }

    // Pass 4: Add edges from Function Arguments to their users
    for (auto& Arg : F.args()) {
        DataflowNode* argNode = customGraph.findNodeForValue(&Arg);
        if (!argNode) continue; // Should not happen if getOrAdd was called for args

        // Iterate over the users of the original LLVM Argument
        for (User *user : Arg.users()) {
            // If the user is an instruction, wire it
            if (Instruction *userInst = dyn_cast<Instruction>(user)) {
                customGraph.wireValueToNode(&Arg, customGraph.getOrAdd(userInst));
            }
            // Handle cases where an argument is used by another Argument or Constant (less common)
        }
    }
    
    // Hook up memory-dependency edges (store->load) - this function
    // in CustomDataflowGraph is currently a placeholder as per its implementation.
    customGraph.addMemDepEdges();
    customGraph.removeNode(customGraph.findNodeForValue(const_duplicate));

    // Print the custom graph to a DOT file
    printCustomDFGToFile(customGraph, "dfg.dot");
    
    // This pass only builds a representation and doesn't modify the IR
    // so it preserves all analyses. If you modify IR, return PreservedAnalyses::none();
    return PreservedAnalyses::all();
  }
};
} // namespace

// Plugin registration for new LLVM pass manager
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "DataflowGraph", "v0.8", // Updated version
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "DataflowGraph") {
            FPM.addPass(DataflowGraph());
            return true;
          }
          return false;
        });
    }
  };
}