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
#include <string>
#include <fstream>

// project
#include "prefetcher.hpp"
#include "util.hpp"

// Register Pass
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/LegacyPassManager.h"

// Clone Function
#include "llvm/Transforms/Utils/Cloning.h"

#include "llvm/Support/CommandLine.h"

#define DEBUG 0
#define MAX_STACK_COUNT 2

llvm::cl::opt<std::string> FunctionWhiteListFile(
		"func-wl-file", llvm::cl::Hidden,
		llvm::cl::desc("function whitelist file"));

namespace {

bool getAllocationSizeCalc(llvm::Value &I, std::set<llvm::Value *> &vals, int stack_count = 0) {
	bool ret = false;

	if (llvm::Instruction* Instr = dyn_cast<llvm::Instruction>(&I)) {

		for (int i = 0; i < Instr->getNumOperands(); ++i) {
			if (auto *user = llvm::dyn_cast<llvm::Instruction>(Instr->getOperand(i))) {

				if (user->getOpcode() == Instruction::Call) {
					if (dyn_cast<llvm::CallInst>(user)->getCalledFunction()->getName().str() == std::string("llvm.umul.with.overflow.i64")) {
						ret = true;
						vals.insert(user);
						return true;
					}
				}

				if (stack_count < 200) {
					ret |= getAllocationSizeCalc(*user, vals, ++stack_count);
				}
			}
		}
	}

	return ret;
}

void identifyNewA(llvm::Function &F,
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
					std::set<llvm::Value*> vals;
					getAllocationSizeCalc(*(CS.getArgOperand(0)),vals);
					if (vals.size() > 0) {
						for (auto v : vals) {
							CallSite size(v);
							myAllocCallInfo allocInfo;
							allocInfo.allocInst = &I;
							allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),CS.getArgOperand(0));
							allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),size.getArgOperand(1));
							allocInfos.push_back(allocInfo);
						}
					}
					else { }
				}
			}
		}
	}
}

bool isTargetGEPusedInLoad(llvm::Instruction *I) {
	for (auto &u : I->uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::Load) {
			return true;
		}
	}

	return false;
}

llvm::Instruction * findGEPToSameBasePtr(llvm::Function &F, llvm::Instruction & firstI) {
	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == llvm::Instruction::GetElementPtr &&
					firstI.getOperand(0) == I.getOperand(0) && &firstI != &I) {
				return &I;
			}
		}
	}
	return nullptr;
}

bool areUsedInComparisonOp(Instruction * I, Instruction * I2) {
	llvm::SmallVector<llvm::Instruction*,8> I_loads;
	llvm::SmallVector<llvm::Instruction*,8> I2_loads;

	for (auto &u : I->uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());
		if (dyn_cast<llvm::Instruction>(user)->getOpcode() == llvm::Instruction::Load) {
			I_loads.push_back(dyn_cast<llvm::Instruction>(user));
		}
	}

	for (auto &u : I2->uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());
		if (dyn_cast<llvm::Instruction>(user)->getOpcode() == llvm::Instruction::Load) {
			I2_loads.push_back(dyn_cast<llvm::Instruction>(user));
		}
	}

	for (auto l1 : I_loads) {
		for (auto l2 : I2_loads) {
			for (auto &l1_u : l1->uses()) {
				auto *user = llvm::dyn_cast<llvm::Instruction>(l1_u.getUser());
				if (dyn_cast<llvm::Instruction>(user)->getOpcode() == llvm::Instruction::ICmp) {
					for (auto &l2_u : l2->uses()) {
						auto *user_2 = llvm::dyn_cast<llvm::Instruction>(l2_u.getUser());
						if (user == user_2) {
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

void findSourceGEPCandidates(Function &F, llvm::SmallVectorImpl<llvm::Instruction*> & source_geps)
{
	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == Instruction::GetElementPtr) {
				source_geps.push_back(&I);
			}
		}
	}
}

void getLoadsUsingSourceGEP(llvm::Instruction * I, llvm::SmallVectorImpl<llvm::Instruction*> & loads, int iter = 0)
{
	if (dyn_cast<llvm::PHINode>(I)) {
		llvm::PHINode * pI = dyn_cast<llvm::PHINode>(I);

		for (auto &u : I->uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if (user->getOpcode() == Instruction::Load) {
				loads.push_back(user);
				return;
			}
			else if (iter < 20 && user->getOpcode() != Instruction::GetElementPtr && user->getOpcode() != Instruction::Store) { // Allow up to three modifications of value between gep calc and load, provided that they are not a store or another GEP
				getLoadsUsingSourceGEP(user, loads, ++iter);
			}
			else if (iter >= 20) {
				return;
			}
		}
	}
	else {
		for (auto &u : I->uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if (user->getOpcode() == Instruction::Load) {
				loads.push_back(user);
				return;
			}
			else if (iter < 20 && user->getOpcode() != Instruction::GetElementPtr && user->getOpcode() != Instruction::Store) { // Allow up to three modifications of value between gep calc and load, provided that they are not a store or another GEP
				getLoadsUsingSourceGEP(user, loads, ++iter);
			}
			else if (iter >= 20) {
				return;
			}
		}
	}
}

void getGEPsUsingLoad(llvm::Instruction * I, llvm::SmallVectorImpl<llvm::Instruction*> & target_geps, int iter = 0)
{
	for (auto &u : I->uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::Store) { }
		else if (user->getOpcode() == Instruction::GetElementPtr) {
			if (user->getOperand(1) == I) { // GEP is dependent only if load result is used as an index
				target_geps.push_back(user);
			}
		}
		else if (user->getOpcode() == Instruction::Load) { }

		if (iter < 5) { // Allow up to three modifications of value between gep calc and load, provided that they are not a store or another GEP
			getGEPsUsingLoad(user, target_geps, ++iter);
		}
		else {
			return;
		}
	}
}

bool RIfindLoadUsingGEP(llvm::Instruction * src, std::vector<llvm::Instruction *> &targets, int stack_count = 0) {
	bool ret = false;
	for (auto &u : src->uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == llvm::Instruction::Load) {
			targets.push_back(user);
			return true;
		}

		if (stack_count < MAX_STACK_COUNT) {
			ret |= RIfindLoadUsingGEP(user, targets, ++stack_count);
		}
	}
	return ret;
}

void identifyCorrectRangedIndirection(Function &F, llvm::SmallVectorImpl<GEPDepInfo> & riInfos) {
	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == llvm::Instruction::GetElementPtr) {
				llvm::Instruction * otherGEP = findGEPToSameBasePtr(F, I);
				if (otherGEP) {
					GEPDepInfo gepdepinfo;
					if (areUsedInComparisonOp(&I,otherGEP)) {
						std::vector<llvm::Instruction*> targets;
						bool found_load = RIfindLoadUsingGEP(&I, targets);
						if (found_load) {
							gepdepinfo.source = I.getOperand(0);
							gepdepinfo.target = targets.at(0);
							riInfos.push_back(gepdepinfo);
						}
					}
				}
			}
		}
	}
}

