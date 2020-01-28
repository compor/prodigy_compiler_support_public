#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/Analysis/DependenceAnalysis.h"

#include <string.h>

using namespace llvm;

struct RangedIndirectionBlock
{
  Function *function;
  Instruction *invokeInstr;
  Instruction *loadInvokeInstr;
  Value *loadInvokeIdx;
  Instruction *baseAddrLoad;
  Instruction *addrValueLoad;
};

struct BaseAddressBlock
{
  Function *function;
  Instruction *baseAddrLoad;
};

struct InstructionSet
{
  Instruction *currentInstr;
  Instruction *baseAddrLoad;
  Instruction *addrValueLoad;
};

bool getUserFromInstrType(Instruction &I, std::vector<Instruction *> &uses, const char *InstrType) {
	bool ret = false;

	for (auto &u: I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (!strcmp(user->getOpcodeName(), InstrType)) {
			ret = true;
			uses.push_back(user);
		}

		ret |= getUserFromInstrType(*user,uses, InstrType);
	}

	return ret;
}

namespace {
struct RangedIndirection : public ModulePass {
  static char ID;
  RangedIndirection() : ModulePass(ID) {}

  bool runOnModule(Module &M) override {

    /*
      * Detect A[i] and connect to use
    */
    std::vector<BaseAddressBlock> baseAddrBlkAi;
    Instruction *load0 = nullptr, *gep0 = nullptr, *load1 = nullptr, *gep1 = nullptr,
                *load2 = nullptr, *sext = nullptr, *gep3 = nullptr, *load3 = nullptr;

    // Find A[i]
    for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
      for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
        for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
        {
          load0 = &*II;
          if(!strcmp(load0->getOpcodeName(), "load")) 
          {
            gep0 = load0->getNextNonDebugInstruction();
            if (!strcmp(gep0->getOpcodeName(), "getelementptr"))
            {
              load1 = gep0->getNextNonDebugInstruction();
              if(!strcmp(load1->getOpcodeName(), "load")) 
              {
                gep1 = load1->getNextNonDebugInstruction();
                if (!strcmp(gep1->getOpcodeName(), "getelementptr"))
                {
                  load2 = gep1->getNextNonDebugInstruction();
                  if (!strcmp(load2->getOpcodeName(), "load"))
                  {
                    sext = load2->getNextNonDebugInstruction();
                    if (!strcmp(sext->getOpcodeName(), "sext"))
                    {
                      gep3 = sext->getNextNonDebugInstruction();
                      if (!strcmp(gep3->getOpcodeName(), "getelementptr"))
                      {
                        load3 = gep3->getNextNonDebugInstruction();
                        if (!strcmp(load3->getOpcodeName(), "load"))
                        {
                          std::vector<Instruction *> uses;
                          if(getUserFromInstrType(*load0, uses, "ret"))
                          {
                            BaseAddressBlock baseAddrBlk = {load1->getFunction(), load1};
                            baseAddrBlkAi.push_back(baseAddrBlk);
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }

    // Find Call Sites for the function which calculates A[i]
    std::vector<std::pair<Instruction *, Instruction *>> ranIndBlkCallSitesAi;
    for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
      for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
        for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
          if (!strcmp(II->getOpcodeName(), "invoke"))
          {
            for (auto i : baseAddrBlkAi)
            {
              CallSite CS(dyn_cast<InvokeInst>(II));
              if (!CS.getInstruction()) { continue; }
              Value *called = CS.getCalledValue()->stripPointerCasts();
              if (Function *f = dyn_cast<Function>(called))
                if (f->getName().equals((i.function)->getName()))
                  ranIndBlkCallSitesAi.push_back(std::make_pair(&*II, i.baseAddrLoad));
            }
          }
          else if (!strcmp(II->getOpcodeName(), "call"))
          {
            for (auto i : baseAddrBlkAi)
            {
              CallSite CS(dyn_cast<CallInst>(II));
              if (!CS.getInstruction()) { continue; }
              Value *called = CS.getCalledValue()->stripPointerCasts();
              if (Function *f = dyn_cast<Function>(called))
                if (f->getName().equals((i.function)->getName()))
                  ranIndBlkCallSitesAi.push_back(std::make_pair(&*II, i.baseAddrLoad));
            }      
          }

    // Find where the value for A[i] is loaded to be used
    std::vector<RangedIndirectionBlock> ranIndBlkAi; 
    for (auto i : ranIndBlkCallSitesAi)
    {
      std::vector<Instruction *> uses;
      if(getUserFromInstrType(*(i.first), uses, "store"))
      {
        Function *callSiteFunction = (i.first)->getFunction();
        std::vector<InstructionSet> localLoads;

        Value *storeVar = (uses.back())->getOperand(1);

        for (Function::iterator BB = callSiteFunction->begin(), BB_E = callSiteFunction->end();
            BB != BB_E; ++BB)
          for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
            if (!strcmp(II->getOpcodeName(), "load") && II->getOperand(0) == storeVar)
            {
              if(!strcmp(II->getNextNonDebugInstruction()->getOpcodeName(), "load") &&
                  II->getName() == II->getNextNonDebugInstruction()->getOperand(0)->getName())
              localLoads.push_back(InstructionSet{&*II, i.second, II->getNextNonDebugInstruction()});
            }

        for (auto j : localLoads)
        {
          std::vector<Instruction *> localStores;
          if(getUserFromInstrType(*(j.currentInstr), localStores, "store"))
          {
            RangedIndirectionBlock ranIndBlk = {callSiteFunction, i.first, 
              (i.first)->getPrevNonDebugInstruction(), 
              (i.first)->getPrevNonDebugInstruction()->getOperand(0),
              j.baseAddrLoad, j.addrValueLoad};
            ranIndBlkAi.push_back(ranIndBlk);
          }
        }
      }
    }

    /*
      * Detect A[i+1]
    */
    std::vector<BaseAddressBlock> baseAddrBlkAi1;
    load0 = nullptr; gep0 = nullptr; load1 = nullptr; gep1 = nullptr;
    load2 = nullptr; sext = nullptr; gep3 = nullptr; load3 = nullptr;
    Instruction *add = nullptr;

    for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
      for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
        for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
        {
          load0 = &*II;
          if(!strcmp(load0->getOpcodeName(), "load")) 
          {
            gep0 = load0->getNextNonDebugInstruction();
            if (!strcmp(gep0->getOpcodeName(), "getelementptr"))
            {
              load1 = gep0->getNextNonDebugInstruction();
              if(!strcmp(load1->getOpcodeName(), "load")) 
              {
                gep1 = load1->getNextNonDebugInstruction();
                if (!strcmp(gep1->getOpcodeName(), "getelementptr"))
                {
                  load2 = gep1->getNextNonDebugInstruction();
                  if (!strcmp(load2->getOpcodeName(), "load"))
                  {
                    add = load2->getNextNonDebugInstruction();
                    if(!strcmp(add->getOpcodeName(), "add"))
                    {
                      sext = add->getNextNonDebugInstruction();
                      if (!strcmp(sext->getOpcodeName(), "sext"))
                      {
                        gep3 = sext->getNextNonDebugInstruction();
                        if (!strcmp(gep3->getOpcodeName(), "getelementptr"))
                        {
                          load3 = gep3->getNextNonDebugInstruction();
                          if (!strcmp(load3->getOpcodeName(), "load"))
                          {
                            std::vector<Instruction *> uses;
                            if(getUserFromInstrType(*load0, uses, "ret"))
                            {
                              BaseAddressBlock baseAddrBlk = {load1->getFunction(), load1};
                              baseAddrBlkAi1.push_back(baseAddrBlk);
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }

    // Find Call Sites for the function which calculates A[i+1]
    std::vector<std::pair<Instruction *, Instruction *>> ranIndBlkCallSitesAi1;
    for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
      for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
        for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
          if (!strcmp(II->getOpcodeName(), "invoke"))
          {
            for (auto i : baseAddrBlkAi1)
            {
              CallSite CS(dyn_cast<InvokeInst>(II));
              if (!CS.getInstruction()) { continue; }
              Value *called = CS.getCalledValue()->stripPointerCasts();
              if (Function *f = dyn_cast<Function>(called))
                if (f->getName().equals((i.function)->getName()))
                  ranIndBlkCallSitesAi1.push_back(std::make_pair(&*II, i.baseAddrLoad));
            }                  
          }
          else if (!strcmp(II->getOpcodeName(), "call"))
          {
            for (auto i : baseAddrBlkAi1)
            {
              CallSite CS(dyn_cast<CallInst>(II));
              if (!CS.getInstruction()) { continue; }
              Value *called = CS.getCalledValue()->stripPointerCasts();
              if (Function *f = dyn_cast<Function>(called))
                if (f->getName().equals((i.function)->getName()))
                  ranIndBlkCallSitesAi1.push_back(std::make_pair(&*II, i.baseAddrLoad));
            }
          }

    // Find where the value for A[i+1] is loaded to be used
    std::vector<RangedIndirectionBlock> ranIndBlkAi1; 
    for (auto i : ranIndBlkCallSitesAi1)
    {
      std::vector<Instruction *> uses;
      if(getUserFromInstrType(*(i.first), uses, "store"))
      {
        Function *callSiteFunction = (i.first)->getFunction();

        Value *storeVar = (uses.back())->getOperand(1);

        for (Function::iterator BB = callSiteFunction->begin(), BB_E = callSiteFunction->end();
            BB != BB_E; ++BB)
          for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
            if (!strcmp(II->getOpcodeName(), "load") && II->getOperand(0) == storeVar)
              if(!strcmp(II->getNextNonDebugInstruction()->getOpcodeName(), "icmp") &&
                  II->getName() == II->getNextNonDebugInstruction()->getOperand(1)->getName())
              {
                RangedIndirectionBlock ranIndBlk = {callSiteFunction, i.first, 
                  (i.first)->getPrevNonDebugInstruction(), (i.first)->getPrevNonDebugInstruction()->getOperand(0), 
                  (i.second), &*II};
                ranIndBlkAi1.push_back(ranIndBlk);
              }
      }
    }

    /*
      * Connect A[i] and A[i+1] using the common object used for both function calls
    */
    std::vector<std::pair<RangedIndirectionBlock, RangedIndirectionBlock>> rangedIndirectionExport;
    for (auto i : ranIndBlkAi1)
      for (auto j : ranIndBlkAi)
        if ((j.function)->getName() == (i.function)->getName())
          if (j.loadInvokeIdx == i.loadInvokeIdx)
            rangedIndirectionExport.push_back(std::make_pair(j, i));

    /*
      * Print RangedIndirection blocks
    */
    for (auto i : rangedIndirectionExport)
      errs() << "Function: " << (i.first.function)->getName() << "\n"
             << "\tMatched:\n\t" << *(i.first.invokeInstr) << "\n"
             << "\t\twhich has load: " << *(i.first.baseAddrLoad) << "\n"
             << "\twith\n"
             << "\t" << *(i.second.invokeInstr) << "\n"
             << "\t\twhich has load: " << *(i.second.addrValueLoad) << "\n\n";

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
