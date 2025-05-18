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
#include <list>
#include <vector>
#include <memory> // For unique_ptr
#include <algorithm> // For std::find
#include <system_error>

using namespace llvm;
using namespace llvm::sys;

// Define the types of custom dataflow operators
enum class DataflowOperatorType {
    Unknown,
    FunctionInput, // Represents a function argument - ADDED
    FunctionOutput, // Represents a return value - ADDED (Good practice)
    Constant, // Represents a constant value - ADDED
    BasicBinaryOp, // For arithmetic, bitwise, etc.
    Load,
    Store,
    TrueSteer, // TIF, representing conditional data steering
    FalseSteer, // F, representing conditional data steering
    Merge, /* M, The merge operator enforces cross-iteration ordering by
              making sure that tokens from different loop iterations appear in
              the same order, regardless of the control path taken within by
              each loop iteration. The operator takes three inputs:a decider,
              D, and two data inputs, A and B. Merge is essentially a mux
              that passes through either A or B, depending on D. But note
              that only the value passed through is consumed. */
    Carry, // C, for loop-carried values
    Invariant, /* I, Theinvariantoperator isaslightvariationofcarry.
                  It represents a loop invariant andcanbe implementedasa
                  carry with a self-edge back to B.Invariants are used to generate
                  a new loop-invariant token for each loop iteration. */
    Order, /* O, Order.Theorderoperator isusedtoenforcememoryordering
            by guaranteeing that multiple preceding operations have
            executed. It takes two inputs, A and B, and fires as soon as
            both arrive,passing B through.*/
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
  std::list<std::unique_ptr<DataflowNode>> Nodes;
  std::vector<std::unique_ptr<DataflowEdge>> Edges;
  // Map original LLVM Values to their corresponding custom graph nodes
  std::map<const Value*, DataflowNode*> ValueToNodeMap;

  /// Recursively wire V into destN, but if V is either a GEP
  /// or any instruction whose node type is still Unknown,
  /// don’t create a node for V—just forward its operands.
  void wireValueToNode(Value  *V, DataflowNode *destN) {
    if (!V || !destN) return;

    // Unwrap GEP transparently
    if (isa<GetElementPtrInst>(V)) {
        auto *GEP = cast<GetElementPtrInst>(V);
        wireValueToNode(GEP->getPointerOperand(), destN);
        for (Use &IU : GEP->indices())
            wireValueToNode(IU.get(), destN);
        return;
    }

    // Handle zero/sign extend transparently
    if (isa<ZExtInst>(V) || isa<SExtInst>(V)) {
        if (auto *Z = dyn_cast<ZExtInst>(V)) {
            wireValueToNode(Z->getOperand(0), destN);
        } else if (auto *S = dyn_cast<SExtInst>(V)) {
            wireValueToNode(S->getOperand(0), destN);
        }
        return;
    }

    // If a real node exists, hook up directly
    if (auto *srcN = findNodeForValue(V)) {
        if (srcN->Type != DataflowOperatorType::Unknown) {
            addEdge(srcN, destN);
            return;
        }
        // Unknown → fall through to unwrap
    }

    // For instructions with operands, recurse
    if (auto *I = dyn_cast<Instruction>(V)) {
        for (Value *op : I->operand_values())
            wireValueToNode(op, destN);
    }

  }

    // Modified getOrAdd to set basic types for Arguments and Constants
    DataflowNode* getOrAdd(Value *V) {
      // never materialize conditional branches as nodes
      if (auto *BI = dyn_cast<BranchInst>(V))
        if (BI->isConditional())
          return nullptr;
      if (isa<GetElementPtrInst>(V) || isa<ZExtInst>(V) || isa<SExtInst>(V))
        return nullptr;
      if (!V) return nullptr; // Handle null values gracefully
      if (auto *N = findNodeForValue(V)) return N;

      // Determine initial type based on Value type
      DataflowOperatorType type = DataflowOperatorType::Unknown;
      if (isa<Argument>(V)) type = DataflowOperatorType::FunctionInput;
      else if (isa<Constant>(V)) type = DataflowOperatorType::Constant;
      return addNode(type, V); // Initial label can be empty and generated later
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
      if (!source) {
        // Optionally, llvm::report_fatal_error("Null source in addEdge");
        return;
      }
      if (!destination) {
        // Optionally, llvm::report_fatal_error("Null destination in addEdge");
        return;
      }  
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
        //   wireValueToNode(lastStore, S.get());
      }
    }

    // Destructor to clean up memory
    ~CustomDataflowGraph() = default;
};


namespace {
class DataflowGraph : public PassInfoMixin<DataflowGraph> {

