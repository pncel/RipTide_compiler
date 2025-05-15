#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h"        // <-- Add this
#include "llvm/IR/CFG.h"      // for successors()

#include <map>
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

      // First pass: assign names
      for (const BasicBlock &BB : F) {
        std::string label;
        raw_string_ostream bbStream(label);
        BB.printAsOperand(bbStream, false);
        bbNames[&BB] = bbStream.str();
      }

      // Second pass: emit nodes and edges
      for (const BasicBlock &BB : F) {
        outFile << "  \"" << bbNames[&BB] << "\" [label=\"";
        for (const Instruction &I : BB) {
          std::string instrStr;
          raw_string_ostream instrStream(instrStr);
          I.print(instrStream);
          outFile << instrStream.str() << "\\l";
        }
        outFile << "\"];\n";

        const Instruction *TI = BB.getTerminator();
        if (TI) {
          for (const BasicBlock *Succ : successors(&BB)) {
            outFile << "  \"" << bbNames[&BB]
                    << "\" -> \"" << bbNames[Succ] << "\";\n";
          }
        }
      }

      outFile << "}\n";  // <-- Missing in your version
      return PreservedAnalyses::all();  // <-- Also missing
    }
  }; 
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  errs() << ">>> cfg-pass plugin loaded!\n";
  return {
    LLVM_PLUGIN_API_VERSION, "ControlflowGraph", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name,
           FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement> Pipeline) {
          if (Name == "ControlflowGraph") {
            FPM.addPass(ControlflowGraph());
            return true;
          }
          return false;
        });
    }
  };
}
