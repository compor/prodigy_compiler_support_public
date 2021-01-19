#include "llvm/Pass.h"
// using llvm::RegisterPass

#include "llvm/IR/Type.h"
// using llvm::Type

#include "llvm/IR/DerivedTypes.h"
// using llvm::IntegerType

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
// using llvm::Instruction

#include "llvm/IR/Function.h"
// using llvm::Function

#include "llvm/IR/Module.h"
// using llvm::Module

#include "llvm/IR/LegacyPassManager.h"
// using llvm::PassManagerBase

#include "llvm/Analysis/LoopInfo.h"
// using llvm::LoopInfoWrapperPass
// using llvm::LoopInfo
// using llvm::Loop

#include "llvm/Transforms/IPO/PassManagerBuilder.h"
// using llvm::PassManagerBuilder
// using llvm::RegisterStandardPasses

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallSet.h"
// using llvm::SmallVector

#include "llvm/Support/CommandLine.h"
// using llvm::cl::opt
// using llvm::cl::list
// using llvm::cl::desc
// using llvm::cl::value_desc
// using llvm::cl::location
// using llvm::cl::ZeroOrMore

#include "llvm/Support/raw_ostream.h"
// using llvm::raw_ostream

#include "llvm/Support/Debug.h"
// using DEBUG macro
// using llvm::dbgs

#include <vector>
// using std::vector

#include <string>
// using std::string

#include "llvm/IR/Dominators.h"
// dominator tree

#include "prefetcher.hpp"

#define DEBUG_TYPE "prefetcher-codegen"

// plugin registration for opt

#define DEBUG 0

namespace {

struct PrefetcherRuntime {
	static constexpr char *CreateParams = "create_params";
	static constexpr char *CreateEnable = "create_enable";
	static constexpr char *RegisterNode = "register_node";
	static constexpr char *RegisterIdentifyEdgeSource = "register_identify_edge_source";
	static constexpr char *RegisterIdentifyEdgeTarget = "register_identify_edge_target";
	static constexpr char *RegisterIdentifyEdge = "register_identify_edge";
	static constexpr char *RegisterNodeWithSize = "register_node_with_size";
	static constexpr char *RegisterTravEdge1 = "register_trav_edge1";
	static constexpr char *RegisterTravEdge2 = "register_trav_edge2";
	static constexpr char *RegisterTrigEdge1 = "register_trig_edge1";
	static constexpr char *RegisterTrigEdge2 = "register_trig_edge2";
	static constexpr char *SimUserPfSetParam = "sim_user_pf_set_param";
	static constexpr char *SimUserPfSetEnable = "sim_user_pf_set_enable";
	static constexpr char *SimUserPfEnable = "sim_user_pf_enable";
	static constexpr char *SimUserWait = "sim_user_wait";
	static constexpr char *SimRoiStart = "sim_roi_start";
	static constexpr char *SimRoiEnd = "sim_roi_end";
	static constexpr char *SimUserPfDisable = "sim_user_pf_disable";
	static constexpr char *DeleteParams = "delete_params";
	static constexpr char *DeleteEnable = "delete_enable";

	static const std::vector<std::string> Functions;
};

const std::vector<std::string> PrefetcherRuntime::Functions = {
		PrefetcherRuntime::CreateParams,
		PrefetcherRuntime::CreateEnable,
		PrefetcherRuntime::RegisterIdentifyEdge,
		PrefetcherRuntime::RegisterIdentifyEdgeSource,
		PrefetcherRuntime::RegisterIdentifyEdgeTarget,
		PrefetcherRuntime::RegisterNode,
		PrefetcherRuntime::RegisterNodeWithSize,
		PrefetcherRuntime::RegisterTravEdge1,
		PrefetcherRuntime::RegisterTravEdge2,
		PrefetcherRuntime::RegisterTrigEdge1,
		PrefetcherRuntime::RegisterTrigEdge2,
		PrefetcherRuntime::SimUserPfSetParam,
		PrefetcherRuntime::SimUserPfSetEnable,
		PrefetcherRuntime::SimUserPfEnable,
		PrefetcherRuntime::SimUserWait,
		PrefetcherRuntime::SimRoiStart,
		PrefetcherRuntime::SimRoiEnd,
		PrefetcherRuntime::SimUserPfDisable,
		PrefetcherRuntime::DeleteParams,
		PrefetcherRuntime::DeleteEnable};

enum FuncId {
	// traversal functions registered
	TraversalHolder,
	BaseOffset_int32_t,
	PointerBounds_int32_t,
	PointerBounds_uint64_t,

