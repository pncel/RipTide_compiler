#include "CustomDataflowGraph.h" // Include its own header
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/FileSystem.h" // For sys::fs::OF_Text, raw_fd_ostream
#include <system_error> // For std::error_code
#include "llvm/IR/Function.h" // Added for llvm::Function

using namespace llvm;
using namespace llvm::sys;

// DataflowNode Constructor Implementation
DataflowNode::DataflowNode(DataflowOperatorType type, const llvm::Value* originalValue, const std::string& label)
    : Type(type), OriginalValue(originalValue), Label(label) {}

// CustomDataflowGraph method implementations
void CustomDataflowGraph::wireValueToNode(llvm::Value *V, DataflowNode *destN) {
    if (!V || !destN) return;

    // Unwrap GEP transparently
    if (isa<GetElementPtrInst>(V)) {
        auto *GEP = cast<GetElementPtrInst>(V);
        wireValueToNode(GEP->getPointerOperand(), destN);
        for (Use &IU : GEP->indices())
            wireValueToNode(IU.get(), destN);
        return;
    }

     // Handle *any* cast instruction (bitcast, trunc, fptrunc, fpext, sitofp, etc.) transparently
    if (auto *CI = dyn_cast<CastInst>(V)) {
        wireValueToNode(CI->getOperand(0), destN);
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

void CustomDataflowGraph::removeNode(DataflowNode* nodeToRemove) {
    if (!nodeToRemove) {
        errs() << "Warning: Null node provided to removeNode\n";
        return;
    }

    // Step 1: Remove all edges connected to the node
    // A temporary vector to store edges to be removed
    std::vector<DataflowEdge*> edgesToRemove;
    for (DataflowEdge* edge : nodeToRemove->Inputs) {
        edgesToRemove.push_back(edge);
    }
    for (DataflowEdge* edge : nodeToRemove->Outputs) {
        edgesToRemove.push_back(edge);
    }

    for (DataflowEdge* edge : edgesToRemove) {
        // Remove the edge from the graph's main Edges vector
        auto it = std::find_if(Edges.begin(), Edges.end(), 
            [edge](const std::unique_ptr<DataflowEdge>& edgePtr) {
                return edgePtr.get() == edge;
            });

        if (it != Edges.end()) {
            // Unlink from source and destination nodes' Input/Output lists
            if (edge->Source) {
                auto& sourceOutputs = edge->Source->Outputs;
                sourceOutputs.erase(std::remove(sourceOutputs.begin(), sourceOutputs.end(), edge), sourceOutputs.end());
            }
            if (edge->Destination) {
                auto& destInputs = edge->Destination->Inputs;
                destInputs.erase(std::remove(destInputs.begin(), destInputs.end(), edge), destInputs.end());
            }
            Edges.erase(it);
        }
    }

    // Step 2: Remove the node from the main Nodes list
    auto nodeIt = std::find_if(Nodes.begin(), Nodes.end(),
        [nodeToRemove](const std::unique_ptr<DataflowNode>& nodePtr) {
            return nodePtr.get() == nodeToRemove;
        });
    
    if (nodeIt != Nodes.end()) {
        Nodes.erase(nodeIt);
    }

    // Step 3: Remove the node's entry from the ValueToNodeMap
    // Find the key (OriginalValue) associated with the node
    const llvm::Value* originalValue = nullptr;
    for (const auto& pair : ValueToNodeMap) {
        if (pair.second == nodeToRemove) {
            originalValue = pair.first;
            break;
        }
    }
    if (originalValue) {
        ValueToNodeMap.erase(originalValue);
    }

    errs() << "Node removed successfully.\n";
}

DataflowNode* CustomDataflowGraph::getOrAdd(llvm::Value *V) {
    if (!V) return nullptr; // Handle null values gracefully

    // NEVER materialize a Function as its own node.
    if (isa<Function>(V)) { // Added check for Function
        return nullptr;
    }
    // Never materialize ANY branch instruction as its own node
    if (isa<BranchInst>(V) || isa<SelectInst>(V)) {
        return nullptr;
    }
    // Never materialize GEP or ANY cast instruction as its own node
    if (isa<GetElementPtrInst>(V) || isa<CastInst>(V)) {
        return nullptr;
    }

    if (auto *N = findNodeForValue(V)) return N;

    // Determine initial type based on Value type
    DataflowOperatorType type = DataflowOperatorType::Unknown;
    if (isa<Argument>(V)) type = DataflowOperatorType::FunctionInput;
    else if (isa<Constant>(V)) type = DataflowOperatorType::Constant;
    // For actual instructions, the type will be refined in the main pass.
    return addNode(type, V); // Initial label can be empty and generated later
}

DataflowNode* CustomDataflowGraph::addNode(DataflowOperatorType type, const llvm::Value* originalValue, const std::string& label) {
    Nodes.push_back(std::make_unique<DataflowNode>(type, originalValue, label));
    DataflowNode* newNode = Nodes.back().get();
    if (originalValue) {
        ValueToNodeMap[originalValue] = newNode;
    }
    return newNode;
}

void CustomDataflowGraph::addEdge(DataflowNode* source, DataflowNode* destination) {
    if (!source) {
        errs() << "Warning: Null source in addEdge\n";
        return;
    }
    if (!destination) {
        errs() << "Warning: Null destination in addEdge\n";
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

DataflowNode* CustomDataflowGraph::findNodeForValue(const llvm::Value* val) const {
    auto it = ValueToNodeMap.find(val);
    if (it != ValueToNodeMap.end()) {
        return it->second;
    }
    return nullptr;
}

void CustomDataflowGraph::addMemDepEdges() {
    // This is a very simplistic memory dependency model, derived from the original code.
    // A proper one would require alias analysis and careful consideration of memorySSA.
    // The original code commented out the actual connection, and for this refactor,
    // we maintain that behavior, indicating this is a placeholder or a very high-level
    // representation. If actual memory dependencies are to be enforced, this
    // section would need significant expansion.
    DataflowNode *lastStore = nullptr;
    for (auto &S : Nodes) {
       if (S->Type == DataflowOperatorType::Store)
         lastStore = S.get();
       // else if (S->Type == DataflowOperatorType::Load && lastStore)
       //  wireValueToNode(lastStore, S.get());
    }
}

// Global helper function for printing the graph
void printCustomDFGToFile(const CustomDataflowGraph &customGraph, const std::string &filename) {
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
        case DataflowOperatorType::FunctionInput: return "ellipse";
        case DataflowOperatorType::FunctionOutput: return "ellipse";
        case DataflowOperatorType::Constant: return "box";
        case DataflowOperatorType::BasicBinaryOp: return "box";
        case DataflowOperatorType::Load: return "ellipse";
        case DataflowOperatorType::Store: return "ellipse";
        case DataflowOperatorType::TrueSteer: return "triangle";
        case DataflowOperatorType::FalseSteer: return "invtriangle";
        case DataflowOperatorType::Merge: return "octagon";
        case DataflowOperatorType::Carry: return "box"; /* Carry.Carryrepresentsaloop-carrieddependencyandtakesa
 decider,D,andtwodatavalues,AandB.Carryhastheinternal
 statemachineshowninFig.3. IntheInitial state, itwaitsfor
 A, andthenpasses it throughandtransitions totheBlockstate.
 WhileinBlock, ifDisTrue, theoperatorpasses throughB. It
 transitionsbacktoInitialwhenDisFalse, andbeginswaiting
 for thenextAvalue(ifnotalreadybufferedat theinput).
 Carryoperatorskeeptokensorderedinloops,eliminatingthe
 needtotagtokens.Allbackedgesareroutedthroughacarry
 operator inRipTide.BynotconsumingAwhileinBlock,carry
 operatorspreventouter loopsfromspawninganewinner-loop
 instancebeforethepreviousonehasfinished. (Iterationsfrom
 oneinner-loopmaybepipelinedif theyareindependent,but
 entireinstancesof theinner loopwillbeserialized.)*/
        case DataflowOperatorType::Invariant: return "box";
        case DataflowOperatorType::Order: return "box";
        case DataflowOperatorType::Stream: return "circle";
        default: return "box";
      }
    };

    auto getNodeLabel = [](const DataflowNode* node) {
        if (!node->OpSymbol.empty())
            return node->OpSymbol;
        if (!node->Label.empty()) {
            return node->Label;
        }
        // Generate a default label based on operator type and original instruction
        std::string label;
        switch (node->Type) {
            case DataflowOperatorType::BasicBinaryOp: label = "BinOp"; break;
            case DataflowOperatorType::Load: label = "Load"; break;
            case DataflowOperatorType::Constant: label = "Constant"; break;
            case DataflowOperatorType::Store: label = "Store"; break;
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
            std::string ss_str;
            llvm::raw_string_ostream ss(ss_str);
            node->OriginalValue->print(ss);
            label += "\\n" + ss.str();
        }
        return label;
    };

    for (const auto& nodePtr : customGraph.Nodes) {
      const DataflowNode* node = nodePtr.get();

      // Dont create any node that has no outputs
      if (node->Outputs.empty()) {
          continue;
      }
      
      std::string nodeName = "node" + std::to_string(id++);
      nodeNames[node] = nodeName;

      outFile << "  \"" << nodeName << "\" [label=\"" << getNodeLabel(node)
              << "\", shape=\"" << getNodeShape(node->Type) << "\"];\n";
    }

    // Add edges
    for (const auto& edgePtr : customGraph.Edges) {
        const DataflowEdge* edge = edgePtr.get();
        // Ensure both source and destination nodes exist in the `nodeNames` map
        // (i.e., they weren't skipped by the empty node check)
        if (nodeNames.count(edge->Source) && nodeNames.count(edge->Destination)) {
            outFile << "  \"" << nodeNames.at(edge->Source) << "\" -> \""
                    << nodeNames.at(edge->Destination) << "\";\n";
        }
    }

    outFile << "}\n";
    outFile.close();
    errs() << "Custom DFG written to " << filename << "\n";
}