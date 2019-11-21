//
//
//

#ifndef PREFETCHER_HPP_
#define PREFETCHER_HPP_

// LLVM
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instruction.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

#include "llvm/ADT/SmallVector.h"
// using llvm::SmallVector

#include "llvm/Support/Debug.h"
// using DEBUG macro
// using llvm::dbgs

// standard
#include <vector>
// using std::vector

#include <string>
// using std::string

// project
#include "util.hpp"

#define DEBUG_TYPE "prefetcher-analysis"

namespace llvm {
class Value;
class Instruction;
}; // namespace llvm

struct myAllocCallInfo {
  llvm::Instruction *allocInst;
  llvm::SmallVector<llvm::Value *, 3> inputArguments;
};

struct GEPDepInfo {
  llvm::Instruction *source;
  llvm::Instruction *target;
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
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addRequired<DependenceAnalysisWrapperPass>();
  }

  bool runOnFunction(llvm::Function &F) override;
};

#endif // PREFETCHER_HPP_
