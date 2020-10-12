//
//
//

#define DEBUG 0

#include "llvm/Pass.h"
// using llvm::RegisterPass

#include "llvm/IR/Type.h"
// using llvm::Type

#include "llvm/IR/DerivedTypes.h"
// using llvm::IntegerType

#include "llvm/IR/Instruction.h"
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

#include "prefetcher.hpp"

#define DEBUG_TYPE "prefetcher-codegen"

// plugin registration for opt

namespace {

llvm::Loop *getTopLevelLoop(llvm::Loop *CurLoop) {
	auto *loop = CurLoop;

	while (loop && loop->getParentLoop()) {
		loop = loop->getParentLoop();
	}

	return loop;
}

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



	llvm::Instruction *findInsertionPointBeforeLoopNest(llvm::Instruction &I) {
		llvm::Loop *loop = nullptr;
		if (LI && (loop = getTopLevelLoop(LI->getLoopFor(I.getParent())))) {
			auto *ph = loop->getLoopPreheader();
			return ph ? loop->getLoopPreheader()->getTerminator() : nullptr;
		}

		return nullptr;
	}

public:
	llvm::SmallPtrSet<llvm::Value *, 4> emittedNodes;
	llvm::SmallSet<struct GEPDepInfo, 4> emittedTravEdges;
	llvm::SmallPtrSet<llvm::Value *, 4> emittedTrigEdges;
	std::map<llvm::Value *, llvm::Instruction *> insertPts;

	PrefetcherCodegen(llvm::Module &M)
	: Mod(&M), LI(nullptr), NodeCount(0), TriggerEdgeCount(0){};

	void setLoopInfo(llvm::LoopInfo &LI_) { LI = &LI_; }

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

			emitSimUserPFSetParam(*(call->getNextNode()));
			emitSimUserPFSetEnable(*(call->getNextNode()));
			//

			emittedNodes.insert(AI.allocInst);
			insertPts[AI.allocInst] = call;
#if DEBUG == 1
			llvm::errs() << "ALLOC INSTR: "<< *(AI.allocInst) << "\n";
#endif
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
				llvm::IntegerType::get(Mod->getContext(), 32), num_edges_pf));

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32), TriggerEdgeCount));

		auto *insertPt = &I;

		auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args,
				"", insertPt);
	}

	void emitCreateEnable(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::CreateEnable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitRegisterRITravEdge(GEPDepInfo &gdi)
	{
		llvm::SmallVector<llvm::Value *, 1> args;
		args.push_back(gdi.source);
		//		  LoadInst(Type *Ty, Value *Ptr, const Twine &NameStr = "",
		//		           Instruction *InsertBefore = nullptr);

		BasicBlock *B = gdi.load_to_copy->getParent();
		auto *load_instr = gdi.load_to_copy->clone();
		B->getInstList().insert(gdi.load_to_copy->getIterator(), load_instr);
		load_instr->setName(gdi.load_to_copy->getName());

//		auto *load_instr = new llvm::LoadInst(gdi.load_to_copy->getType(), gdi.load_to_copy->getOperand(0),"load_copy",llvm::dyn_cast<llvm::Instruction>(gdi.source)->getNextNode());

		if (auto *func = Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
			llvm::SmallVector<llvm::Value *, 2> args;

			args.push_back(gdi.source);
			args.push_back(load_instr);

			args.push_back(llvm::ConstantInt::get(
					llvm::IntegerType::get(Mod->getContext(), 32), PointerBounds_int32_t));

#if DEBUG == 1
			llvm::errs() << "emitRegisterTravEdge!\n";

			llvm::errs() << *(gdi.source) << "\n";
			llvm::errs() << *(load_instr) << "\n";

			llvm::errs() << "Done RI emitRegisterTravEdge!\n";
#endif

			auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
					args, "", load_instr->getNextNode());

			//      emitSimUserPFSetParam(*(call->getNextNode()));
			//      emitSimUserPFSetEnable(*(call->getNextNode()));

			emittedTravEdges.insert(gdi);
		}
	}

	void emitRegisterTravEdge(GEPDepInfo &gdi) {
		if(emittedTravEdges.count(gdi) == 0 && emittedNodes.count(gdi.source) && emittedNodes.count(gdi.target)) {
			if (auto *func = Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
				llvm::SmallVector<llvm::Value *, 4> args;

				args.push_back(gdi.source);
				args.push_back(gdi.target);

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), BaseOffset_int32_t));

				auto *insertPt = insertPts[gdi.target];

