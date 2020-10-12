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

// Clone Function
#include "llvm/Transforms/Utils/Cloning.h"

#include "llvm/Support/CommandLine.h"
// using llvm::cl::opt
// using llvm::cl::list
// using llvm::cl::desc
// using llvm::cl::Hidden

#include <string>
#include <fstream>

#define DEBUG 1
#define INCLUDE_INTERPROC 0

llvm::cl::opt<std::string> FunctionWhiteListFile(
		"func-wl-file", llvm::cl::Hidden,
		llvm::cl::desc("function whitelist file"));

#define MAX_STACK_COUNT 10

namespace {

bool getSizeCalc(llvm::Value &I, std::set<llvm::Value *> &vals, int stack_count = 0) {
	bool ret = false;

	if (llvm::Instruction* Instr = dyn_cast<llvm::Instruction>(&I)) {

		for (int i = 0; i < Instr->getNumOperands(); ++i) {
			if (auto *user = llvm::dyn_cast<llvm::Instruction>(Instr->getOperand(i))) {

				if (user->getOpcode() == Instruction::Call) {
					if (dyn_cast<llvm::CallInst>(user)->getCalledFunction()->getName().str() == std::string("llvm.umul.with.overflow.i64")) {
						//						llvm::errs() << "\nFound " << *user << "\n";
						ret = true;
						vals.insert(user);
						return true;
					}
				}

				if (stack_count < 200) {
					//			llvm::errs() << "Stack Count: " << stack_count << "\n";
					ret |= getSizeCalc(*user, vals, ++stack_count);
				}
			}
		}
	}

	return ret;
}

// TODO: Extract information from new
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
#if DEBUG == 1
					errs() << "New Array Alloc: " << I << "\n"; // Pointer
					errs() << "Argument0:" << *(CS.getArgOperand(0)); // Total Size
#endif

					// Get element size
					std::set<llvm::Value*> vals;
					getSizeCalc(*(CS.getArgOperand(0)),vals);
					if (vals.size() > 0) {
#if DEBUG == 1
						llvm::errs() << "\ngetSizeCalc: \n";
#endif
						for (auto v : vals) {
#if DEBUG == 1
							llvm::errs() << *v << "\n";
#endif
							CallSite size(v);
#if DEBUG == 1
							llvm::errs() << *(size.getArgOperand(1));
#endif

							myAllocCallInfo allocInfo;
							allocInfo.allocInst = &I;
							allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),CS.getArgOperand(0));
							allocInfo.inputArguments.insert(allocInfo.inputArguments.end(),size.getArgOperand(1));
							allocInfos.push_back(allocInfo);
						}
#if DEBUG == 1
						llvm::errs() << "\n";
#endif
					}
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
#if DEBUG == 1
					errs() << "Alloc: " << I << "\n";
					errs() << "Argument0:" << *(CS.getArgOperand(0)) << "\n";
#endif
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

bool recurseUsesForGEP(llvm::Instruction &I,llvm::Instruction*&use, int stack_count = 0) {
	bool ret = false;

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == llvm::Instruction::Ret) {
			use = user;
			return true;
		}

		if (stack_count < MAX_STACK_COUNT) {
			ret |= recurseUsesForGEP(*user,use, ++stack_count);
		}
	}

	return ret;
}

// First argument is a function on which a GEP instruction depends.
// This function iterates over the instructions in this function, and searches for GEP instructions on which the return of that function depends
bool funcUsesGEP(llvm::Function *F, std::set<llvm::Instruction*> & GEPs, int stack_count = 0)
{
	bool ret = false;

	for (auto &BB : *F) {
		for (auto &I : BB) {
			if (I.getOpcode() == llvm::Instruction::GetElementPtr) {
				llvm::Instruction* use;
				if (recurseUsesForGEP(I,use)) {
					//					llvm::errs() << "Function " << F->getName().str().c_str() << " has GEP: " << I <<
					//							" that is used in return call: " << *use << "\n\n";
					GEPs.insert(&I);
					ret |= true;
				}
			}
			else if (I.getOpcode() == llvm::Instruction::Call) {
				if (stack_count < MAX_STACK_COUNT) {
					funcUsesGEP(dyn_cast<CallInst>(&I)->getCalledFunction(),GEPs, ++stack_count);
				}
			}
		}
	}

	return ret;
}