  // ----------------------------------------------------------------------------
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

  std::map<BranchInst*, std::pair<DataflowNode*,DataflowNode*>> BranchSteers;
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
        case DataflowOperatorType::FunctionInput: return "ellipse"; // ADDED shape
        case DataflowOperatorType::FunctionOutput: return "ellipse"; // ADDED shape
        case DataflowOperatorType::Constant: return "box"; // ADDED shape
        case DataflowOperatorType::BasicBinaryOp: return "box";
        case DataflowOperatorType::Load: return "ellipse";
        case DataflowOperatorType::Store: return "ellipse";
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
            case DataflowOperatorType::FunctionInput: label = "Input"; break;
            case DataflowOperatorType::FunctionOutput: label = "Output"; break;
            case DataflowOperatorType::Constant: label = "Const"; break;
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

    // ADDED: Lambda to check if a node should be included even if empty
    auto shouldIncludeEmptyNode = [](const DataflowNode* node) {
      return node->Type == DataflowOperatorType::FunctionInput  ||
          node->Type == DataflowOperatorType::FunctionOutput ||
          node->Type == DataflowOperatorType::Merge        ;
    };

    for (const auto& nodePtr : customGraph.Nodes) {
      // Skip empty nodes
      if (nodePtr->Inputs.empty() && nodePtr->Outputs.empty())
        continue;
      if (nodePtr->Inputs.empty() && nodePtr->Outputs.empty() && !shouldIncludeEmptyNode(nodePtr.get())) continue; // MODIFIED condition
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
    // PostDominatorTree &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
    LoopInfo         &LI  = FAM.getResult<LoopAnalysis>(F);

    // First pass: Create nodes for all relevant LLVM values (instructions, arguments, constants)
    // This helps in mapping users/operands to graph nodes later.
    for (auto& BB : F) {
        for (auto& I : BB) {
          if (isa<SelectInst>(&I) || isa<GetElementPtrInst>(&I)
            || (isa<BranchInst>(&I) && cast<BranchInst>(&I)->isConditional()) ||
            isa<ZExtInst>(&I) || isa<SExtInst>(&I))
            continue;
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

          // Get or create the node for the instruction
          DataflowNode* instNode = customGraph.getOrAdd(&I);

          // Update the type and label if determined
          if (instNode) {
            if (opType != DataflowOperatorType::Unknown) instNode->Type = opType;
            if (!label.empty()) instNode->Label = label; // Prioritize explicit labels
          }
        }
    }

    // ==== PASS 1.3: handle all branches up‐front ====
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *BI = dyn_cast<BranchInst>(&I)) {
          if (BI->isConditional()) {
            // setup steer nodes and wire condition
            auto [tS,fS] = createSteers(customGraph,
                                        BI->getCondition(),
                                        /*no data value*/ nullptr,
                                        /*no data value*/ nullptr);
            BranchSteers[BI] = {tS,fS};

            // Hook the entry stream into each branch-steer
            DataflowNode *entryStr = getOrCreateFuncEntryStream(customGraph, F);
            customGraph.addEdge(entryStr, tS);
            customGraph.addEdge(entryStr, fS);

            // Wire each steer into the first “real” instruction of its successor,
            // skipping PHI, GEP, ZExt and SExt which we treat as transparent.
            auto skipPassThroughs = [](BasicBlock *BB) -> Instruction* {
              for (auto &I : *BB) {
                //if (isa<PHINode>(I))          continue;
                if (isa<GetElementPtrInst>(I)) continue;
                if (isa<ZExtInst>(I))         continue;
                if (isa<SExtInst>(I))         continue;
                return &I;
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
      if (argNode && argNode->Label.empty()) { // Don't overwrite existing labels if any
        std::string argLabel; raw_string_ostream ss(argLabel);
        ss << Arg; // Print arg name and type
        DataflowNode* argNode = customGraph.getOrAdd(&Arg);
        argNode->Type = DataflowOperatorType::FunctionInput;
        argNode->Label = ss.str();
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
          // Also add nodes for Arguments if they are operands (e.g., ptr args)
          if (auto *A = dyn_cast<Argument>(Op)) {
            customGraph.getOrAdd(A); // Type and label handled in getOrAdd
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
            // TODO: Handle cases where a SelectInst is used by an Argument or Constant (less common)
          }
        }
      }
    }


      // Pass 2: Add edges based on data dependencies and handle special instructions
      for (auto &BB : F) {
        for (auto &I : BB) {
          // --- new: wire Load pointer into the Load node ---
          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            if (auto *loadNode = customGraph.findNodeForValue(&I)) {
              // wire the address operand (so GEP→ZExt→%m path reaches the load)
              customGraph.wireValueToNode(LI->getPointerOperand(), loadNode);
            }
            
          }
          // --- new: wire Store operands into the Store node ---
          if (auto *SI = dyn_cast<StoreInst>(&I)) {
            if (auto *storeNode = customGraph.findNodeForValue(&I)) {
              // wire the *value* being stored (this is where '%m' travels)
              customGraph.wireValueToNode(SI->getValueOperand(), storeNode);
              // wire the *pointer* you're storing into
              customGraph.wireValueToNode(SI->getPointerOperand(), storeNode);
            }
            // no need to do any further wiring for stores
            continue;
          }
          // --- new: handle GEP as pure pass-through ---
          if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
            // for each user of the GEP, wire all of GEP’s operands directly to that user
            for (User *U : GEP->users()) {
              if (auto *userInst = dyn_cast<Instruction>(U)) {
                if (auto *dest = customGraph.findNodeForValue(userInst)) {
                  // wire base pointer
                  if (auto *base = GEP->getPointerOperand())
                    customGraph.addEdge(customGraph.getOrAdd(base), dest);
                  // wire any variable indices too (optional)
                  for (unsigned i = 1, e = GEP->getNumOperands(); i < e; ++i) {
                    if (auto *idx = dyn_cast<Value>(GEP->getOperand(i)))
                      customGraph.addEdge(customGraph.getOrAdd(idx), dest);
                  }
                }
              }
            }
            // skip all the rest of the generic wiring for this instruction
            continue;
          }
          // wire every constant operand into I
          if (DataflowNode *instN = customGraph.findNodeForValue(&I)) {
            for (auto &op : I.operands()) {
              if (auto *C = dyn_cast<Constant>(op)) {
                customGraph.addEdge(customGraph.getOrAdd(C), instN);
              }
            }
          }
          // skip control ops entirely
          if (isa<BranchInst>(&I)   ||
            isa<PHINode>(&I)      ||
            isa<SelectInst>(&I)   ||
            isa<GetElementPtrInst>(&I) ||
            isa<ZExtInst>(&I) || isa<SExtInst>(&I))
            continue;
          DataflowNode* sourceNode = customGraph.findNodeForValue(&I);
          // Source node might not exist for instructions we chose to skip (e.g., some control flow)
          // Or if it's a PHINode, we will handle edges in the next pass.
          // skip PHI (handled later), Select (steers), and GEP
          if (!sourceNode ||
            isa<PHINode>(&I) ||
            isa<SelectInst>(&I) ||
            isa<GetElementPtrInst>(&I) ||
            isa<ZExtInst>(&I) || isa<SExtInst>(&I)
          )
            continue;

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
          if (auto *SI = dyn_cast<SelectInst>(&I)) {
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
              // This might be tricky with the current wireValueToNode logic preventing duplicates.
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

              // —— new: wire in unconditional branches directly to the merge ——
              for (unsigned i = 0, e = PN->getNumIncomingValues(); i < e; ++i) {
                BasicBlock *inBB  = PN->getIncomingBlock(i);
                if (auto *term = dyn_cast<BranchInst>(inBB->getTerminator())) {
                  // if it's an *un*conditional branch, hook the branch node straight in
                  if (!term->isConditional()) {
                    if (auto *brNode = customGraph.findNodeForValue(term))
                      customGraph.addEdge(brNode, mergeNode);
                  }
                }
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
                  if (auto *GEP = dyn_cast<GetElementPtrInst>(inVal)) {
                    // wire the base pointer
                    customGraph.addEdge(
                      customGraph.getOrAdd(GEP->getPointerOperand()),
                      mergeNode
                    );
                    // wire each index operand
                    for (unsigned k = 1, ke = GEP->getNumOperands(); k < ke; ++k)
                      customGraph.addEdge(
                      customGraph.getOrAdd(GEP->getOperand(k)),
                      mergeNode);
                  } else {
                  // normal case: non‐GEP incoming value
                  DataflowNode *valueN = customGraph.getOrAdd(inVal);
                  customGraph.addEdge(valueN, mergeNode);
                  }
              }
              // Wire outputs
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
            // If the user is an instruction, add an edge to its corresponding node
            if (Instruction *userInst = dyn_cast<Instruction>(user)) {
                DataflowNode* userNode = customGraph.findNodeForValue(userInst);
                if (userNode) {
                    // Add a data dependency edge from the argument node to the instruction node
                    customGraph.addEdge(argNode, userNode);
                }
            }
            // TODO: Handle cases where an argument is used by another Argument or Constant (less common)
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