#if DEBUG == 1
				llvm::errs() << "emitRegisterTravEdge!\n";

				llvm::errs() << *(gdi.source) << "\n";
				llvm::errs() << *(gdi.target) << "\n";

				llvm::errs() << "Done emitRegisterTravEdge!\n";
#endif

				auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
						args, "", insertPt->getNextNode());

				//      emitSimUserPFSetParam(*(call->getNextNode()));
				//      emitSimUserPFSetEnable(*(call->getNextNode()));

				emittedTravEdges.insert(gdi);
			}
		}
	}

	void emitRegisterTravEdge2(GEPDepInfo &gdi, llvm::Instruction *InsertPt) {
		if (auto *func = Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
			llvm::SmallVector<llvm::Value *, 4> args;

			args.push_back(gdi.source);
			args.push_back(gdi.target);

			args.push_back(llvm::ConstantInt::get(
					llvm::IntegerType::get(Mod->getContext(), 32), BaseOffset_int32_t));

#if DEBUG == 1
			llvm::errs() << "emitRegisterTravEdge2!\n";

			llvm::errs() << *(gdi.source) << "\n";
			llvm::errs() << *(gdi.target) << "\n";

			llvm::errs() << "Done emitRegisterTravEdge2!\n";
#endif



			auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
					args, "", InsertPt);

			//      emitSimUserPFSetParam(*(call->getNextNode()));
			//      emitSimUserPFSetEnable(*(call->getNextNode()));

			emittedTravEdges.insert(gdi);
		}
	}

	void emitRegisterIdentifyEdge(llvm::SmallVector<GEPDepInfo,8> &geps) {

		int edge_id_counter = 0;

		for (auto &gdi : geps) {
			// Emit Source
			if (auto *func =
					Mod->getFunction(PrefetcherRuntime::RegisterIdentifyEdgeSource)) {
				llvm::SmallVector<llvm::Value *, 2> args;
				args.push_back(gdi.source);

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), edge_id_counter));

				auto *insertPt = insertPts[gdi.source];

				auto *call =
						llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
								gdi.funcSource->getEntryBlock().getFirstNonPHIOrDbgOrLifetime());
			}

			// Emit Target
			if (auto *func =
					Mod->getFunction(PrefetcherRuntime::RegisterIdentifyEdgeTarget)) {
				llvm::SmallVector<llvm::Value *, 2> args;
				args.push_back(gdi.target);

				args.push_back(llvm::ConstantInt::get(
						llvm::IntegerType::get(Mod->getContext(), 32), edge_id_counter));

				auto *insertPt = insertPts[gdi.target];
				auto *call =
						llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
								gdi.funcTarget->getEntryBlock().getFirstNonPHIOrDbgOrLifetime());
			}

			edge_id_counter++;
		}
	}

	// If a node is a source but not a target, then it is a trigger node.
	void emitRegisterTrigEdge(llvm::SmallVector<GEPDepInfo, 8> &geps) {

		for (auto &gdi : geps) {
			bool trigger_node = true;
			for (auto &gdi2 : geps) {
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

#if DEBUG == 1
						llvm::errs() << "INSERT PT INSTR: "<< *(gdi.source) << "\n";
						llvm::errs() << "INSERT PT: "<< *(insertPts[gdi.source]) << "\n";
#endif
						auto *insertPt = insertPts[gdi.source];
						auto *call =
								llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
										insertPt->getNextNode());

						TriggerEdgeCount++;
						//          emitSimUserPFSetParam(*(call->getNextNode()));
						//          emitSimUserPFSetEnable(*(call->getNextNode()));

						emittedTrigEdges.insert(gdi.source);
					}
				}
			}
		}
	}

	void emitRegisterTrigEdge2(llvm::SmallVector<GEPDepInfo, 8> &geps, llvm::Function &CurFunc) {
		GEPDepInfo *current_trigger;

		for (auto &gdi : geps) {
			bool trigger_node = true;
			for (auto &gdi2 : geps) {
				if (gdi.source == gdi2.target) {
					trigger_node = false;
				}
			}

			if (trigger_node) {

				if(!emittedTrigEdges.count(gdi.source)) {

					if (auto *func =
							Mod->getFunction(PrefetcherRuntime::RegisterTrigEdge1)) {
						llvm::SmallVector<llvm::Value *, 4> args;
						args.push_back(gdi.source);
						args.push_back(gdi.source);

						args.push_back(llvm::ConstantInt::get(
								llvm::IntegerType::get(Mod->getContext(), 32), UpToOffset));

						args.push_back(llvm::ConstantInt::get(
								llvm::IntegerType::get(Mod->getContext(), 32), NeverSquash));

						if (llvm::dyn_cast<llvm::Argument>(gdi.source)) {
							auto *insertPt = CurFunc.getEntryBlock().getFirstNonPHIOrDbgOrLifetime();

#if DEBUG == 1
							llvm::errs() << "INSERT PT2: "<< *insertPt << "\n";
#endif

							auto *call =
									llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
											insertPt->getNextNode());
						}
						else {
							auto *insertPt = llvm::dyn_cast<llvm::Instruction>(gdi.source)->getNextNode();
#if DEBUG == 1
							llvm::errs() << "INSERT PT2: "<< *insertPt << "\n";
#endif

							auto *call =
									llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "",
											insertPt->getNextNode());
						}

						TriggerEdgeCount++;
						//          emitSimUserPFSetParam(*(call->getNextNode()));
						//          emitSimUserPFSetEnable(*(call->getNextNode()));

						emittedTrigEdges.insert(gdi.source);
					}
				}
			}
		}
	}

	void emitSimUserPFSetParam(llvm::Instruction &I) {
		llvm::Function *F =
				getFunctionFromInst(I, PrefetcherRuntime::SimUserPfSetParam);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimUserPFSetEnable(llvm::Instruction &I) {
		llvm::Function *F =
				getFunctionFromInst(I, PrefetcherRuntime::SimUserPfSetEnable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimUserPFEnable(llvm::Instruction &I) {
		llvm::Function *F =
				getFunctionFromInst(I, PrefetcherRuntime::SimUserPfEnable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimUserWait(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::SimUserWait);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimRoiStart(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::SimRoiStart);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimRoiEnd(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::SimRoiEnd);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitSimUserPFDisable(llvm::Instruction &I) {
		llvm::Function *F =
				getFunctionFromInst(I, PrefetcherRuntime::SimUserPfDisable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}
};

//

class PrefetcherCodegenPass : public llvm::ModulePass {
public:
	static char ID;

	PrefetcherCodegenPass() : llvm::ModulePass(ID) {}
	bool runOnModule(llvm::Module &CurMod) override;
	void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
};

//

char PrefetcherCodegenPass::ID = 0;
static llvm::RegisterPass<PrefetcherCodegenPass>
Y("prefetcher-codegen", "Prefetcher Codegen Pass", false, true);

// plugin registration for clang

// the solution was at the bottom of the header file
// 'llvm/Transforms/IPO/PassManagerBuilder.h'
// create a static free-floating callback that uses the legacy pass manager to
// add an instance of this pass and a static instance of the
// RegisterStandardPasses class

static void
registerPrefetcherCodegenPass(const llvm::PassManagerBuilder &Builder,
		llvm::legacy::PassManagerBase &PM) {
	PM.add(new PrefetcherCodegenPass());

	return;
}

static llvm::RegisterStandardPasses
RegisterPrefetcherCodegenPass(llvm::PassManagerBuilder::EP_EarlyAsPossible,
		registerPrefetcherCodegenPass);

//

static bool shouldSkip(llvm::Function &CurFunc) {
	if (CurFunc.isIntrinsic() || CurFunc.empty()) {
		return true;
	}

	auto found = std::find(PrefetcherRuntime::Functions.begin(),
			PrefetcherRuntime::Functions.end(), CurFunc.getName());
	if (found != PrefetcherRuntime::Functions.end()) {
		return true;
	}
	if (CurFunc.getName().equals("myIntMallocFn32") ||
			CurFunc.getName().equals("myIntMallocFn64")) {
		return true;
	}

#if DEBUG == 1
	llvm::outs() << "NAME:\n";
	llvm::outs() << CurFunc.getName();
#endif
	return false;
}

//

bool PrefetcherCodegenPass::runOnModule(llvm::Module &CurMod) {
	bool hasModuleChanged = false;

	PrefetcherCodegen pfcg(CurMod);
	pfcg.declareRuntime();

	unsigned totalNodesNum = 0;
	unsigned totalEdgesNum = 0;

	for (llvm::Function &curFunc : CurMod) {
		if (shouldSkip(curFunc)) {
			DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs() << "skipping func: "
					<< curFunc.getName() << '\n';);
			continue;
		}

		DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs() << "processing func: "
				<< curFunc.getName() << '\n';);

		PrefetcherAnalysisResult * pfa =
				this->getAnalysis<PrefetcherPass>(curFunc).getPFA();


		for (auto &ai : pfa->allocs) {
			if (ai.allocInst) {
				pfcg.emitRegisterNode(ai);

				for (GEPDepInfo &gdi : pfa->geps) {
					pfcg.emitRegisterTravEdge(gdi);
					totalEdgesNum++;
				}
			}
			totalNodesNum++;
		}

		//		pfcg.emitRegisterIdentifyEdge(pfa->geps);
		pfcg.emitRegisterTrigEdge(pfa->geps);

		for (GEPDepInfo & gdi : pfa->geps) {
			if(pfcg.emittedTravEdges.count(gdi) == 0) {
				// Check if both source and target are arguments (otherwise if not, then they are defined in the same function,
				// and should have been caught by previous call to emitRegisterTravEdge)
				if (llvm::dyn_cast<llvm::Argument>(gdi.source) && llvm::dyn_cast<llvm::Argument>(gdi.target)) {
					// If edge is in a different function than allocation, emit using emitRegisterTravEdge2
					pfcg.emitRegisterTravEdge2(gdi, curFunc.getEntryBlock().getFirstNonPHIOrDbgOrLifetime());
				}
				else if (llvm::dyn_cast<llvm::Argument>(gdi.source) && !llvm::dyn_cast<llvm::Argument>(gdi.target)) {
					pfcg.emitRegisterTravEdge2(gdi, llvm::dyn_cast<llvm::Instruction>(gdi.target)->getNextNode());
				}
				else if (!llvm::dyn_cast<llvm::Argument>(gdi.source) && llvm::dyn_cast<llvm::Argument>(gdi.target)) {
					pfcg.emitRegisterTravEdge2(gdi, llvm::dyn_cast<llvm::Instruction>(gdi.source)->getNextNode());
				}
				else {
					pfcg.emitRegisterTravEdge2(gdi, llvm::dyn_cast<llvm::Instruction>(gdi.target)->getNextNode());
					//					assert(false);
				}
			}
		}

		pfcg.emitRegisterTrigEdge2(pfa->geps, curFunc);

		for (GEPDepInfo & gdi : pfa->ri_geps) {
			pfcg.emitRegisterRITravEdge(gdi);
		}

		if (auto *mainFn = CurMod.getFunction("main")) {
			llvm::BasicBlock &bb = mainFn->getEntryBlock();
			llvm::Instruction *I = bb.getFirstNonPHIOrDbg();

			pfcg.emitCreateParams(*I, (int)totalNodesNum, (int)totalEdgesNum);
			pfcg.emitCreateEnable(*I);
		}
	}
	return hasModuleChanged;

}
void PrefetcherCodegenPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
	AU.addRequiredTransitive<PrefetcherPass>();
	AU.addRequired<LoopInfoWrapperPass>();
	//	AU.addRequiredTransitive<SinValIndirectionPass>();
	//	AU.addRequiredTransitive<RangedIndirectionPass>();
	AU.setPreservesCFG();

	return;
}

} // namespace
