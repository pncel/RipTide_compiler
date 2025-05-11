#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

namespace {
class ControlflowGraph : public PassInfoMixin<ControlflowGraph> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    std::error_code EC;
    raw_fd_ostream outFile("cfg.dot", EC, sys::fs::OF_Text);

    if (EC) {
      errs() << "Error opening output file: " << EC.message() << "\n";
      return PreservedAnalyses::all();
    }

    outFile << "digraph \"CFG\" {\n";
    outFile << "  node [shape=rectangle fontname=\"Courier\"];\n";

    std::map<const BasicBlock*, std::string> bbNames;
    int id = 0;

    for (const BasicBlock &BB : F) {
      std::string label;
      raw_string_ostream bbStream(label);
      BB.printAsOperand(bbStream, false);
      bbNames[&BB] = bbStream.str();

      // Emit block label
      outFile << "  \"" << bbNames[&BB] << "\" [label=\"";
      for (const Instruction &I : BB) {
        std::string instrStr;
        raw_string_ostream instrStream(instrStr);
        I.print(instrStream);
        outFile << instrStream.str() << "\\l";  // left-align with \l
      }
      outFile << "\"];\n";

      // Emit edges to successors
      if (const TerminatorInst *TI = BB.getTerminator()) {
        for (unsigned i = 0; i < TI->getNumSuccessors(); ++i) {
          const BasicBlock *Succ = TI->getSuccessor(i);
          outFile << "  \"" << bbNames[&BB] << "\" -> \"" << bbNames[Succ] << "\";\n";
        }
      }
    }

    outFile << "}\n";

    return PreservedAnalyses::all();
  }
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "ControlflowGraph", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "cfg-pass") {
            FPM.addPass(ControlflowGraph());
            return true;
          }
          return false;
        });
    }
  };
}
