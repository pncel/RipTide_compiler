#include "llvm/IR/Function.h"
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

#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory> // For unique_ptr
#include <algorithm> // For std::find
#include <system_error>

using namespace llvm;
using namespace llvm::sys;

// Define the types of custom dataflow operators
enum class DataflowOperatorType {
    Unknown,
    BasicBinaryOp, // For arithmetic, bitwise, etc.
    Load,
    Store,
    Select, // If we decide to include LLVM's select
    TrueSteer, // TIF, representing conditional data steering
    FalseSteer, // F, representing conditional data steering
    Merge, // M, corresponds to PHI
    Carry, // C, for loop-carried values
    Invariant, // I, for loop invariants
    Order, // O, for synchronization/ordering
    Stream // STR, for stream processing/loops
};

// Forward declaration
struct DataflowNode;

// Represents an edge in the custom dataflow graph
struct DataflowEdge {
    DataflowNode* Source;
    DataflowNode* Destination;
    // Could add edge labels if needed (e.g., for conditional outputs)
};

// Represents a node (operator) in the custom dataflow graph
struct DataflowNode {
    DataflowOperatorType Type;
    const Value* OriginalValue; // Reference to the original LLVM Value (Instruction, Argument, Constant)
    std::vector<DataflowEdge*> Inputs;
    std::vector<DataflowEdge*> Outputs;
    std::string Label; // Label for the DOT graph

    // Constructor
    DataflowNode(DataflowOperatorType type, const Value* originalValue = nullptr, const std::string& label = "")
        : Type(type), OriginalValue(originalValue), Label(label) {}

    // Destructor
    ~DataflowNode() = default;
};

// Represents the entire custom dataflow graph
struct CustomDataflowGraph {
    std::vector<std::unique_ptr<DataflowNode>> Nodes;
    std::vector<std::unique_ptr<DataflowEdge>> Edges;
    // Map original LLVM Values to their corresponding custom graph nodes
    std::map<const Value*, DataflowNode*> ValueToNodeMap;

    DataflowNode* getOrAdd(Value *V) {
      if (auto *N = findNodeForValue(V)) return N;
      std::string tmp; raw_string_ostream ss(tmp);
      V->print(ss);
      return addNode(DataflowOperatorType::Unknown, V, ss.str());
    }

    // Add a node to the graph
    DataflowNode* addNode(DataflowOperatorType type, const Value* originalValue = nullptr, const std::string& label = "") {
        Nodes.push_back(std::make_unique<DataflowNode>(type, originalValue, label));
        DataflowNode* newNode = Nodes.back().get();
        if (originalValue) {
            ValueToNodeMap[originalValue] = newNode;
        }
        return newNode;
    }

    // Add an edge to the graph
    void addEdge(DataflowNode* source, DataflowNode* destination) {
        // Prevent duplicate edges
        for (const auto& edge : source->Outputs) {
            if (edge->Destination == destination) {
                return;
            }
        }
        Edges.push_back(std::make_unique<DataflowEdge>());
        DataflowEdge* edge = Edges.back().get();
        edge->Source = source;
        edge->Destination = destination;
        source->Outputs.push_back(edge);
        destination->Inputs.push_back(edge);
    }

    // Helper to find a node representing an original LLVM Value
    DataflowNode* findNodeForValue(const Value* val) const {
        auto it = ValueToNodeMap.find(val);
        if (it != ValueToNodeMap.end()) {
            return it->second;
        }
        return nullptr;
    }

    // New helper: hook all loads after a store into that store
    void addMemDepEdges() {
      // This is a very simplistic memory dependency model.
      // A proper one would require alias analysis.
      // This connects *any* load to the *last* store encountered in instruction order,
      // which is generally incorrect. For a proper DFG, memory dependencies are complex.
      // I'm keeping it as is for now based on the original code, but note this limitation.
      DataflowNode *lastStore = nullptr;
      for (auto &S : Nodes) {
        if (S->Type == DataflowOperatorType::Store)
          lastStore = S.get();
        // Only add dependency if the load is *after* the store in program order
        // and potentially accesses the same memory. This simple pass doesn't track order
        // precisely across blocks or perform alias analysis.
        // The original code just connected any load after any store, which is wrong.
        // Removing this simplistic memory dependency for now.
        // else if (S->Type == DataflowOperatorType::Load && lastStore)
        //   addEdge(lastStore, S.get());
      }
    }

    // Destructor to clean up memory
    ~CustomDataflowGraph() = default;
};