	// trigger functions registered
	TriggerHolder,
	UpToOffset,
	StaticOffset_32,
	StaticOffset_64,
	StaticOffset_256,
	StaticOffset_512,
	StaticOffset_1024,

	// squash functions registered
	SquashIfLarger,
	NeverSquash,

	InvalidFuncId
};

class PrefetcherCodegen {
	llvm::Module *Mod;
	llvm::LoopInfo *LI;
	unsigned long NodeCount;
	unsigned long TriggerEdgeCount;

public:
	llvm::SmallPtrSet<llvm::Value *, 4> emittedNodes;
	llvm::SmallSet<struct GEPDepInfo, 4> emittedTravEdges;
	llvm::SmallPtrSet<llvm::Value *, 4> emittedTrigEdges;
	std::map<llvm::Value *, llvm::Instruction *> insertPts;
	unsigned int edgeCount = 0;

	PrefetcherCodegen(llvm::Module &M)
	: Mod(&M), LI(nullptr), NodeCount(0), TriggerEdgeCount(0){};

	void declareRuntime() {
		for (auto e : PrefetcherRuntime::Functions) {
			auto *funcType = llvm::FunctionType::get(
					llvm::Type::getInt32Ty(Mod->getContext()), true);

			Mod->getOrInsertFunction(e, funcType);
			DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs()
			<< "adding func: " << e << " to module "
			<< Mod->getName() << "\n");
		}

		return;
	}

