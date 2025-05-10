#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"

#include <map>
#include <set>
#include <string>

using namespace llvm;

namespace {
class DataflowGraph : public PassInfoMixin<DataflowGraph> {
  std::map<const Instruction*, std::set<const Instruction*>> graph;

  void printDFGToFile(const std::string &filename) const {
    std::error_code EC;
    raw_fd_ostream outFile(filename, EC, sys::fs::OF_Text);
    if (EC) {
      errs() << "Error opening file " << filename << ": " << EC.message() << "\n";
      return;
    }
  
    outFile << "digraph \"dfg\" {\n";
    std::map<const Instruction*, std::string> instrNames;
    int id = 0;
  
    for (const auto &entry : graph) {
      const Instruction *I = entry.first;
      if (!instrNames.count(I))
        instrNames[I] = "%" + std::to_string(id++);
      outFile << "  \"" << instrNames[I] << "\" [label=\"";
      std::string instrStr;
      raw_string_ostream instrStream(instrStr);
      I->print(instrStream, false);
      outFile << instrStream.str() << "\"];\n";
  
      for (const Instruction *succ : entry.second) {
        if (!instrNames.count(succ))
          instrNames[succ] = "%" + std::to_string(id++);
        outFile << "  \"" << instrNames[succ] << "\" -> \"" << instrNames[I] << "\";\n";
      }
    }
  
    outFile << "}\n";
}

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    graph.clear();
    errs() << "Building DFG for function: " << F.getName() << "\n";

    for (auto &BB : F) {
      for (auto &I : BB) {
        for (unsigned i = 0; i < I.getNumOperands(); ++i) {
          Value *operand = I.getOperand(i);
          if (Instruction *dep = dyn_cast<Instruction>(operand)) {
            graph[dep].insert(&I);
          }
        }
      }
    }

    printDFGToFile("dfg.dot");
    return PreservedAnalyses::all();
  }
};
} // namespace

// Plugin registration for new LLVM pass manger
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "DataflowGraph", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "dfg-pass") {
            FPM.addPass(DataflowGraph());
            return true;
          }
          return false;
        });
    }
  };
}
