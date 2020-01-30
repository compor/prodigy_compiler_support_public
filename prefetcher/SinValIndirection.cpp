#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/CallSite.h"

#include <string.h>
#include <vector>
#include "prefetcher.hpp"

using namespace llvm;

typedef std::vector<Function *> FuncVector;
typedef std::vector<Instruction *> InstrVector;
typedef std::map<Function *, Instruction *> SinValIndBlk;

struct SinValIndExport
{
	Function *function;
	Instruction *loadDS1;
	Instruction *loadDS2;
};

bool getUserFromInstrType(Instruction &I, InstrVector &uses, const char *InstrType) {
	bool ret = false;

	for (auto &u: I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());
		if(user)
		{
			if (!strcmp(user->getOpcodeName(), InstrType))
			{
				ret = true;
				uses.push_back(user);
			}
			ret |= getUserFromInstrType(*user, uses, InstrType);
		}
	}

	return ret;
}

bool getIndexAccessFromFunc(Function &F, FuncVector &arrayIndexFunctions, CallGraph &CG, FuncVector &arrayIndexParents)
{
	bool ret = false;

	CallGraphNode *node = CG.operator[](&F);

	if (node->empty())
	{
		if (std::find(arrayIndexFunctions.begin(), arrayIndexFunctions.end(), &F) != arrayIndexFunctions.end())
		{
			arrayIndexParents.push_back(&F);
			ret = true;
		}
	}
	else
	{
		for (auto IT = node->begin(), EI = node->end(); IT != EI; ++IT)
			if (&F.getFunction())
			{
				if (std::find(arrayIndexFunctions.begin(), arrayIndexFunctions.end(), IT->second->getFunction()) != arrayIndexFunctions.end())
				{
					arrayIndexParents.push_back(IT->second->getFunction());
					ret = true;
				}
				else
					ret |= getIndexAccessFromFunc(*(IT->second->getFunction()), arrayIndexFunctions, CG, arrayIndexParents);
			}
	}
	return ret;
}