	void emitRegisterNode(myAllocCallInfo &AI) {
		if (auto *func =
				Mod->getFunction(PrefetcherRuntime::RegisterNodeWithSize)) {
			llvm::SmallVector<llvm::Value *, 4> args;

			args.push_back(AI.allocInst);
			args.append(AI.inputArguments.begin(), AI.inputArguments.end());

			args.push_back(llvm::ConstantInt::get(
					llvm::IntegerType::get(Mod->getContext(), 32), NodeCount++));

			auto *insertPt = AI.allocInst->getNextNode();
			auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
					args, "", insertPt);

			emittedNodes.insert(AI.allocInst);
			insertPts[AI.allocInst] = call;
		}
	}

	void emitCreateParams(llvm::Instruction &I, int num_nodes_pf,
			int num_edges_pf) {
		llvm::Function *func =
				getFunctionFromInst(I, PrefetcherRuntime::CreateParams);
		llvm::SmallVector<llvm::Value *, 4> args;

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32), num_nodes_pf));

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32), num_edges_pf * 2));

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32), num_edges_pf * 2));

		auto *insertPt = &I;

		auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args,
				"", insertPt);
	}

	void emitCreateEnable(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::CreateEnable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	bool insertIfNotEmitted(std::vector<GEPDepInfo> & emitted_traversal_edges, GEPDepInfo & edge) {
		for (auto & e : emitted_traversal_edges) {
			if (e == edge) {
				return false;
			}
		}

		emitted_traversal_edges.push_back(edge);
		return true;
	}


	void emitRegisterRITravEdge_New(GEPDepInfo &gdi, unsigned int edge_type, std::vector<GEPDepInfo> & emitted_traversal_edges)
	{
		/* Copy load instruction */
		if (insertIfNotEmitted(emitted_traversal_edges,gdi)) {
			/* Emit call to register the traversal edge */
			if (auto *func = Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
				llvm::SmallVector<llvm::Value *, 2> args;

				/* First argument is usually the baseptr operand to the first GEP - i.e. the source node,
				 * however, in this case we can use the operand to the load instruction. I think
				 * this should make the pass more robust. In BFS in particular, the first GEP accesses the index array
				 * as a member of the Graph class, and so the baseptr to the GEP is just a pointer to the class object.
				 * The result of the GEP, which is also the load instruction argument, is the actual pointer we're interested in.*/
				args.push_back(gdi.source);
				args.push_back(gdi.target);

				/* The baseptr of the second GEP is obtained from a load using the resulting address of the first GEP
				 * Since we need this value before the actual load occurs, we take the result of the copied load instruction. */

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), edge_type));

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), edgeCount++));

				/* Insert the edge between the copied load instruction and the actual load instruction */
				auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
						args, "", dyn_cast<llvm::Instruction>(dyn_cast<llvm::Instruction>(gdi.target))->getNextNode());

				emittedTravEdges.insert(gdi);
			}
		}
	}

	llvm::Instruction * getFirstInstruction(const llvm::Function & F, llvm::Instruction * a, llvm::Instruction * b) {
		for (auto &BB : F) {
			for (auto &I : BB) {
				if (&I == a) {
					return a;
				}

				if (&I == b) {
					return b;
				}
			}
		}
		assert(false && "Instruction not in current function!\n");
		return nullptr;
	}

	llvm::Instruction * getSecondInstruction(const llvm::Function & F, llvm::Instruction * a, llvm::Instruction * b) {

		if (getFirstInstruction(F,a,b) == a) {
			return b;
		}
		else if (getFirstInstruction(F,a,b) == b){
			return a;
		}
		assert(false && "Instruction not in current function!\n");
		return nullptr;
	}

	bool needLoadCopy(llvm::Function * f, llvm::Instruction * source_load, llvm::Instruction * target_load) {

		if (target_load->getOpcode() == llvm::Instruction::Load) {
			for (auto & BB : *f) {
				for (auto &I : BB) {
					if (&I == source_load) {
						return true;
					}

					if (&I == target_load) {
						return false;
					}
				}
			}
		}

		return false;
	}

	bool isUsedInPhiInCurrentBB(llvm::BasicBlock * BB, llvm::Instruction * instr, llvm::PHINode *& phi, DominatorTree & DT) {
		errs() << "Check if used in PHI \n";
		for (auto &I : *BB) {
			errs() << "Check " << *instr << " against " << I << "\n";
			if (llvm::PHINode * bb_phi = dyn_cast<llvm::PHINode>(&I)) {
				for (int i = 0; i < bb_phi->getNumIncomingValues(); ++i) {
					llvm::errs() << "PHI: " << *(bb_phi->getIncomingValue(i)) << " " << *(dyn_cast<llvm::Value>(instr)) << "\n";
					if (bb_phi->getIncomingValue(i) == dyn_cast<llvm::Value>(instr)) {
						phi = bb_phi;
						llvm::errs() << "FOUND " << *phi << "\n";
						return true;
					}
				}
			}
			else {
				break;
			}
		}

		return false;
	}

	void emitRegisterTravEdge_New(GEPDepInfo &gdi, std::vector<GEPDepInfo> & emitted_traversal_edges, DominatorTree & DT) {
		if(insertIfNotEmitted(emitted_traversal_edges,gdi)) {
			if (auto *func = Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
				llvm::SmallVector<llvm::Value *, 4> args;

				llvm::Instruction * insertPt = nullptr;

				llvm::PHINode * phi_source;
				llvm::PHINode * phi_target;

				llvm::errs() << "Emit Edge: ";

				llvm::Instruction * instr_src = dyn_cast<llvm::Instruction>(gdi.source);
				llvm::Instruction * instr_target = dyn_cast<llvm::Instruction>(gdi.target);

				if (instr_src) {
					if (!dyn_cast<llvm::PHINode>(instr_src)) {
						if (instr_src->getParent() != instr_target->getParent()) {
							if (isUsedInPhiInCurrentBB(instr_target->getParent(), instr_src, phi_source, DT)) {
								llvm::errs() << "FOUND " << *phi_source << "\n";
								gdi.source = dyn_cast<llvm::Value>(phi_source);
							}
						}
						else {
							if (isUsedInPhiInCurrentBB(instr_src->getParent(), instr_src,phi_source, DT)) {
								gdi.source = dyn_cast<llvm::Value>(phi_source);
							}
						}
					}
				}
				if (llvm::Instruction * instr = dyn_cast<llvm::Instruction>(gdi.target)) {
					if (!dyn_cast<llvm::PHINode>(gdi.target)) {
						if (isUsedInPhiInCurrentBB(instr_target->getParent(), instr_target, phi_target, DT)) {
							gdi.target = dyn_cast<llvm::Value>(phi_target);
						}
					}
				}

				if (gdi.phi) {
					llvm::errs() << __FUNCTION__ << " " << __LINE__ << "\n";
					args.push_back(gdi.phi_node);
					errs() << __LINE__ << " " << *(gdi.phi_node);
					insertPt = gdi.phi_node->getParent()->getFirstNonPHIOrDbgOrLifetime();
				}
				else if (dyn_cast<llvm::PHINode>(gdi.target)){
					llvm::errs() << __FUNCTION__ << " " << __LINE__ << "\n";
					args.push_back(gdi.source);
					errs() << " " << *(gdi.source);
					insertPt = dyn_cast<llvm::Instruction>(gdi.target)->getParent()->getFirstNonPHIOrDbgOrLifetime();
				}
				else if (dyn_cast<llvm::PHINode>(gdi.source)){
					llvm::errs() << __FUNCTION__ << " " << __LINE__ << "\n";
					args.push_back(gdi.source);
					errs() << " " << *(gdi.source);
					insertPt = dyn_cast<llvm::Instruction>(gdi.source)->getParent()->getFirstNonPHIOrDbgOrLifetime();
				}
				else {
					llvm::errs() << __FUNCTION__ << " " << __LINE__ << "\n";
					args.push_back(gdi.source);
					errs() << " " << *(gdi.source);
				}

				if (dyn_cast<llvm::Instruction>(gdi.target)) {
					if (needLoadCopy(gdi.funcSource, gdi.source_use, cast<llvm::Instruction>(gdi.target))) {
						llvm::Instruction * load_to_copy = (llvm::Instruction*)(gdi.target);
						llvm::LoadInst * load_instr = new llvm::LoadInst(load_to_copy->getType(), load_to_copy->getOperand(0));

						BasicBlock *B = ((llvm::Instruction*)(gdi.source_use))->getParent();
						B->getInstList().insert(gdi.source_use->getIterator(), load_instr);

						args.push_back(load_instr);
						errs() << " " << *load_instr << "\n";
						insertPt = load_instr->getNextNode();
					}
					else {
						args.push_back(gdi.target);
						errs() << " " << *(gdi.target) << "\n";
					}
				}
				else {
					args.push_back(gdi.target);
					errs() << " " << *(gdi.target) << "\n";
				}

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), BaseOffset_int32_t));

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), edgeCount++));

				if (!insertPt) { // If insertion point hasn't been decided by Phi Node
					if (llvm::dyn_cast<llvm::GlobalValue>(gdi.source) && llvm::dyn_cast<llvm::GlobalValue>(gdi.target)) {
						insertPt = gdi.funcSource->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
					}
					else if (llvm::dyn_cast<llvm::GlobalValue>(gdi.source) && !llvm::dyn_cast<llvm::GlobalValue>(gdi.target)) {
						if (llvm::dyn_cast<llvm::Argument>(gdi.target)) {
							insertPt = gdi.funcSource->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
						}
						else {
							insertPt = dyn_cast<llvm::Instruction>(gdi.target)->getNextNode();
						}
					}
					else if (!llvm::dyn_cast<llvm::GlobalValue>(gdi.source) && llvm::dyn_cast<llvm::GlobalValue>(gdi.target)) {
						if (llvm::dyn_cast<llvm::Argument>(gdi.source)) {
							insertPt = gdi.funcSource->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
						}
						else {
							insertPt = dyn_cast<llvm::Instruction>(gdi.source)->getNextNode();
						}
					}
					else if (llvm::dyn_cast<llvm::Argument>(gdi.source) && llvm::dyn_cast<llvm::Argument>(gdi.target)) {
						insertPt = gdi.funcSource->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
					}
					else if (llvm::dyn_cast<llvm::Argument>(gdi.source) && !llvm::dyn_cast<llvm::Argument>(gdi.target)) {
						insertPt = dyn_cast<llvm::Instruction>(gdi.target)->getNextNode();
					}
					else if (!llvm::dyn_cast<llvm::Argument>(gdi.source) && llvm::dyn_cast<llvm::Argument>(gdi.target)) {
						insertPt = dyn_cast<llvm::Instruction>(gdi.source)->getNextNode();
					}
					else {
						insertPt = getSecondInstruction(*(gdi.funcSource),dyn_cast<llvm::Instruction>(gdi.source),dyn_cast<llvm::Instruction>(gdi.target))->getNextNode();
					}
				}

				auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
						args, "", insertPt);

				emittedTravEdges.insert(gdi);
			}
		}
	}

	// If a node is a source but not a target, then it is a trigger node.
	void emitRegisterTrigEdge(llvm::SmallVectorImpl<GEPDepInfo> &geps, llvm::SmallVectorImpl<GEPDepInfo> &ri_geps) {

		std::vector<GEPDepInfo> all_geps;
		for (auto &gdi : geps) {
			all_geps.push_back(gdi);
		}

		for (auto &gdi : ri_geps) {
			all_geps.push_back(gdi);
		}

		for (auto &gdi : all_geps) {
			bool trigger_node = true;
			for (auto &gdi2 : all_geps) {
				if (gdi.source == gdi2.target) {
					trigger_node = false;
				}
			}

			if (trigger_node) {

				if(emittedTrigEdges.count(gdi.source) == 0 && emittedNodes.count(gdi.source)) {

					if (auto *func =
							Mod->getFunction(PrefetcherRuntime::RegisterTrigEdge1)) {
						llvm::SmallVector<llvm::Value *, 4> args;
						args.push_back(gdi.source);
						args.push_back(gdi.source);

						args.push_back(llvm::ConstantInt::get(
								llvm::IntegerType::get(Mod->getContext(), 32), UpToOffset));

						args.push_back(llvm::ConstantInt::get(
								llvm::IntegerType::get(Mod->getContext(), 32), NeverSquash));

						auto *insertPt = insertPts[gdi.source];
						auto *call =
								llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
										insertPt->getNextNode());

						TriggerEdgeCount++;
						emittedTrigEdges.insert(gdi.source);
					}
				}
			}
		}
	}
};

