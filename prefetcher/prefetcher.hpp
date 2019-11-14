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

struct PrefetcherAnalysisResult {
  llvm::SmallVector<myAllocCallInfo, 8> allocs;
  // TODO: Kuba add results from edge analysis
};

using namespace llvm;

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

  // ***** helper function to print vectors ****** //
  // This version of the function takes a vector of T* as input
  template <typename T> void printVector(std::string inStr, T begin, T end) {
    errs() << inStr << ": < ";
    for (auto it = begin; it != end; ++it) {
      errs() << **it << " ";
    }
    errs() << ">\n";
  }

  /* GEP DEPENDENCE */

  bool usedInLoad(llvm::Instruction *I) {
    for (auto &u : I->uses()) {
      auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

      if (user->getOpcode() == Instruction::Load) {
        return true;
      }
    }

    return false;
  }

  bool recurseUsesSilent(llvm::Instruction &I,
                         std::vector<llvm::Instruction *> &uses) {
    bool ret = false;

    for (auto &u : I.uses()) {
      auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

      if (user->getOpcode() == Instruction::GetElementPtr) {
        ret = true;
        uses.push_back(user);
      }

      ret |= recurseUsesSilent(*user, uses);
    }

    return ret;
  }

  std::vector<std::pair<llvm::Instruction *, llvm::Instruction *>>
  identifyGEPDependence(Function &F, DependenceInfo &DI) {

    std::vector<llvm::Instruction *> insns;
    std::vector<llvm::Instruction *> loads;

    for (llvm::BasicBlock &BB : F) {
      for (llvm::Instruction &I : BB) {
        // errs() << "I  :" << I << "\n";
        if (I.getOpcode() == Instruction::GetElementPtr) {
          insns.push_back(&I);
          // errs() << "ins:" << I << "\n";
        }

        if (I.getOpcode() == Instruction::Load) {
          loads.push_back(&I);
        }

        if (I.getOpcode() == Instruction::Load) {
          loads.push_back(&I);
        }
      }
    }

    std::vector<std::pair<llvm::Instruction *, llvm::Instruction *>>
        dependentGEPs;

    if (insns.size() > 0) {
      for (auto I : insns) {
        if (I->getOpcode() == llvm::Instruction::GetElementPtr) {
          // usedInLoad()        finds if a GEP instruction is used in load
          // recurseUsesSilent() finds if the GEP instruction is
          //                     *eventually* used in another GEP instruction
          // can detect loads of type A[B[i]]
          // and does not detect stores of type A[B[i]]

          std::vector<llvm::Instruction *> uses;

          if (usedInLoad(I) && recurseUsesSilent(*I, uses)) {
            for (auto U : uses) {
              if (usedInLoad(U)) {
                errs() << "\n" << demangle(F.getName().str().c_str()) << "\n";
                errs() << *I;
                printVector("\n  is used by:\n", uses.begin(), uses.end());
                errs() << "\n";
                dependentGEPs.push_back(std::make_pair(I, U));
              }
            }
          }
        }
      }
    }

    return dependentGEPs;
  }

  /* End GEP Dependence */

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