namespace {
class DataflowGraph : public PassInfoMixin<DataflowGraph> {
    // Helper to unify steering logic
    std::pair<DataflowNode*,DataflowNode*>
    createSteers(CustomDataflowGraph &G,
                 Value *Cond, Value *TV, Value *FV) {

        auto *condN = G.findNodeForValue(Cond);
        if (!condN)
            condN = G.addNode(DataflowOperatorType::Unknown,
                              Cond,
                              "Cond:"+Cond->getName().str());
        auto *tS = G.addNode(DataflowOperatorType::TrueSteer, nullptr, "T");
        auto *fS = G.addNode(DataflowOperatorType::FalseSteer,nullptr, "F");
        G.addEdge(condN, tS);
        G.addEdge(condN, fS);
        if (TV) G.addEdge(G.getOrAdd(TV), tS);
        if (FV) G.addEdge(G.getOrAdd(FV), fS);
        return {tS, fS};
    }
  // Function to print the custom graph to a DOT file
  void printCustomDFGToFile(const CustomDataflowGraph &customGraph, const std::string &filename) const {
    std::error_code EC;
    raw_fd_ostream outFile(filename, EC, sys::fs::OF_Text);
    if (EC) {
      errs() << "Error opening file " << filename << ": " << EC.message() << "\n";
      return;
    }

    outFile << "digraph \"custom_dfg\" {\n";
    // Map DataflowNode pointers to DOT node names
    std::map<const DataflowNode*, std::string> nodeNames;
    int id = 0;

    // Define node shapes and labels based on operator type
    auto getNodeShape = [](DataflowOperatorType type) {
      switch (type) {
        case DataflowOperatorType::BasicBinaryOp: return "box";
        case DataflowOperatorType::Load: return "ellipse";
        case DataflowOperatorType::Store: return "ellipse";
        //case DataflowOperatorType::Select: return "diamond";
        case DataflowOperatorType::TrueSteer: return "triangle"; 
        case DataflowOperatorType::FalseSteer: return "invtriangle"; 
        case DataflowOperatorType::Merge: return "octagon"; 
        case DataflowOperatorType::Carry: return "box"; 
        case DataflowOperatorType::Invariant: return "box";
        case DataflowOperatorType::Order: return "box"; 
        case DataflowOperatorType::Stream: return "circle"; 
        default: return "box";
      }
    };

    auto getNodeLabel = [](const DataflowNode* node) {
        if (!node->Label.empty()) {
            return node->Label;
        }
        // Generate a default label based on operator type and original instruction
        std::string label;
        switch (node->Type) {
            case DataflowOperatorType::BasicBinaryOp: label = "BinOp"; break;
            case DataflowOperatorType::Load: label = "Load"; break;
            case DataflowOperatorType::Store: label = "Store"; break;
            //case DataflowOperatorType::Select: label = "Select"; break;
            case DataflowOperatorType::TrueSteer: label = "TrueSteer"; break;
            case DataflowOperatorType::FalseSteer: label = "FalseSteer"; break;
            case DataflowOperatorType::Merge: label = "Merge"; break;
            case DataflowOperatorType::Carry: label = "Carry"; break;
            case DataflowOperatorType::Invariant: label = "Invariant"; break;
            case DataflowOperatorType::Order: label = "Order"; break;
            case DataflowOperatorType::Stream: label = "Stream"; break;
            default: label = "Unknown"; break;
        }
        if (node->OriginalValue) {
            std::string valueStr;
            raw_string_ostream ss(valueStr);
            node->OriginalValue->print(ss);
            label += "\\n" + ss.str();
        }
        return label;
    };


    for (const auto& nodePtr : customGraph.Nodes) {
      // SKip empty nodes
      if (nodePtr->Inputs.empty() && nodePtr->Outputs.empty())
        continue;
      const DataflowNode* node = nodePtr.get();
      std::string nodeName = "node" + std::to_string(id++);
      nodeNames[node] = nodeName;

      outFile << "  \"" << nodeName << "\" [label=\"" << getNodeLabel(node)
              << "\", shape=\"" << getNodeShape(node->Type) << "\"];\n";
    }

    // Add edges
    for (const auto& edgePtr : customGraph.Edges) {
        const DataflowEdge* edge = edgePtr.get();
        outFile << "  \"" << nodeNames.at(edge->Source) << "\" -> \""
                << nodeNames.at(edge->Destination) << "\";\n";
    }

    outFile << "}\n";
}


public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    CustomDataflowGraph customGraph; 
    errs() << "Building Custom DFG for function: " << F.getName() << "\n";

    // Get analysis results
    // Get analysis results
    PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
    LoopInfo         &LI  = FAM.getResult<LoopAnalysis>(F);

    // For each natural loop, allocate a single Stream node at the header
    std::map<BasicBlock*,DataflowNode*> StreamMap;
    for (Loop *L : LI) {
      BasicBlock *H = L->getHeader();
      // Create one STR per loop
      StreamMap[H] = customGraph.addNode(DataflowOperatorType::Stream,
                                         nullptr, "STR");
    }