class PrefetcherCodegenPass : public llvm::ModulePass {
public:
	static char ID;

	PrefetcherCodegenPass() : llvm::ModulePass(ID) {}
	bool runOnModule(llvm::Module &CurMod) override;
	void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
	unsigned edgeCount = 0;
};

//

char PrefetcherCodegenPass::ID = 0;
static llvm::RegisterPass<PrefetcherCodegenPass>
Y("prefetcher-codegen", "Prefetcher Codegen Pass", false, true);

static void
registerPrefetcherCodegenPass(const llvm::PassManagerBuilder &Builder,
		llvm::legacy::PassManagerBase &PM) {
	PM.add(new PrefetcherCodegenPass());

	return;
}

static llvm::RegisterStandardPasses
RegisterPrefetcherCodegenPass(llvm::PassManagerBuilder::EP_EarlyAsPossible,
		registerPrefetcherCodegenPass);

static bool shouldSkip(llvm::Function &CurFunc) {
	if (CurFunc.isIntrinsic() || CurFunc.empty()) {
		llvm::errs() << "func is instrinsic or empty\n";
		return true;
	}

	auto found = std::find(PrefetcherRuntime::Functions.begin(),
			PrefetcherRuntime::Functions.end(), CurFunc.getName());
	if (found != PrefetcherRuntime::Functions.end()) {
		llvm::errs() << "func is in runtime\n";
		return true;
	}

	return false;
}

