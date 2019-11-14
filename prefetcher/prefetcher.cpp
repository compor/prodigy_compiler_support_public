// LLVM Headers
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

// Project headers
#include "util.hpp"
#include "prefetcher.hpp"

// Standard libs
#include <vector>

using namespace llvm;

/* Identify Custom malloc */

// struct myAllocCallInfo {
// std::vector<llvm::Instruction*> allocInst;
// std::vector<llvm::Value*> inputArgument;
//};

void identifyAlloc(Function &F,
                   llvm::SmallVectorImpl<myAllocCallInfo> &allocInfos) {
  for (llvm::BasicBlock &BB : F) {
    for (llvm::Instruction &I : BB) {
      CallSite CS(&I);
      if (!CS.getInstruction()) {
        continue;
      }
      Value *called = CS.getCalledValue()->stripPointerCasts();

      if (llvm::Function *f = dyn_cast<Function>(called)) {
        if (f->getName().equals("myIntMallocFn32")) {
          // errs() << "Alloc: " << I << "\n";
          // errs() << "Argument0:" << *(CS.getArgOperand(0)) << "\n";
          myAllocCallInfo allocInfo;
          allocInfo.allocInst = &I;
          allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),
                                          CS.args().begin(), CS.args().end());
          allocInfos.push_back(allocInfo);
        }
      }
    }
  }
  return;
}

bool PrefetcherPass::runOnFunction(llvm::Function &F) {
  DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>().getDI();
  Result.allocs.clear();

  identifyAlloc(F, Result.allocs);
  identifyGEPDependence(F, DI);

  // addOne(F);

  return false;
}

/* End identify Custom malloc */

// namespace {

// struct Prefetcher_Module : public ModulePass {
// static char ID;
// Prefetcher_Module() : ModulePass(ID) {}

// bool runOnModule(Module &M) override { return false; }
//};

//} // namespace

// char Prefetcher_Module::ID = 0; // Initialization value not important

// static RegisterPass<Prefetcher_Module> Y("prefetcher_module",
//"Module Prefetcher Pass",
// false, [> Only looks at CFG <]
// true [> Analysis Pass <]);

char PrefetcherPass::ID = 0; // Initialization value not important

static RegisterPass<PrefetcherPass> X("prefetcher", "Prefetcher Pass", false,
                                      false);
