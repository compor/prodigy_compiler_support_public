//
//
//

#ifndef PREFETCHER_HPP_
#define PREFETCHER_HPP_

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/DependenceAnalysis.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

#include "llvm/ADT/SmallVector.h"
// using llvm::SmallVector

#include "util.hpp"

#include <vector>
#include <string>

namespace llvm {
class Value;
class Instruction;
}; // namespace llvm

struct myAllocCallInfo {
  llvm::Instruction *allocInst;
  llvm::SmallVector<llvm::Value *, 3> inputArguments;
};

struct GEPDepInfo {
	llvm::Instruction * source;
	llvm::Instruction * target;
};

struct PrefetcherAnalysisResult {
  llvm::SmallVector<myAllocCallInfo, 8> allocs;
  llvm::SmallVector<GEPDepInfo, 8> geps;
  // TODO: Kuba add results from edge analysis
};

using namespace llvm;

// ***** helper function to print vectors ****** //
// This version of the function takes a vector of T* as input
template <typename T> void printVector(std::string inStr, T begin, T end) {
  errs() << inStr << ": < ";
  for (auto it = begin; it != end; ++it) {
    errs() << **it << " ";
  }
  errs() << ">\n";
}

struct PrefetcherPass : public FunctionPass {
  static char ID;

  using ResultT = PrefetcherAnalysisResult;

  ResultT Result;

  PrefetcherPass() : FunctionPass(ID) {}

  const ResultT &getPFA() const { return Result; }

  ResultT &getPFA() { return Result; }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    // AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addRequired<DependenceAnalysisWrapperPass>();
  }

  /* Identify Standard malloc */

  bool identifyMemoryAllocations(Function &F) {
    bool malloc_present = false;

    for (llvm::BasicBlock &BB : F) {
      for (llvm::Instruction &I : BB) {
        CallSite CS(&I);
        if (!CS.getInstruction()) {
          continue;
        }
        Value *called = CS.getCalledValue()->stripPointerCasts();

        if (llvm::Function *f = dyn_cast<Function>(called)) {
          if (f->getName().equals("calloc")) {
            malloc_present = true;
          } else if (f->getName().equals("malloc")) {
            malloc_present = true;
          }
        }
        // Check for other allocation functions - C++ new, etc.
      }
    }

    return false;
  }

  bool runOnFunction(llvm::Function &F) override;
};

#endif // PREFETCHER_HPP_
