// LLVM
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instruction.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemorySSA.h"

#include "llvm/ADT/SmallVector.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

// standard
#include <vector>

// project
#include "prefetcher.hpp"
#include "util.hpp"

// Register Pass
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

namespace {

// TODO: Extract information from new
void identifyNew(llvm::Function &F,
		llvm::SmallVectorImpl<myAllocCallInfo> &allocInfos) {
	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			llvm::CallSite CS(&I);
			if (!CS.getInstruction()) {
				continue;
			}
			llvm::Value *called = CS.getCalledValue()->stripPointerCasts();

			if (llvm::Function *f = llvm::dyn_cast<llvm::Function>(called)) {
				if (f->getName().equals("_Znam")) {
					errs() << "New Array Alloc: " << I << "\n";
					errs() << "Argument0:" << *(CS.getArgOperand(0));
				}
			}
		}
	}
}

void identifyMalloc(llvm::Function &F,
		llvm::SmallVectorImpl<myAllocCallInfo> &allocInfos) {
	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			llvm::CallSite CS(&I);
			if (!CS.getInstruction()) {
				continue;
			}
			llvm::Value *called = CS.getCalledValue()->stripPointerCasts();

			if (llvm::Function *f = llvm::dyn_cast<llvm::Function>(called)) {
				if (f->getName().equals("malloc")) {
					errs() << "Alloc: " << I << "\n";
					errs() << "Argument0:" << *(CS.getArgOperand(0)) << "\n";
					myAllocCallInfo allocInfo;
					allocInfo.allocInst = &I;
					allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),
							CS.args().begin(), CS.args().end());
					allocInfos.push_back(allocInfo);
				}
			}
		}
	}
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

bool inArray(Instruction *I, std::vector<Instruction *> vec) {
	for (auto *v : vec) {
		if (I == v) {
			return true;
		}
	}

	return false;
}

bool recurseUsesForGEP(llvm::Instruction &I,llvm::Instruction*&use) {
	bool ret = false;

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == llvm::Instruction::Ret) {
			use = user;
			return true;
		}

		ret |= recurseUsesForGEP(*user,use);
	}

	return ret;
}

bool funcUsesGEP(llvm::Function *F)
{
	bool ret = false;

	for (auto &BB : *F) {
		for (auto &I : BB) {
			if (I.getOpcode() == llvm::Instruction::GetElementPtr) {
				llvm::Instruction* use;
				if (recurseUsesForGEP(I,use)) {
					llvm::errs() << "Function " << F->getName().str().c_str() << " has GEP: " << I <<
							" that is used in return call: " << *use;
				}
			}
		}
	}

	return ret;
}

bool getCallGEPUses(llvm::Instruction &I,
		std::vector<std::pair<llvm::Instruction *,llvm::Instruction*>> &uses, llvm::Instruction *func = nullptr) {
	bool ret = false;

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::GetElementPtr) {
			llvm::errs() << "GEP:" << *user << "\nUses:" << *func << "\n";
		}

		if (user->getOpcode() == Instruction::GetElementPtr) {
			ret = true;
			uses.push_back({user,func});
			return true;
		}

		if (func == nullptr) {
			ret |= getCallGEPUses(*user, uses, &I);
		}
		else {
			ret |= getCallGEPUses(*user, uses, func);
		}
	}

	return ret;
}

bool recurseUsesSilent(llvm::Instruction &I,
		std::vector<llvm::Instruction *> &uses) {
	bool ret = false;

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::GetElementPtr) {
			ret = true;
			uses.push_back(user);
			return true;
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
			else if (I.getOpcode() == Instruction::Call) {
				insns.push_back(&I);
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
							g.source = I->getOperand(0);
							g.funcSource = I->getParent()->getParent();
							g.target = U->getOperand(0);
							g.funcTarget = U->getParent()->getParent();

							errs() << "source: " << *(g.source) << "\n";
							errs() << "target: " << *(g.target) << "\n";
							gepInfos.push_back(g);
						}
					}
				}
			}
			else if (I->getOpcode() == llvm::Instruction::Call){
				std::vector<std::pair<llvm::Instruction *,llvm::Instruction*>> uses;
				if (getCallGEPUses(*I, uses)) {
					funcUsesGEP(dyn_cast<CallInst>(I)->getCalledFunction());
				}
			}
		}
	}
}

} // namespace

void PrefetcherPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<TargetLibraryInfoWrapperPass>();
	AU.addRequired<MemorySSAWrapperPass>();
	AU.addRequired<DependenceAnalysisWrapperPass>();
	AU.setPreservesAll();
}

bool PrefetcherPass::runOnFunction(llvm::Function &F) {

	errs() << "PrefetcherPass: " << F.getName() << "\n";

	Result->allocs.clear();
	auto &TLI = getAnalysis<llvm::TargetLibraryInfoWrapperPass>().getTLI(F);

	identifyMalloc(F, Result->allocs);
	identifyNew(F, Result->allocs);
	identifyGEPDependence(F, Result->geps);

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

char PrefetcherPass::ID = 0;

static llvm::RegisterPass<PrefetcherPass> X("prefetcher", "Prefetcher Pass",
		false, false);


