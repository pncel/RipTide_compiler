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
      DataflowNode *lastStore = nullptr;
      for (auto &S : Nodes) {
        if (S->Type == DataflowOperatorType::Store)
          lastStore = S.get();
        else if (S->Type == DataflowOperatorType::Load && lastStore)
          addEdge(lastStore, S.get());
      }
    }

    // Destructor to clean up memory
    ~CustomDataflowGraph() = default;
};


namespace {
class DataflowGraph : public PassInfoMixin<DataflowGraph> {

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
        case DataflowOperatorType::Select: return "diamond";
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
            case DataflowOperatorType::Select: label = "Select"; break;
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
            } else if (isa<SelectInst>(&I)) {
                 opType = DataflowOperatorType::Select;
                 label = "sel"; // Based on image
            }
            // Add checks for other instruction types you want to represent

            if (opType != DataflowOperatorType::Unknown || !label.empty()) {
                 customGraph.addNode(opType, &I, label);
            }
        }
    }

    // Add nodes for function arguments
    for (auto& Arg : F.args()) {
        customGraph.addNode(DataflowOperatorType::Unknown, &Arg, Arg.getName().str());
    }

    // Second pass: Add edges based on data dependencies and introduce steer/merge operators
    for (auto &BB : F) {
        for (auto &I : BB) {
            DataflowNode* sourceNode = customGraph.findNodeForValue(&I);
            if (!sourceNode) continue; // Skip instructions we didn't create nodes for

            // Handle data dependencies for basic operations
            // Edges go from the definition to the use
            for (User *user : I.users()) {
                 if (Instruction *dep = dyn_cast<Instruction>(user)) {
                     DataflowNode* destNode = customGraph.findNodeForValue(dep);
                     if (destNode) {
                         // Don't add direct edges from comparison/branch if they are handled by steer
                         bool isSteerSource = isa<ICmpInst>(&I) || isa<FCmpInst>(&I) || isa<BranchInst>(&I);
                         bool isSteerDest = destNode->Type == DataflowOperatorType::TrueSteer || destNode->Type == DataflowOperatorType::FalseSteer;

                         if (!isSteerSource || !isSteerDest) {
                              customGraph.addEdge(sourceNode, destNode);
                         }
                     }
                 } else if (Argument* argUser = dyn_cast<Argument>(user)) {
                     // Handle cases where an instruction's result is used by an argument (less common in standard DFG)
                 } else if (Constant* constUser = dyn_cast<Constant>(user)) {
                     // Handle cases where an instruction's result is used by a constant (unusual)
                 }
            }


            // Handle conditional branches and introduce Steer/Merge
            if (auto *BI = dyn_cast<BranchInst>(&I)) {
              if (!BI->isConditional()) continue;

              // 1) make sure there's a node for the condition
              Value *condValue = BI->getCondition();
              DataflowNode *condNode = customGraph.findNodeForValue(condValue);
              if (!condNode)
                condNode = customGraph.addNode(DataflowOperatorType::Unknown,
                                              condValue,
                                              "Cond: " +
                                                (condValue->getName().str()));

              // 2) find the common post-dominator block
              BasicBlock *trueBB  = BI->getSuccessor(0);
              BasicBlock *falseBB = BI->getSuccessor(1);
              BasicBlock *mergeBB = PDT.findNearestCommonDominator(trueBB, falseBB);

              
              if (!mergeBB) continue;

              // 3) Create a single pair of steer nodes per branch site
              DataflowNode *trueSteerNode  =
                customGraph.addNode(DataflowOperatorType::TrueSteer,  nullptr, "T");
              DataflowNode *falseSteerNode =
                customGraph.addNode(DataflowOperatorType::FalseSteer, nullptr, "F");
              customGraph.addEdge(condNode, trueSteerNode);
              customGraph.addEdge(condNode, falseSteerNode);

              // 4) For each PHI in the merge block, connect incoming → steer → merge
              for (auto &MergeI : *mergeBB) {
                if (auto *PN = dyn_cast<PHINode>(&MergeI)) {
                  DataflowNode *mergeNode = customGraph.findNodeForValue(PN);
                  if (!mergeNode)
                    mergeNode = customGraph.addNode(DataflowOperatorType::Merge, PN, "M");

                   // 2) Create explicit True/False Steer nodes
                   DataflowNode *trueSteer  = customGraph.addNode(DataflowOperatorType::TrueSteer,
                                                                  nullptr, "T");
                  DataflowNode *falseSteer = customGraph.addNode(DataflowOperatorType::FalseSteer,
                                                                  nullptr, "F");
                  customGraph.addEdge(condNode, trueSteer);
                  customGraph.addEdge(condNode, falseSteer);

                  // 3) Rewire: T/F → Merge instead of select
                  customGraph.addEdge(trueSteer,  mergeNode);
                  customGraph.addEdge(falseSteer, mergeNode);

                  // If this PHI is in a loop header, create its Carry node now
                  if (Loop *L = LI.getLoopFor(PN->getParent())) {
                    // 4) Create Carry operator for the loop‐carried phi
                    DataflowNode *carry = customGraph.addNode(DataflowOperatorType::Carry,
                                                              PN, "C");
                    customGraph.addEdge(mergeNode, carry);
                    // remember it in a map if you like, or find it later
                  }

                  // Connect each incoming value to the correct steer
                  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
                    Value *inVal   = PN->getIncomingValue(i);
                    BasicBlock *inBB = PN->getIncomingBlock(i);
                    auto *inNode = customGraph.findNodeForValue(inVal);
                    if (!inNode) {
                      std::string s;
                      raw_string_ostream ss(s);
                      inVal->print(ss);
                      inNode = customGraph.addNode(DataflowOperatorType::Unknown,
                                                  inVal, ss.str());
                    }
                    if (inBB == trueBB)
                      customGraph.addEdge(inNode, trueSteerNode);
                    else if (inBB == falseBB)
                      customGraph.addEdge(inNode, falseSteerNode);
                  }
                }
              }
            }
        }
      }

    // Now that all PHIs & merges exist, hook up loop‐back and carry→STR
    for (auto &BB : F) {

      auto SI = StreamMap.find(&BB);
      if (SI == StreamMap.end()) continue;
      DataflowNode *StrNode = SI->second;

      // For each PHI in this header, wire back‐edge into STR,
      // and STR into its carry‐node if we made one.
      for (Instruction &X : BB) {
        if (auto *PN = dyn_cast<PHINode>(&X)) {
          // assume your carry-node is the one you added before the merge
          DataflowNode *CarryNode = nullptr;
          
          for (auto &Eptr : customGraph.Nodes) {
            DataflowNode *E = Eptr.get();
            if (E->Type == DataflowOperatorType::Carry && E->Label == "C")
              CarryNode = E;
          }
          // connect PN’s incoming from latch → STR
          // find the actual loop latch from LI
          Loop *L = LI.getLoopFor(&BB);

          if (!L) continue;
          BasicBlock *Latch = L->getLoopLatch();
          if (!Latch) continue;
          Value *BackVal = PN->getIncomingValueForBlock(Latch);
          
          if (auto *BackNode = customGraph.findNodeForValue(BackVal))
            customGraph.addEdge(BackNode, StrNode);

          // connect STR → carry (so the next iter sees the carried value)
          if (CarryNode)
            customGraph.addEdge(StrNode, CarryNode);
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