// This function checks if a GEP instruction is dependent on a call instruction
bool getCallGEPUses(llvm::Instruction &I,
		std::vector<std::pair<llvm::Instruction *,llvm::Instruction*>> &uses, llvm::Instruction *func = nullptr, int stack_count = 0) {
	bool ret = false;

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::GetElementPtr) {
			//			llvm::errs() << "GEP:" << *user << "\nUses:" << *func << "\n";
			ret = true;
			uses.push_back({user,func});
			return true;
		}

		// Recurse over instruction dependence chain
		if (stack_count < MAX_STACK_COUNT) {
			if (func == nullptr) {
				ret |= getCallGEPUses(*user, uses, &I, ++stack_count);
			}
			else {
				ret |= getCallGEPUses(*user, uses, func, ++stack_count);
			}
		}
	}

	return ret;
}

bool recurseUsesSilent(llvm::Instruction &I,
		std::vector<llvm::Instruction *> &uses, llvm::SmallPtrSetImpl<llvm::Instruction *> &visited, int stack_count = 0) {
	bool ret = false;

	//llvm::errs() << I << "\n";
	if(visited.count(&I)) {
		return false;
	}
	visited.insert(&I);

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::GetElementPtr) {
			ret = true;

			auto found = std::find(uses.begin(), uses.end(), u);

			if(found == uses.end()) {
				uses.push_back(user);
			}
			//			return true;
		}

		//if (stack_count < MAX_STACK_COUNT) {
		//			llvm::errs() << "Stack Count: " << stack_count << "\n";
		ret |= recurseUsesSilent(*user, uses, visited, ++stack_count);
		//}
	}

	return ret;
}

llvm::Instruction * findGEPToSameBasePtr(llvm::Function &F, llvm::Instruction & firstI) {

	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			//			if (I.getOpcode() == llvm::Instruction::GetElementPtr) {
			//				llvm::errs() << "findGEPToSameBasePtr " << firstI << " " << I;
			//			}
			if (I.getOpcode() == llvm::Instruction::GetElementPtr &&
					firstI.getOperand(0) == I.getOperand(0) &&
					&firstI != &I) {
				//				llvm::errs() << "True\n";
				return &I;
			}
			//			else {
			//				llvm::errs() << "\n";
			//			}
		}
	}

	return nullptr;

}

bool areCompared(Instruction * I, Instruction * I2) {
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

llvm::Instruction * getRIDepLoad(llvm::Instruction * I) {
	if (llvm::Instruction * load = dyn_cast<llvm::Instruction>(I->getOperand(0))) {
		if (load->getOpcode() == llvm::Instruction::Load) {
			return load;
		}
	}
	return nullptr;
}

llvm::Instruction * dependsOnGEP(llvm::Instruction * I) {
	if (llvm::Instruction * load = dyn_cast<llvm::Instruction>(I->getOperand(0))) {
		if (load->getOpcode() != llvm::Instruction::Load) {
			return nullptr;
		}

		if (llvm::Instruction * gep = dyn_cast<llvm::Instruction>(load->getOperand(0))) {
			if (gep->getOpcode() != llvm::Instruction::GetElementPtr) {
				return nullptr;
			}
			else {
				return gep;
			}
		}
	}

	return nullptr;
}

void identifyRangedIndirection(Function &F, llvm::SmallVectorImpl<GEPDepInfo> & riInfos) {

	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == llvm::Instruction::GetElementPtr) {
				llvm::Instruction * otherGEP = findGEPToSameBasePtr(F, I);
				if (otherGEP) {
					GEPDepInfo gepdepinfo;
					if (areCompared(&I,otherGEP) && dependsOnGEP(&I)) {
#if DEBUG == 1
						llvm::errs() << "Ranged Indirection Identified!\n";
						llvm::errs() << "Source: " << *dependsOnGEP(&I) << "\n";
						llvm::errs() << "Target: " << I << "\n";
#endif
						gepdepinfo.source = dependsOnGEP(&I);
						gepdepinfo.load_to_copy = getRIDepLoad(&I);
						gepdepinfo.target = &I;
						riInfos.push_back(gepdepinfo);
					}
				}
			}
		}
	}

}