void identifyCorrectGEPDependence(Function &F,
		llvm::SmallVectorImpl<GEPDepInfo> &gepInfos) {

	llvm::SmallVector<llvm::Instruction*,8> source_geps;
	findSourceGEPCandidates(F,source_geps);

	for (auto I : source_geps) {
		llvm::SmallVector<llvm::Instruction*,8> loads;
		getLoadsUsingSourceGEP(I, loads);

		for (auto ld : loads) {
			llvm::SmallVector<llvm::Instruction*,8> target_geps;
			getGEPsUsingLoad(ld, target_geps);
			for (auto target_gep : target_geps) {
				if (isTargetGEPusedInLoad(target_gep)) {
					GEPDepInfo g;
					g.source = I->getOperand(0);
					g.source_use = ld;
					g.funcSource = I->getParent()->getParent();
					g.target = target_gep->getOperand(0);
					g.funcTarget = target_gep->getParent()->getParent();
					gepInfos.push_back(g);
					// If the source GEP comes from a PHI node, we use the result of the phi node as the source edge, and insert the registration call
					// immediately after the phi nodes
					if (dyn_cast<llvm::Instruction>(ld->getOperand(0))->getOpcode() == llvm::Instruction::PHI) {
						g.phi_node = dyn_cast<llvm::Instruction>(ld->getOperand(0));
						g.phi = true;
					}
				}
			}
		}
	}
}

void removeDuplicates(std::set<GEPDepInfo> &svInfos, std::set<GEPDepInfo> &riInfos)
{
	llvm::SmallVector<GEPDepInfo,8> duplicates;
	for (auto g : riInfos) {
		std::set<GEPDepInfo>::iterator duplicate = std::find(svInfos.begin(), svInfos.end(), g);
		if (duplicate != svInfos.end()) {
			svInfos.erase(duplicate);
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

bool in(llvm::SmallVectorImpl<std::string> &C, std::string E) {
	for (std::string entry : C) {
		if (E.find(entry) != std::string::npos) {
			return true;
		}
	}
	return false;
};

bool PrefetcherPass::runOnFunction(llvm::Function &F) {
	llvm::SmallVector<std::string, 32> FunctionWhiteList;

	if (FunctionWhiteListFile.getPosition()) {
		std::ifstream wlFile{FunctionWhiteListFile};

		std::string funcName;
		while (wlFile >> funcName) {
			FunctionWhiteList.push_back(funcName);
		}
	}

	if (F.isDeclaration()) {
		return false;
	}

	Result->allocs.clear();
	Result->geps.clear();
	Result->ri_geps.clear();
	auto &TLI = getAnalysis<llvm::TargetLibraryInfoWrapperPass>().getTLI(F);

	identifyNewA(F, Result->allocs);

	if (FunctionWhiteListFile.getPosition() &&
			!in(FunctionWhiteList, F.getName().str())) {
		llvm::errs() << "skipping func: " << F.getName() << " reason: not in whitelist\n";;
		return false;
	}

	identifyCorrectGEPDependence(F, Result->geps);
	identifyCorrectRangedIndirection(F,Result->ri_geps);

	return false;
}
char PrefetcherPass::ID = 0;

static llvm::RegisterPass<PrefetcherPass> X("prefetcher", "Prefetcher Pass",
		false, false);


