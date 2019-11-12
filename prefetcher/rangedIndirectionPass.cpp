#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/Analysis/DependenceAnalysis.h"

#include <string.h>

using namespace llvm;

struct arrayIndexLoad
{
  Instruction *GEPInstr;
  Instruction *LdInstr;
  Value *LdIdx;
  Instruction *StrInstr;
  Value *StrIdx;
};

bool getUsers(Instruction &I, SmallVector<Instruction *, 5> &uses, const char *InstrType) {
	bool ret = false;

	for (auto &u: I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (!strcmp(user->getOpcodeName(), InstrType)) {
			ret = true;
			uses.push_back(user);
		}

		ret |= getUsers(*user,uses, InstrType);
	}

	return ret;
}

namespace {
struct RangedIndirection : public FunctionPass {
  static char ID;
  RangedIndirection() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {

    errs() << "Identifying Instruction pair for ranged indirection on " << F.getName() << "\n";

    SmallVector<arrayIndexLoad, 5> arrIdxLdVector;

    for (Function::iterator BB = F.begin(), BB_E = F.end(); BB != BB_E; ++BB)
      for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
      {
        if(!strcmp(II->getOpcodeName(), "load")) 
        {
          Instruction *sext = II->getNextNonDebugInstruction();
          Instruction *gep = sext->getNextNonDebugInstruction();
          Instruction *finalLoad = gep->getNextNonDebugInstruction();
          // Check if sext is used by getelementptr for a sanity check
          if(!strcmp(sext->getOpcodeName(), "sext") && 
              !strcmp(gep->getOpcodeName(), "getelementptr") &&
              !strcmp(finalLoad->getOpcodeName(), "load"))
          {
            SmallVector<Instruction *, 5> uses;
            if(getUsers(*II, uses, "store"))
            {
              Instruction *storeInstr = uses.back();
              arrayIndexLoad arrIdxLdObj = {&*gep, &*II, II->getOperand(0), storeInstr, storeInstr->getOperand(1)};
              arrIdxLdVector.push_back(arrIdxLdObj);
            }
          }
        }
      }

    std::vector<std::pair<arrayIndexLoad, arrayIndexLoad>> depGEPBlocks;
    std::vector<arrayIndexLoad> temp(arrIdxLdVector.begin(), arrIdxLdVector.end());
    std::vector<std::tuple<Instruction *, Instruction *, Instruction *>> rangedIndirectionGEP;

    for (auto i : arrIdxLdVector)
    {
      Value *storeVar = i.StrIdx;

      temp.erase(temp.begin());
      for (auto j : temp)
        if(j.LdIdx == storeVar)
          depGEPBlocks.push_back(std::make_pair(i, j));
    }

    for (auto i : depGEPBlocks)
    {
      BasicBlock *destination = (i.second).StrInstr->getParent();

      for (auto U : (i.first).LdIdx->users())
      {
        if (!strcmp((dyn_cast<Instruction>(U))->getOpcodeName(), "load"))
        {
          Instruction *load = dyn_cast<Instruction>(U);
          Instruction *add = load->getNextNonDebugInstruction();
          Instruction *sext = add->getNextNonDebugInstruction();
          Instruction *gep = sext->getNextNonDebugInstruction();

          if(!strcmp(add->getOpcodeName(), "add") && !strcmp(sext->getOpcodeName(), "sext"))
          {
            SmallVector<Instruction *, 5> uses;
            if(getUsers(*load, uses, "br"))
              for (auto j : dyn_cast<BranchInst>(uses.back())->successors())
                if(j == destination)
                  rangedIndirectionGEP.push_back(std::make_tuple((i.first).GEPInstr, gep, (i.second).GEPInstr));
          }
        }
      }
    }

    for (auto i : rangedIndirectionGEP)
      errs() << "Ranged Indirection GEPs:\n\t" << *(std::get<0>(i))
              << "\n\t" << *(std::get<1>(i))
              << "\n\t" << *(std::get<2>(i))
              << "\n\n";

    return false;
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DependenceAnalysisWrapperPass>();
  }
}; // end of struct Hello
}  // end of anonymous namespace

char RangedIndirection::ID = 0;
static RegisterPass<RangedIndirection> X("RangedIndirection", "Ranged Indirection for Project",
                                         false /* Only looks at CFG */,
                                         false /* Analysis Pass */);

static RegisterStandardPasses Y(
    PassManagerBuilder::EP_EarlyAsPossible,
    [](const PassManagerBuilder &Builder,
           legacy::PassManagerBase &PM) { PM.add(new RangedIndirection()); });
