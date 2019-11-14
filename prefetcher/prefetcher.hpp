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

#include "util.hpp"

#include <vector>

namespace llvm {
class Value;
class Instruction;
}; // namespace llvm

struct myAllocCallInfo {
  llvm::Instruction *allocInst;
  std::vector<llvm::Value *> inputArguments;
};

//myAllocCallInfo identifyAlloc(llvm::Function &F);

using namespace llvm;

struct PrefetcherPass : public FunctionPass {
  static char ID;
  PrefetcherPass() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    // AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
    AU.addRequired<DependenceAnalysisWrapperPass>();
  }

  // ***** helper function to print vectors ****** //
  // This version of the function takes a vector of T* as input
  template <typename T>
  void printVector(std::string inStr, std::vector<T *> inVector) {
    errs() << inStr << ": < ";
    for (auto it = inVector.begin(); it != inVector.end(); ++it) {
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
                printVector("\n  is used by:\n", uses);
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

  /* End Identify Standard malloc */

  /* Identify Custom malloc */

  // struct myAllocCallInfo {
  // std::vector<llvm::Instruction*> allocInst;
  // std::vector<llvm::Value*> inputArgument;
  //};

  //myAllocCallInfo identifyAlloc(Function &F) {
    //myAllocCallInfo allocInfo;
    //for (llvm::BasicBlock &BB : F) {
      //for (llvm::Instruction &I : BB) {
        //CallSite CS(&I);
        //if (!CS.getInstruction()) {
          //continue;
        //}
        //Value *called = CS.getCalledValue()->stripPointerCasts();

        //if (llvm::Function *f = dyn_cast<Function>(called)) {
          //if (f->getName().equals("myIntMallocFn32")) {
            //// errs() << "Alloc: " << I << "\n";
            //// errs() << "Argument0:" << *(CS.getArgOperand(0)) << "\n";
            //allocInfo.allocInst.push_back(&I);
            //allocInfo.inputArgument.push_back(CS.getArgOperand(0));
          //}
        //}
      //}
    //}
    //return allocInfo;
  //}

  /* End identify Custom malloc */

  /* Test */

  // Example function for inserting llvm instructions
  bool addOne(Function &F) {
    for (llvm::BasicBlock &BB : F) {

      /* This might do something on code that has the prefetcher headers
       * included. It won't work for code that doesn't.
       * We need to somehow compile the headers for sniper (pf_interface.h) and
       * link them in.
       */
      llvm::IRBuilder<> Builder(&BB);
      CallInst *callTwo =
          Builder.CreateCall(F.getParent()->getFunction("RegisterNode"));

      /* Insert a lot of instructions */
      for (llvm::Instruction &I : BB) {
        llvm::IRBuilder<> Builder(&I);

        for (int i = 0; i < 100; ++i) {
          AllocaInst *callOne =
              Builder.CreateAlloca(Type::getInt32Ty(F.getContext()));
          // Figure out how to add pf calls into the context
        }
      }
    }

    return true;
  }

  bool runOnFunction(Function &F) override {

    //myAllocCallInfo allocInfo;
    DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>().getDI();

    //allocInfo = identifyAlloc(F);
    //if (!allocInfo.allocInst.empty()) {
      //errs() << "\n --- \n" << F.getName() << "\n --- \n";
      //printVector("startPointers", allocInfo.allocInst);
      //printVector("sizeOfArray", allocInfo.inputArgument);
      //errs() << "\n";
    //}

    identifyGEPDependence(F, DI);

    //		addOne(F);
    return false;
  }
};

#endif // PREFETCHER_HPP_