void walkOperands(llvm::Instruction & I, llvm::SmallVector<llvm::Instruction*,1> & geps, int iters = 0) {

	for (int i = 0; i < I.getNumOperands(); ++i) {
		auto * op = I.getOperand(i);
		if (llvm::Instruction * I2 = dyn_cast<llvm::Instruction>(op)) {
			if (I2->getOpcode() == llvm::Instruction::GetElementPtr) {
				geps.push_back(I2);
				return;
			}
			else {
				if (iters < 1) {
					walkOperands(*I2,geps,++iters);
				}
			}
		}
	}
}

void identifyGEPDependenceOpWalk(Function &F,
		llvm::SmallVectorImpl<GEPDepInfo> &gepInfos) {

	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == Instruction::GetElementPtr) {
				llvm::SmallVector<llvm::Instruction*,1> geps;
				if (llvm::Instruction * IOp = dyn_cast<llvm::Instruction>(I.getOperand(0))) {
					walkOperands(*IOp, geps);
				}
				if (geps.size() > 0) {
					GEPDepInfo g;
					if (&I != geps[0])
					{
						g.source = I.getOperand(0);
						g.funcSource = I.getParent()->getParent();
						g.target = geps[0]->getOperand(0);
						g.funcTarget = geps[0]->getParent()->getParent();
#if DEBUG == 1
						errs() << "source: " << *(g.source) << "\n";
						errs() << "target: " << *(g.target) << "\n";
#endif
					}
				}
			}
		}
	}
}