    // First pass: Create nodes for all relevant LLVM values (instructions, arguments, constants)
    // This helps in mapping users/operands to graph nodes later.
    for (auto& BB : F) {
        for (auto& I : BB) {
            DataflowOperatorType opType = DataflowOperatorType::Unknown;
            std::string label = "";

            if (I.isBinaryOp()) {
                opType = DataflowOperatorType::BasicBinaryOp;
                label = Instruction::getOpcodeName(I.getOpcode());
            } else if (isa<LoadInst>(&I)) {
                opType = DataflowOperatorType::Load;
                label = "ld";
            } else if (isa<StoreInst>(&I)) {
                opType = DataflowOperatorType::Store;
                label = "st";
            } else if (isa<ICmpInst>(&I) || isa<FCmpInst>(&I)) {
                 // Comparisons will be linked to Steer nodes, create a node for the comparison result
                 opType = DataflowOperatorType::BasicBinaryOp; // Treat comparison as a binary op for its result
                 label = Instruction::getOpcodeName(I.getOpcode());
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

            if (opType != DataflowOperatorType::Unknown || !label.empty()) {
                 customGraph.addNode(opType, &I, label);
            }
        }
    }

    // Add nodes for function arguments
    for (auto& Arg : F.args()) {
        customGraph.getOrAdd(&Arg); // This adds the node if it doesn't exist
        auto *node = customGraph.findNodeForValue(&Arg);
        if (node && node->Label.empty()) {
             node->Label = Arg.getName().str();
        }
    }

    // Add nodes for constants used
    // This is a bit more involved to find *all* used constants.
    // A simple approach is to iterate through operands of instructions and getOrAdd constants.
     for (auto& BB : F) {
        for (auto& I : BB) {
            for (auto &Op : I.operands()) {
                if (auto *C = dyn_cast<Constant>(Op)) {
                    customGraph.getOrAdd(C);
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


          // (4) now re-wire **all** users of the LLVM select → our two steers
          for (auto *U : SI->users()) {
            if (auto *userInst = dyn_cast<Instruction>(U)) {
              if (auto *dest = customGraph.findNodeForValue(userInst)) {
                customGraph.addEdge(tS, dest);
                customGraph.addEdge(fS, dest);
              }
            }
          }
        }
      }
    }


      // Pass 2: Add edges based on data dependencies and handle special instructions
      for (auto &BB : F) {
        for (auto &I : BB) {
            DataflowNode* sourceNode = customGraph.findNodeForValue(&I);
            // Source node might not exist for instructions we chose to skip (e.g., some control flow)
            // Or if it's a PHINode, we will handle edges in the next pass.
             if (!sourceNode || isa<PHINode>(&I)) continue;

            // Handle data dependencies for basic operations
            // Edges go from the definition to the use
            for (User *user : I.users()) {
                 if (Instruction *dep = dyn_cast<Instruction>(user)) {
                     DataflowNode* destNode = customGraph.findNodeForValue(dep);
                     if (destNode) {
                         // Add data dependency edge
                         // We will handle conditional branches and selects via steers
                         // and PHIs in a separate pass.
                         bool isSteerSource = isa<ICmpInst>(&I) || isa<FCmpInst>(&I);
                         bool isSteerDest = destNode->Type == DataflowOperatorType::TrueSteer || destNode->Type == DataflowOperatorType::FalseSteer;
                         bool isBranchInst = isa<BranchInst>(&I);
                         bool isPHIDest = isa<PHINode>(dep); // PHI edges handled in pass 3

                         if (!isPHIDest && !isBranchInst && (!isSteerSource || !isSteerDest)) {
                             customGraph.addEdge(sourceNode, destNode);
                         }
                     }
                 } else if (Argument* argUser = dyn_cast<Argument>(user)) {
                     // Handle cases where an instruction's result is used by an argument (less common in standard DFG)
                     // This might happen if building a graph for a function called by this one.
                     // For a single-function DFG, this is less relevant.
                 }
            }

            // Handle conditional branches and introduce Steer
             if (auto *BI = dyn_cast<BranchInst>(&I)) {
                 if (!BI->isConditional()) continue;

                 // Create steer nodes for the branch condition
                 auto [tS, fS] = createSteers(customGraph,
                                              BI->getCondition(),
                                              nullptr, // Branch doesn't have data values itself
                                              nullptr); // Steers connected to condition, then values routed

                 // The steer nodes represent the control flow divergence.
                 // Dataflow through the branches is handled by PHI nodes at the merge point.
                 // We do *not* connect steers directly to the PHI nodes here in the standard model.
                 // The steer's outputs would influence which path is taken, affecting which
                 // incoming value a PHI node receives, but this is control flow, not data flow edges
                 // in the DFG itself.
             } else if (auto *SI = dyn_cast<SelectInst>(&I)) {
                 // Convert select nodes to T/F steers and route their inputs/outputs
                 auto [tS, fS] = createSteers(customGraph,
                                              SI->getCondition(),
                                              SI->getTrueValue(),
                                              SI->getFalseValue());

                 // The output of the select is now represented by the outputs of the steers.
                 // Re-wire users of the select instruction to use the steer nodes' outputs.
                 DataflowNode* selectNode = customGraph.findNodeForValue(SI);
                 if (selectNode) {
                     // Remove existing edges from the original select node if any were added
                     // This might be tricky with the current addEdge logic preventing duplicates.
                     // A better approach might be to not add edges *from* the select instruction
                     // in the main dependency loop if we know we'll replace it with steers.
                     // For simplicity now, users of the SelectInst will be connected to the steers.
                     for (auto *U : SI->users()) {
                         if (auto *userInst = dyn_cast<Instruction>(U)) {
                             if (auto *dest = customGraph.findNodeForValue(userInst)) {
                                 customGraph.addEdge(tS, dest); // Output of TrueSteer goes to user
                                 customGraph.addEdge(fS, dest); // Output of FalseSteer goes to user
                             }
                         }
                     }
                 }
             }
        }
      }


      // Pass 3: Add edges specifically for PHI nodes (Merges)
      for (auto &BB : F) {
         for (auto &I : BB) {
             if (auto *PN = dyn_cast<PHINode>(&I)) {
                 DataflowNode *mergeNode = customGraph.findNodeForValue(PN);
                 if (!mergeNode) {
                     // This shouldn't happen if Pass 1 is correct, but as a safeguard:
                     mergeNode = customGraph.addNode(DataflowOperatorType::Merge, PN, "M");
                 }

                 // Connect incoming values to the Merge node
                 for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
                     Value *inVal = PN->getIncomingValue(i);
                     // BasicBlock *inBB = PN->getIncomingBlock(i); // Not needed for simple PHI data edges

                     DataflowNode *inNode = customGraph.getOrAdd(inVal); // Ensure node exists for incoming value

                     // Add edge from the incoming value node to the Merge node
                     customGraph.addEdge(inNode, mergeNode);
                 }

                 // Handle loop-carried values. If this PHI is loop-carried, add a Carry node
                 // and connect the Merge node to it.
                 if (Loop *L = LI.getLoopFor(PN->getParent())) {
                      // Create a Carry node associated with this specific PHI
                      DataflowNode *carryNode = customGraph.addNode(DataflowOperatorType::Carry, PN, "C");
                      // Connect the Merge node to the Carry node
                      customGraph.addEdge(mergeNode, carryNode);

                      // This Carry node represents the value being carried to the next iteration.
                      // How this Carry node connects back to the loop's entry (possibly via a Stream node)
                      // depends on the desired loop DFG model.
                      // The original code had a separate loop for this which is prone to errors.
                      // Let's add connections from the Carry node to the Stream node for this loop header.
                      if (StreamMap.count(L->getHeader())) {
                          DataflowNode *strNode = StreamMap.at(L->getHeader());
                          // Connect the Carry node to the Stream node (value available for next iteration)
                          customGraph.addEdge(carryNode, strNode);
                      }
                 }
             }
         }
     }

      // The loop handling connecting the loop-back value to the Stream node.
      for (auto &BB : F) {
        if (Loop *L = LI.getLoopFor(&BB)) {
            // If this block is a loop latch, connect its terminator's relevant operand
            // (the loop-back value) to the Stream node for the loop header.
            if (L->isLoopLatch(&BB)) {
                if (Instruction *Term = BB.getTerminator()) {
                    // For a branch, the condition might be relevant, but for the data DFG,
                    // we're interested in loop-carried values which pass through PHI nodes.
                    // The loop-back value for a PHI is the incoming value from the latch block.
                     BasicBlock *Header = L->getHeader();
                     for (auto &HInstr : *Header) {
                         if (auto *PN = dyn_cast<PHINode>(&HInstr)) {
                             Value *BackVal = PN->getIncomingValueForBlock(&BB); // Get value from latch
                             DataflowNode *BackNode = customGraph.findNodeForValue(BackVal);
                             if (BackNode && StreamMap.count(Header)) {
                                  DataflowNode *StrNode = StreamMap.at(Header);
                                 // Connect the loop-back value's node to the Stream node
                                  customGraph.addEdge(BackNode, StrNode);
                             }
                         }
                     }
                }
            }
        }
      }

    // Hook up memory‐dependency edges (store→load)
    customGraph.addMemDepEdges();

    printCustomDFGToFile(customGraph, "dfg.dot");
    return PreservedAnalyses::all();
  }
};
} // namespace

// Plugin registration for new LLVM pass manger
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "DataflowGraph", "v0.4", // Updated version
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