bool PrefetcherCodegenPass::runOnModule(llvm::Module &CurMod) {

	bool hasModuleChanged = false;

	PrefetcherCodegen pfcg(CurMod);
	pfcg.declareRuntime();

	unsigned totalNodesNum = 0;
	unsigned totalEdgesNum = 0;

	std::vector<GEPDepInfo> emitted_traversal_edges;


	for (llvm::Function &curFunc : CurMod) {
		if (shouldSkip(curFunc)) {
			llvm::errs() << "skipping func: "
					<< curFunc.getName() << '\n';
			continue;
		}

		llvm::errs() << "processing func: "
				<< curFunc.getName() << '\n';

		PrefetcherAnalysisResult * pfa =
				this->getAnalysis<PrefetcherPass>(curFunc).getPFA();

		DominatorTree &DT = this->getAnalysis<DominatorTreeWrapperPass>(curFunc).getDomTree();


		for (auto &ai : pfa->allocs) {
			if (ai.allocInst) {
				pfcg.emitRegisterNode(ai);
				totalNodesNum++;
			}
		}

		pfcg.emitRegisterTrigEdge(pfa->geps, pfa->ri_geps);

		for (GEPDepInfo & gdi : pfa->ri_geps) {
			pfcg.emitRegisterRITravEdge_New(gdi, PointerBounds_uint64_t, emitted_traversal_edges);
			totalEdgesNum++;
		}

		for (GEPDepInfo & gdi : pfa->geps) {
			pfcg.emitRegisterTravEdge_New(gdi, emitted_traversal_edges, DT);
			totalEdgesNum++;
		}
	}

	if (auto *mainFn = CurMod.getFunction("main")) {
		llvm::BasicBlock &bb = mainFn->getEntryBlock();
		llvm::Instruction *I = bb.getFirstNonPHIOrDbg();

		pfcg.emitCreateParams(*I, (int)totalNodesNum, emitted_traversal_edges.size());
		pfcg.emitCreateEnable(*I);
	}

	return hasModuleChanged;

}
void PrefetcherCodegenPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
	AU.addRequiredTransitive<PrefetcherPass>();
	AU.addRequired<LoopInfoWrapperPass>();
	AU.addRequired<DominatorTreeWrapperPass>();
	AU.setPreservesCFG();

	return;
}

} // namespace
