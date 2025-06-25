#ifndef CUSTOM_DATAFLOW_GRAPH_H
#define CUSTOM_DATAFLOW_GRAPH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"

#include <map>
#include <set>
#include <string>
#include <list>
#include <vector>
#include <memory> // For unique_ptr
#include <algorithm> // For std::find
#include <utility>   // For std::pair

// Forward declarations for LLVM types used in the header
namespace llvm {
    class Value;
    class Instruction;
    class BranchInst;
    class SelectInst;
    class GetElementPtrInst;
    class CastInst;
    class Constant;
    class Argument;
    class BasicBlock;
    class ICmpInst;
    class FCmpInst;
    class PHINode;
    class User;
    class raw_fd_ostream;
}

// Define the types of custom dataflow operators
enum class DataflowOperatorType {
    Unknown,
    FunctionInput,  // Represents a function argument
    FunctionOutput, // Represents a return value
    Constant,       // Represents a constant value
    BasicBinaryOp,  // For arithmetic, bitwise, etc.
    Load,
    Store,
    TrueSteer,      // TIF, representing conditional data steering
    FalseSteer,     // F, representing conditional data steering
    Merge,          // M, The merge operator
    Carry,          // Carry for loop-carried dependencies
    Invariant,      // I, for loop invariants
    Order,          // O, to enforce memory ordering
    Stream          // STR, for stream processing/loops
};

// Forward declaration of DataflowNode for DataflowEdge
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
    const llvm::Value* OriginalValue; // Reference to the original LLVM Value (Instruction, Argument, Constant)
    std::vector<DataflowEdge*> Inputs;
    std::vector<DataflowEdge*> Outputs;
    std::string Label;      // Label for the DOT graph
    std::string OpSymbol;   // Holds “+”, “<=”, etc.

    // Constructor
    DataflowNode(DataflowOperatorType type, const llvm::Value* originalValue = nullptr, const std::string& label = "");

    // Destructor
    ~DataflowNode() = default;
};

// Represents the entire custom dataflow graph
struct CustomDataflowGraph {
    std::list<std::unique_ptr<DataflowNode>> Nodes;
    std::vector<std::unique_ptr<DataflowEdge>> Edges;
    // Map original LLVM Values to their corresponding custom graph nodes
    std::map<const llvm::Value*, DataflowNode*> ValueToNodeMap;

    CustomDataflowGraph() = default;
    ~CustomDataflowGraph() = default;

    /// Recursively wire V into destN, but if V is either a GEP
    /// or any instruction whose node type is still Unknown,
    /// don’t create a node for V—just forward its operands.
    void wireValueToNode(llvm::Value *V, DataflowNode *destN);

    // Modified getOrAdd to set basic types for Arguments and Constants
    DataflowNode* getOrAdd(llvm::Value *V);

    // Add a node to the graph
    DataflowNode* addNode(DataflowOperatorType type, const llvm::Value* originalValue = nullptr, const std::string& label = "");

    // Add an edge to the graph
    void addEdge(DataflowNode* source, DataflowNode* destination);

    // Helper to find a node representing an original LLVM Value
    DataflowNode* findNodeForValue(const llvm::Value* val) const;

    // New helper: hook all loads after a store into that store
    // (Note: This is a simplistic model, a proper one would require alias analysis.)
    void addMemDepEdges();
};

// Function to print the custom graph to a DOT file
void printCustomDFGToFile(const CustomDataflowGraph &customGraph, const std::string &filename);

#endif // CUSTOM_DATAFLOW_GRAPH_H