void identifyGEPDependenceOpWalk2(Function &F,
		llvm::SmallVectorImpl<GEPDepInfo> &gepInfos) {

	for (llvm::BasicBlock &BB : F) {
		for (llvm::Instruction &I : BB) {
			if (I.getOpcode() == Instruction::GetElementPtr) {
				llvm::Instruction * source = dependsOnGEP(&I);
				if (source != nullptr) {
					GEPDepInfo g;
					std::string type_str;
					llvm::raw_string_ostream rso(type_str);
					source->getOperand(0)->getType()->print(rso);

					std::string type_str_target;
					llvm::raw_string_ostream rso_target(type_str_target);
					I.getOperand(0)->getType()->print(rso_target);

					if (rso.str().find(std::string("string")) == std::string::npos &&
							rso_target.str().find(std::string("string")) == std::string::npos) {
						g.source = source->getOperand(0);
						g.funcSource = source->getParent()->getParent();
						g.target = I.getOperand(0);
						g.funcTarget = I.getParent()->getParent();

						gepInfos.push_back(g);

#if DEBUG == 1
						errs() << "source: " << *(source) << "\n";
						errs() << "target: " << I << "\n";
#endif
					}

					//#if DEBUG == 1
					//						errs() << "source: " << *(g.source) << "\n";
					//						errs() << "target: " << *(g.target) << "\n";
					//#endif
				}
			}
		}
	}
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

			if (I.getOpcode() == Instruction::Load) { // TODO: Is there a reason for pushing back the load twice?
				loads.push_back(&I);                  // Or at all? We don't seem to use these loads later in the code
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
				llvm::SmallPtrSet<llvm::Instruction *, 20> visited;

				if (usedInLoad(I) && recurseUsesSilent(*I, uses, visited)) {
					for (auto U : uses) {
						if (usedInLoad(U)) {
#if DEBUG == 1
							errs() << "\n" << demangle(F.getName().str().c_str()) << "\n";
							errs() << *I;
							printVector("\n  is used by:\n", uses.begin(), uses.end());
							errs() << "\n";
#endif
							GEPDepInfo g;
							g.source = I->getOperand(0);
							g.funcSource = I->getParent()->getParent();
							g.target = U->getOperand(0);
							g.funcTarget = U->getParent()->getParent();

#if DEBUG == 1
							errs() << "source: " << *(g.source) << "\n";
							errs() << "target: " << *(g.target) << "\n";
#endif
							gepInfos.push_back(g);
						}
					}
				}
			}
#if INCLUDE_INTERPROC == 1
			else if (I->getOpcode() == llvm::Instruction::Call){
				std::vector<std::pair<llvm::Instruction *,llvm::Instruction*>> uses;
				if (getCallGEPUses(*I, uses)) {
					//					llvm::Instruction * GEP;
					std::set<llvm::Instruction*> GEPs;

					Function* fp = dyn_cast<CallInst>(I)->getCalledFunction();
					if (fp==NULL) {
						//						llvm::errs() << "OMG1 " << *I << "\n";
						Value* v=dyn_cast<CallInst>(I)->getCalledValue();
						Value* sv = v->stripPointerCasts();

						//						llvm::errs() << "OMG" << v->getName().str().c_str() << "\n";
#if DEBUG == 1
						if (llvm::dyn_cast<Function>(sv)) {
							errs()<< "Indirect function: \n";
						}
#endif
						continue;
					}

					if (funcUsesGEP(dyn_cast<CallInst>(I)->getCalledFunction(), GEPs)) {
#if DEBUG == 1
						llvm::errs() << "Edges spanning functions:\n";
#endif

						std::set<std::pair<llvm::Instruction*,llvm::Instruction*>> edges;

						for (auto instr : uses) {
							if (usedInLoad(instr.first)) {
								for (auto I2 : GEPs) {
									edges.insert({instr.first, I2});
								}
							}
						}

						std::set<std::pair<llvm::Instruction*,llvm::Instruction*>> filtered_edges(edges);

						for (auto pair : edges) {
							for (auto pair2 : edges) {
								if (pair != pair2) {
									if ((pair.first)->getOperand(0) == (pair2.first)->getOperand(0) &&
											(pair.second)->getOperand(0) == (pair2.second)->getOperand(0)) {
										if (filtered_edges.find(pair) != filtered_edges.end()) {
											filtered_edges.erase(pair2);
										}
									}
								}
							}
						}

						for (auto pair : filtered_edges) {
#if DEBUG == 1
							llvm::errs() << "source: " << *(pair.first->getOperand(0)) << "\n";
							llvm::errs() << "target: " << *(pair.second->getOperand(0)) << "\n\n";
#endif
							GEPDepInfo g;
							g.source = pair.first->getOperand(0);
							g.funcSource = pair.first->getParent()->getParent(); // TODO: funcSource and funcTarget probably not needed here
							g.target = pair.second->getOperand(0);
							g.funcTarget = pair.second->getParent()->getParent();
							//							gepInfos.push_back(g);
						}

						// Create slimmed down copy of the function that only calculates addr
						// 1) Remove load that uses GEP result
						// 2) Change return type of function to ptr
						// 3) (Optional) Remove any instructions that are not necessary for GEP
						//						{
						//							// Copy function signature from old function (arguments only)
						//							Function * F_old = dyn_cast<CallInst>(I)->getCalledFunction();
						//							FunctionType *FT = F_old->getFunctionType();
						//							SmallVector<Type *, 8> ArgTypes;
						//							for (unsigned i = 0; i < FT->getNumParams(); ++i) {
						//								ArgTypes.push_back(FT->getParamType(i));
						//							}
						//
						//							// Create new function type - change the return type, use existing args
						//							llvm::FunctionType * FT1 = llvm::FunctionType::get(llvm::Type::getInt8PtrTy(F_old->getParent()->getContext()),FT);
						//
						//							// Create new function
						//							llvm::Function * NewF = Function::Create(FT1, llvm::Function::LinkageTypes::InternalLinkage, F_old->getName().str() + "_clone");
						//
						//							F.getParent()->getFunctionList().push_back(NewF);
						//
						//
						//							// Map the arguments from the old function to the new
						//							ValueToValueMapTy VMap;
						//
						//							auto NewArgI = NewF->arg_begin();
						//							for (auto ArgI = F_old->arg_begin(), ArgE = F_old->arg_end(); ArgI != ArgE; ++ArgI, ++NewArgI) {
						//								VMap[&*ArgI] = &*NewArgI;
						//							}
						//
						//
						//							//							VMap[&*F_old->arg_begin()] = &*NewF->arg_begin();
						//
						//							// Again, this is just copied from an example I found in LLVM source. Needed for CloneFunctionInto
						//							SmallVector<ReturnInst*, 4> Returns;
						//
						//							// Clone function
						////							CloneFunctionInto(NewF, F_old, VMap, /*ModuleLevelChanges=*/false, Returns);
						//							//
						//							//							llvm::errs() << "BOOBOO 6\n";
						//							//
						//							//							// Find all ret calls in new function
						//							//							std::vector<llvm::Instruction*> returns;
						//							//							for (llvm::BasicBlock & BB_new : *NewF) {
						//							//								for (llvm::Instruction & I_new : BB_new) {
						//							//									if (I_new.getOpcode() == llvm::Instruction::Ret) {
						//							//										returns.push_back(&I_new);
						//							//									}
						//							//								}
						//							//							}
						//							//
						//							//							llvm::errs() << "BOOBOO 7\n";
						//							//
						//							//							// Find the GEP that the return depends on
						//							//							funcUsesGEP(NewF, GEP);
						//							//
						//							//							llvm::errs() << "BOOBOO 8\n";
						//							//							// delete the returns (tbh we should delete everything after that GEP..)
						//							//							for (llvm::Instruction* I_del: returns) {
						//							//								I_del->eraseFromParent();
						//							//							}
						//							//
						//							//							llvm::errs() << "BOOBOO 9\n";
						//							//
						//							//							// Create new return instruction in new function
						//							//							auto *ret = llvm::ReturnInst::Create(NewF->getParent()->getContext(),GEP, GEP->getNextNode());
						//							//							llvm::errs() << "BOOBOO 10\n";
						//						}
					}

				}
			}
#endif
		}
	}
}

} // namespace

void PrefetcherPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<TargetLibraryInfoWrapperPass>();
	AU.addRequired<MemorySSAWrapperPass>();
	AU.addRequired<DependenceAnalysisWrapperPass>();
	//	AU.addRequired<CallGraphWrapperPass>(); // Need to convert PrefetcherPass to ModulePass to get this
	AU.setPreservesAll();
}

bool PrefetcherPass::runOnFunction(llvm::Function &F) {

#if DEBUG == 1
	errs() << "PrefetcherPass: " << F.getName() << "\n";
#endif

	auto not_in = [](const auto &C, const auto &E) {
		return C.end() == std::find(std::begin(C), std::end(C), E);
	};

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

	if (FunctionWhiteListFile.getPosition() &&
			not_in(FunctionWhiteList, std::string{F.getName()})) {
		LLVM_DEBUG(llvm::dbgs() << "skipping func: " << F.getName()
				<< " reason: not in whitelist\n";);
		return false;
	}

	Result->allocs.clear();
	auto &TLI = getAnalysis<llvm::TargetLibraryInfoWrapperPass>().getTLI(F);

	identifyMalloc(F, Result->allocs);
	identifyNewA(F, Result->allocs);
	//	identifyGEPDependence(F, Result->geps);
	identifyGEPDependenceOpWalk2(F, Result->geps);
	identifyRangedIndirection(F,Result->ri_geps);

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


