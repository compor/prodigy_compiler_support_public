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

#define MAX_STACK_COUNT 10

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
		std::vector<llvm::Instruction *> &uses, int stack_count = 0) {
	bool ret = false;

	//	llvm::errs() << I << "\n";

	for (auto &u : I.uses()) {
		auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

		if (user->getOpcode() == Instruction::GetElementPtr) {
			ret = true;
			uses.push_back(user);
			//			return true;
		}

		if (stack_count < MAX_STACK_COUNT) {
			//			llvm::errs() << "Stack Count: " << stack_count << "\n";
			ret |= recurseUsesSilent(*user, uses, ++stack_count);
		}
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

			if (I.getOpcode() == Instruction::Load) { // TODO: Is there a reason for pushing back the load twice?
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
					//					llvm::Instruction * GEP;
					std::set<llvm::Instruction*> GEPs;
					if (funcUsesGEP(dyn_cast<CallInst>(I)->getCalledFunction(), GEPs)) {

						llvm::errs() << "Edges spanning functions:\n";

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
							llvm::errs() << "source: " << *(pair.first->getOperand(0)) << "\n";
							llvm::errs() << "target: " << *(pair.second->getOperand(0)) << "\n\n";
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


