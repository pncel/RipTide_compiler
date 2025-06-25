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

  // We'll use a single stream token at function entry as the data input for
  // all branch-based steering.
  DataflowNode *getOrCreateFuncEntryStream(CustomDataflowGraph &G, Function &F) {
    static const char *ENTRY_LABEL = "_entry_stream";
    // Only create once per function
    if (auto *N = G.findNodeForValue(reinterpret_cast<const Value*>(ENTRY_LABEL)))
      return N;
    // Add a special stream node
    auto *streamN = G.addNode(DataflowOperatorType::Stream, nullptr, "STR");
    // "Tie" it to a fake Value* so we can look it up again
    G.ValueToNodeMap[reinterpret_cast<const Value*>(ENTRY_LABEL)] = streamN;
    return streamN; 
  }
  
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
          ICmpInst *CI = nullptr;
          FCmpInst *FI = nullptr;
          if (isa<SelectInst>(&I) || isa<GetElementPtrInst>(&I)
            || (isa<BranchInst>(&I) && cast<BranchInst>(&I)->isConditional()) ||
            isa<CastInst>(&I) )
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
          } else if (isa<LoadInst>(&I)) {
             opType = DataflowOperatorType::Load;
             label = "ld";
          } else if (isa<StoreInst>(&I)) {
              opType = DataflowOperatorType::Store;
              label = "st";
          } else if (CI = dyn_cast<ICmpInst>(&I)) {
              // Comparisons will be linked to Steer nodes, create a node for the comparison result
              label = Instruction::getOpcodeName(I.getOpcode());
              opType = DataflowOperatorType::BasicBinaryOp;
              switch (CI->getPredicate()) {
                case ICmpInst::ICMP_EQ:  sym = "=="; break;
                case ICmpInst::ICMP_NE:  sym = "!="; break;
                case ICmpInst::ICMP_SLT: sym = "<";  break;
                case ICmpInst::ICMP_SLE: sym = "<="; break;
                case ICmpInst::ICMP_SGT: sym = ">";  break;
                case ICmpInst::ICMP_SGE: sym = ">="; break;
                default:                 sym = CmpInst::getPredicateName(CI->getPredicate()).str();
              }
          } else if (FI = dyn_cast<FCmpInst>(&I)) {
              opType = DataflowOperatorType::BasicBinaryOp;
              sym = CmpInst::getPredicateName(FI->getPredicate()).str();
          } else if (isa<PHINode>(&I)) {
              opType = DataflowOperatorType::Merge;
              label = "M"; // Based on image
          } else if (isa<CallInst>(&I)) {
              // Handle function calls - could be basic ops or more complex
              label = "call"; // Generic label for now
          } else if (isa<ReturnInst>(&I)) {
              label = "ret"; // Still include return for graph termination visualization
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

    // Pass 1.3: handle all conditional branches up-front
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *BI = dyn_cast<BranchInst>(&I)) {
          if (BI->isConditional()) {
            // Setup steer nodes and wire condition
            auto [tS,fS] = createSteers(customGraph,
                                      BI->getCondition(),
                                      nullptr,
                                      nullptr); 
            BranchSteers[BI] = {tS,fS};
            
            // Hook the entry stream into each branch-steer
            DataflowNode *entryStr = getOrCreateFuncEntryStream(customGraph, F);
            customGraph.addEdge(entryStr, tS);
            customGraph.addEdge(entryStr, fS);
             
            // Wire each steer into the first "real" instruction of its successor,
            // skipping PHI, GEP, Casts which we treat as transparent.
            auto skipPassThroughs = [](BasicBlock *BB) -> Instruction* {
              for (auto &I_in_bb : *BB) { // Renamed 'I' to 'I_in_bb' to avoid conflict
                if (isa<PHINode>(I_in_bb))          continue;
                if (isa<GetElementPtrInst>(I_in_bb)) continue;
                if (isa<CastInst>(I_in_bb))         continue;
                return &I_in_bb;
              }
              return nullptr;
            };
            if (auto *succ = BI->getSuccessor(0)) {
              if (auto *realI = skipPassThroughs(succ))
                customGraph.addEdge(tS, customGraph.getOrAdd(realI));
            }
            if (auto *succ = BI->getSuccessor(1)) {
              if (auto *realI = skipPassThroughs(succ))
                customGraph.addEdge(fS, customGraph.getOrAdd(realI));
            }
          } 
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
            // TODO: Handle cases where a SelectInst is used by an Argument or Constant (less common)
          }
        }
      }
    }

    // Pass 2: Add edges based on data dependencies and handle special instructions
    for (auto &BB : F) {
      for (auto &I : BB) {
          // wire Load pointer into the Load node
          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            if (auto *loadNode = customGraph.findNodeForValue(&I)) {
              // wire the address operand (so GEP→ZExt→%m path reaches the load)
              customGraph.wireValueToNode(LI->getPointerOperand(), loadNode);
            }
            // No need to do any further wiring for loads, as outputs are handled by users
            //continue; // Skip generic wiring below for loads
          }
          // wire Store operands into the Store node
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
            for (auto &op : I.operands()) {
              if (auto *C = dyn_cast<Constant>(op)) {
                customGraph.addEdge(customGraph.getOrAdd(C), instN);
              }
            }
          }
          
          // skip control ops entirely or types handled explicitly above
          if (isa<BranchInst>(&I)   ||
            isa<PHINode>(&I)      ||
            isa<SelectInst>(&I)
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

      // Pass 3: Add edges specifically for PHI nodes (Merges)
      for (auto &BB : F) {
         for (auto &I : BB) {
            if (auto *PN = dyn_cast<PHINode>(&I)) {

              DataflowNode *mergeNode = customGraph.findNodeForValue(PN);
                if (!mergeNode) {
                    // This should have been added in Pass 1, but as a safeguard:
                    mergeNode = customGraph.addNode(DataflowOperatorType::Merge, PN, "M");
                }
                
                // Connect incoming values to the Merge node
                for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                  Value      *inVal = PN->getIncomingValue(i);
                  BasicBlock *inBB  = PN->getIncomingBlock(i);

                  // 1) Wire the “true or false” steer node
                  if (auto *term = dyn_cast<BranchInst>(inBB->getTerminator()))
                    if (term->isConditional()) {
                      auto it = BranchSteers.find(term);
                      if (it != BranchSteers.end()) {
                        DataflowNode *steer =
                          (term->getSuccessor(0) == PN->getParent())
                            ? it->second.first   // true‐steer
                            : it->second.second; // false‐steer
                        if (steer)
                          customGraph.addEdge(steer, mergeNode);
                      }
                    }

                  // 2) Wire the “decider” itself (the icmp/fcmp) as D
                  if (auto *term = dyn_cast<BranchInst>(inBB->getTerminator()))
                    if (term->isConditional()) {
                      DataflowNode *condN = customGraph.findNodeForValue(term->getCondition());
                      if (condN)
                        customGraph.addEdge(condN, mergeNode);
                    }

                  // 3) Wire the actual data value (A or B)
                  // Use wireValueToNode to handle GEPs and Casts transparently
                  customGraph.wireValueToNode(inVal, mergeNode);
              }
              // Wire outputs: from Merge node to its users
              for (User *U : PN->users()) {
                if (auto *userInst = dyn_cast<Instruction>(U)) {
                  if (auto *dest = customGraph.findNodeForValue(userInst)) {
                    customGraph.addEdge(mergeNode, dest);
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
            // TODO: Handle cases where an argument is used by another Argument or Constant (less common)
        }
    }
    
    // Hook up memory-dependency edges (store->load) - this function
    // in CustomDataflowGraph is currently a placeholder as per its implementation.
    customGraph.addMemDepEdges();

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
    LLVM_PLUGIN_API_VERSION, "DataflowGraph", "v0.7", // Updated version
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