bool SinValIndirectionPass::runOnModule(Module &M) {
	CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();

	/*
	 * Find functions which evaluate A[i]
	 */
	FuncVector arrayIndexFunctions;
	SinValIndBlk arrayIndexCalcBlocks;
	Instruction *load0 = nullptr, *gep0 = nullptr, *load1 = nullptr, *gep1 = nullptr, *gep2 = nullptr;
	bool detected = false;

	for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
	{
		detected = false;
		for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
		{
			for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
			{
				gep0 = &*II;
				if (!strcmp(gep0->getOpcodeName(), "getelementptr"))
				{
					load0 = gep0->getNextNonDebugInstruction();
					if (!strcmp(load0->getOpcodeName(), "load")
							&& load0->getOperand(0) == dyn_cast<Value>(gep0))
					{
						gep1 = load0->getNextNonDebugInstruction();
						// Pattern for loading A[i] through pointer dereferencing
						if(!strcmp(gep1->getOpcodeName(), "getelementptr"))
						{
							load1 = gep1->getNextNonDebugInstruction();
							if (!strcmp(load1->getOpcodeName(), "load")
									&& load1->getOperand(0) == dyn_cast<Value>(gep1))
							{
								InstrVector gepLoadAi1, gepLoadAi2;
								if (getUserFromInstrType(*load0, gepLoadAi1, "getelementptr")
										&& getUserFromInstrType(*load1, gepLoadAi2, "getelementptr"))
								{
									if (gepLoadAi1.back() == gepLoadAi2.back())
									{
										gep2 = gepLoadAi1.back();
										InstrVector loadRet;
										if (getUserFromInstrType(*gep2, loadRet, "ret"))
										{
											arrayIndexFunctions.push_back(gep2->getFunction());
											arrayIndexCalcBlocks.insert({gep2->getFunction(), load0});
											detected = true;
											break;
										}
									}
								}
							}
						}
						// Pattern for loading A[i] through array index
						else if (!strcmp(gep1->getOpcodeName(), "load"))
						{
							InstrVector gepLoadAi1, gepLoadAi2;
							if (getUserFromInstrType(*load0, gepLoadAi1, "getelementptr")
									&& getUserFromInstrType(*gep1, gepLoadAi2, "getelementptr"))
							{
								if (gepLoadAi1.back() == gepLoadAi2.back())
								{
									gep2 = gepLoadAi1.back();
									InstrVector loadRet;
									if (getUserFromInstrType(*gep2, loadRet, "ret"))
									{
										arrayIndexFunctions.push_back(gep2->getFunction());
										arrayIndexCalcBlocks.insert({gep2->getFunction(), gep0->getPrevNonDebugInstruction()});
										detected = true;
										break;
									}
								}
							}
						}
					}
				}
			}
			if (detected)
				break;
		}
	}

	// Find Call Sites for the function which calculates A[i]
	// arrayIndexCallSites stores invokes/calls for A[i] functions
	// arrayIndexCallBlocks stores functions called by these call sites and
	//  the respective data structure loads
	InstrVector arrayIndexCallSites;
	SinValIndBlk arrayIndexCallBlocks;
	for (Module::iterator FF = M.begin(), FF_E = M.end(); FF != FF_E; ++FF)
		for (Function::iterator BB = FF->begin(), BB_E = FF->end(); BB != BB_E; ++BB)
			for (BasicBlock::iterator II = BB->begin(), II_E = BB->end(); II != II_E; ++II)
				if (!strcmp(II->getOpcodeName(), "invoke"))
				{
					CallSite CS(dyn_cast<InvokeInst>(II));
					Value *called = CS.getCalledValue()->stripPointerCasts();
					FuncVector arrayIndexParents;
					if (dyn_cast<Function>(called))
						if (getIndexAccessFromFunc(*(dyn_cast<Function>(called)), arrayIndexFunctions, CG, arrayIndexParents))
						{
							auto it = arrayIndexCalcBlocks.find(arrayIndexParents.back());
							if (it != arrayIndexCalcBlocks.end())
							{
								arrayIndexCallBlocks.insert({dyn_cast<Function>(called), it->second});
								arrayIndexCallSites.push_back(&*II);
							}
						}
				}
				else if (!strcmp(II->getOpcodeName(), "call"))
				{
					CallSite CS(dyn_cast<CallInst>(II));
					Value *called = CS.getCalledValue()->stripPointerCasts();
					FuncVector arrayIndexParents;
					if (dyn_cast<Function>(called))
						if (getIndexAccessFromFunc(*(dyn_cast<Function>(called)), arrayIndexFunctions, CG, arrayIndexParents))
						{
							auto it = arrayIndexCalcBlocks.find(arrayIndexParents.back());
							if (it != arrayIndexCalcBlocks.end())
							{
								arrayIndexCallBlocks.insert({dyn_cast<Function>(called), it->second});
								arrayIndexCallSites.push_back(&*II);
							}
						}
				}

	// Find where the value for A[i] is loaded to be used
	std::vector<SinValIndExport> sinValIndExport;
	for (auto i : arrayIndexCallSites)
	{
		InstrVector callValStores;
		if (getUserFromInstrType(*i, callValStores, "store"))
		{
			Value *callValStore = (callValStores.back())->getOperand(1);

			for (auto j : callValStore->users())
			{
				Instruction *callValStoreUser = dyn_cast<Instruction>(j);
				if (!strcmp(callValStoreUser->getOpcodeName(), "load") &&
						callValStoreUser->getOperand(0) == callValStore)
				{
					InstrVector aiStores;
					if (getUserFromInstrType(*callValStoreUser, aiStores, "store"))
					{
						Value *aiValStore = (aiStores.back())->getOperand(1);

						for (auto k : aiValStore->users())
						{
							Instruction *aiValUser = dyn_cast<Instruction>(k);
							if (!strcmp(callValStoreUser->getOpcodeName(), "load") &&
									aiValUser->getOperand(0) == aiValStore)
							{
								InstrVector aiValFuncCalls;
								if (getUserFromInstrType(*aiValUser, aiValFuncCalls, "invoke"))
								{
									Instruction *aiValFuncCall = aiValFuncCalls.back();
									CallSite CS(dyn_cast<InvokeInst>(aiValFuncCall));
									Value *called = CS.getCalledValue()->stripPointerCasts();
									FuncVector arrayIndexParents;
									if (dyn_cast<Function>(called))
										if (getIndexAccessFromFunc(*(dyn_cast<Function>(called)), arrayIndexFunctions, CG, arrayIndexParents))
										{
											// CS1 denotes function called by the invoke/call instruction
											// Retrieve the data structure load for both function calls.
											CallSite CS1(dyn_cast<InvokeInst>(i));
											Value *called1 = CS1.getCalledValue()->stripPointerCasts();
											auto it1 = arrayIndexCallBlocks.find(dyn_cast<Function>(called1));
											auto it2 = arrayIndexCallBlocks.find(dyn_cast<Function>(called));
											if (it1 != arrayIndexCallBlocks.end() && it2 != arrayIndexCallBlocks.end())
												sinValIndExport.push_back({i->getFunction(), it1->second, it2->second});
										}
								}
								else if (getUserFromInstrType(*aiValUser, aiValFuncCalls, "call"))
								{
									Instruction *aiValFuncCall = aiValFuncCalls.back();
									CallSite CS(dyn_cast<CallInst>(aiValFuncCall));
									Value *called = CS.getCalledValue()->stripPointerCasts();
									FuncVector arrayIndexParents;
									if (dyn_cast<Function>(called))
										if (getIndexAccessFromFunc(*(dyn_cast<Function>(called)), arrayIndexFunctions, CG, arrayIndexParents))
										{
											// CS1 denotes function called by the invoke/call instruction
											// Retrieve the data structure load for both function calls.
											CallSite CS1(dyn_cast<CallInst>(i));
											Value *called1 = CS1.getCalledValue()->stripPointerCasts();
											auto it1 = arrayIndexCallBlocks.find(dyn_cast<Function>(called1));
											auto it2 = arrayIndexCallBlocks.find(dyn_cast<Function>(called));
											if (it1 != arrayIndexCallBlocks.end() && it2 != arrayIndexCallBlocks.end())
												sinValIndExport.push_back({i->getFunction(), it1->second, it2->second});
										}
								}
							}
						}
					}
				}
			}
		}
	}

	for (auto i : sinValIndExport) {
		errs() << "Single Valued Indirection in function " << i.function->getName() << "\n"
		<< *(i.loadDS1) << "\n" << *(i.loadDS2) << "\n\n";

		GEPDepInfo g;
		g.source = i.loadDS1->getOperand(0);
		g.target = i.loadDS2->getOperand(0);
		Result.geps.push_back(g);
	}
	return false;
}

void SinValIndirectionPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<CallGraphWrapperPass>();
}

char SinValIndirectionPass::ID = 0;
static RegisterPass<SinValIndirectionPass> X("SinValIndirectionPass", "Ranged Indirection for Project",
		false /* Only looks at CFG */,
		false /* Analysis Pass */);

static RegisterStandardPasses Y(
		PassManagerBuilder::EP_EarlyAsPossible,
		[](const PassManagerBuilder &Builder,
				legacy::PassManagerBase &PM) { PM.add(new SinValIndirectionPass()); });
