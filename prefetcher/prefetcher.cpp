// LLVM
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemorySSA.h"

#include "llvm/ADT/SmallVector.h"

// standard
#include <vector>

// project
#include "prefetcher.hpp"
#include "util.hpp"

namespace {

llvm::PointerType *getCustomMallocType(const llvm::CallInst *CI) {
  llvm::PointerType *MallocType = nullptr;
  unsigned NumOfBitCastUses = 0;

  // Determine if CallInst has a bitcast use.
  for (llvm::Value::const_user_iterator UI = CI->user_begin(),
                                        E = CI->user_end();
       UI != E;) {
    if (const llvm::BitCastInst *BCI =
            llvm::dyn_cast<llvm::BitCastInst>(*UI++)) {
      MallocType = llvm::cast<llvm::PointerType>(BCI->getDestTy());
      NumOfBitCastUses++;
    }
  }

  // Malloc call has 1 bitcast use, so type is the bitcast's destination type.
  if (NumOfBitCastUses == 1)
    return MallocType;

  // Malloc call was not bitcast, so type is the malloc function's return type.
  if (NumOfBitCastUses == 0)
    return llvm::cast<llvm::PointerType>(CI->getType());

  // Type could not be determined.
  return nullptr;
}

llvm::Type *getCustomMallocAllocatedType(const llvm::CallInst *CI) {
  llvm::PointerType *PT = getCustomMallocType(CI);
  return PT ? PT->getElementType() : nullptr;
}

bool isCustomAllocLikeFn(llvm::Instruction *I) {
  if (auto *CI = llvm::dyn_cast<llvm::CallInst>(I)) {
    llvm::Value *called = CI->getCalledValue()->stripPointerCasts();

    if (llvm::Function *func = llvm::dyn_cast<llvm::Function>(called)) {
      if (func->getName().equals("myIntMallocFn32")) {
        return true;
      }
    }
  }

  return false;
}

bool identifyAlloc(llvm::SmallVectorImpl<myAllocCallInfo> &allocInfos,
                   llvm::Function &F,
                   const llvm::TargetLibraryInfo *TLI = nullptr) {
  // TODO handle failures more gracefully, e.g. when TLI is not provided
  bool found = false;
  auto &DL = F.getParent()->getDataLayout();

  for (llvm::BasicBlock &BB : F) {
    for (llvm::Instruction &I : BB) {
      llvm::CallSite CS(&I);
      if (!CS.getInstruction()) {
        continue;
      }

      llvm::Type *allocType = nullptr;
      if (isCustomAllocLikeFn(CS.getInstruction())) {
        allocType = getCustomMallocAllocatedType(
            llvm::dyn_cast<llvm::CallInst>(CS.getInstruction()));
      } else if (TLI && llvm::isAllocLikeFn(CS.getInstruction(), TLI)) {
        allocType = llvm::getMallocAllocatedType(
            llvm::dyn_cast<llvm::CallInst>(CS.getInstruction()), TLI);
      } else {
        continue;
      }

      auto elemSize = DL.getTypeAllocSize(allocType);

      auto *arg = llvm::dyn_cast<llvm::ConstantInt>(CS.getArgument(0));
      if (!arg) {
        DEBUG_WITH_TYPE(
            DEBUG_TYPE,
            llvm::dbgs() << "allocation size is not a compile-time constant!");
        continue;
      }

      found = true;
      auto allocSize = arg->getValue().getSExtValue();
      auto numElements = allocSize / elemSize;
      DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs()
                                      << "alloc elements: " << numElements
                                      << "\nalloc element size: " << elemSize
                                      << '\n');

      myAllocCallInfo allocInfo;
      allocInfo.allocInst = &I;
      auto *intType = llvm::IntegerType::get(F.getParent()->getContext(), 64);
      allocInfo.inputArguments.push_back(
          llvm::ConstantInt::get(intType, numElements));
      allocInfo.inputArguments.push_back(
          llvm::ConstantInt::get(intType, elemSize));

      allocInfos.push_back(allocInfo);
    }
  }

  return found;
}

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

void identifyGEPDependence(Function &F,
                           llvm::SmallVectorImpl<GEPDepInfo> &gepInfos) {

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
              GEPDepInfo g;
              g.source = I;
              g.target = U;

              errs() << "source: " << *(g.source) << "\n";
              errs() << "target: " << *(g.target) << "\n";
              gepInfos.push_back(g);
            }
          }
        }
      }
    }
  }
}

} // namespace

bool PrefetcherPass::runOnFunction(llvm::Function &F) {
  Result.allocs.clear();
  auto &TLI = getAnalysis<llvm::TargetLibraryInfoWrapperPass>().getTLI();

  identifyAlloc(Result.allocs, F, &TLI);
  identifyGEPDependence(F, Result.geps);

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

static llvm::RegisterPass<PrefetcherPass> X("prefetcher", "Prefetcher Pass",
                                            false, false);
