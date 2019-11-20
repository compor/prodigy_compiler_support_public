//
//
//

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

#include "llvm/Transforms/IPO/PassManagerBuilder.h"
// using llvm::PassManagerBuilder
// using llvm::RegisterStandardPasses

#include "llvm/ADT/SmallVector.h"
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

struct PrefetcherRuntime {
	static constexpr char *CreateParams = "create_params";
	static constexpr char *CreateEnable = "create_enable";
	static constexpr char *RegisterNode = "register_node";
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

enum FuncId
{
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
	unsigned long NodeCount;

public:
	PrefetcherCodegen(llvm::Module &M) : Mod(&M), NodeCount(0){};

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

		// TODO: add size of elems, compute number of elements.
		if (auto *func =
				Mod->getFunction(PrefetcherRuntime::RegisterNodeWithSize)) {
			llvm::SmallVector<llvm::Value *, 4> args;

			args.push_back(AI.allocInst);
			args.append(AI.inputArguments.begin(), AI.inputArguments.end());

			args.push_back(llvm::ConstantInt::get(
					llvm::IntegerType::get(Mod->getContext(), 32), NodeCount++));

			auto *insertPt = AI.allocInst->getParent()->getTerminator();
			auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
					args, "", insertPt);
		}
	}

	void emitCreateParams(llvm::Instruction &I, int num_nodes_pf, int num_edges_pf, int num_triggers_pf) {
		llvm::Function * func = getFunctionFromInst(I, PrefetcherRuntime::CreateParams);
		llvm::SmallVector<llvm::Value *, 4> args;

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32),num_nodes_pf));

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32),num_edges_pf));

		args.push_back(llvm::ConstantInt::get(
				llvm::IntegerType::get(Mod->getContext(), 32),num_triggers_pf));

		auto *insertPt = &I;

		auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
				args, "", insertPt);
	}

	void emitCreateEnable(llvm::Instruction &I) {
		llvm::Function *F = getFunctionFromInst(I, PrefetcherRuntime::CreateEnable);
		llvm::IRBuilder<> Builder(&I);
		Builder.CreateCall(F);
	}

	void emitRegisterTravEdge(GEPDepInfo & gdi)
	{
		if (auto *func =
				Mod->getFunction(PrefetcherRuntime::RegisterTravEdge1)) {
			llvm::SmallVector<llvm::Value *, 4> args;

			args.push_back(gdi.source);
			args.push_back(gdi.target->getOperand(0));

			args.push_back(llvm::ConstantInt::get(
					llvm::IntegerType::get(Mod->getContext(), 32),BaseOffset_int32_t));

			auto *insertPt = gdi.source;

			llvm::errs() << "Emitting!\n";

			llvm::errs() << *(gdi.source) << "\n";
			llvm::errs() << *(gdi.target) << "\n";

			llvm::errs() << "Done Emitting!\n";

			auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
					args, "", insertPt->getNextNode());
		}
	}

	// If a node is a source but not a target, then it is a trigger node.
	void emitRegisterTrigEdge(llvm::SmallVector<GEPDepInfo,8> &geps)
	{
		GEPDepInfo * current_trigger;

		for (auto &gdi : geps) {
			bool trigger_node = true;
			for (auto &gdi2 : geps) {
				if (gdi.source == gdi2.target) {
					trigger_node = false;
				}
			}

			if (trigger_node) {
				if (auto *func =
						Mod->getFunction(PrefetcherRuntime::RegisterTrigEdge1)) {
					llvm::SmallVector<llvm::Value *, 4> args;
					args.push_back(gdi.source);
					args.push_back(gdi.source);

					args.push_back(llvm::ConstantInt::get(
							llvm::IntegerType::get(Mod->getContext(), 32),UpToOffset));

					args.push_back(llvm::ConstantInt::get(
							llvm::IntegerType::get(Mod->getContext(), 32),NeverSquash));

					auto *insertPt = gdi.source;
					auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),
							args, "", insertPt->getNextNode());
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
Y("prefetcher-codegen", "Prefetcher Codegen Pass", false, false);

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

	return false;
}

//

bool PrefetcherCodegenPass::runOnModule(llvm::Module &CurMod) {
	bool hasModuleChanged = false;

	PrefetcherCodegen pfcg(CurMod);
	pfcg.declareRuntime();

	for (llvm::Function &curFunc : CurMod) {

		if (shouldSkip(curFunc)) {
			DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs() << "skipping func: "
					<< curFunc.getName() << '\n';);
			continue;
		}


		DEBUG_WITH_TYPE(DEBUG_TYPE, llvm::dbgs() << "processing func: "
				<< curFunc.getName() << '\n';);

		PrefetcherAnalysisResult &pfa = this->getAnalysis<PrefetcherPass>(curFunc).getPFA();

		if (curFunc.getName() == "main") {
			llvm::BasicBlock &bb = curFunc.getEntryBlock();
			llvm::Instruction * I = bb.getFirstNonPHIOrDbg();

//			TODO: One more pass for this to get the number of trigger edges?
			pfcg.emitCreateParams(*I, (int)(pfa.allocs.size()), (int)(pfa.geps.size()), 4); // the 4 here is random.
			pfcg.emitCreateEnable(*I);
		}

		for (auto &ai : pfa.allocs) {
			if (ai.allocInst) {
				pfcg.emitRegisterNode(ai);
			}
		}

		for (GEPDepInfo &gdi : pfa.geps) {
			pfcg.emitRegisterTravEdge(gdi);
		}

		pfcg.emitRegisterTrigEdge(pfa.geps);
	}

	return hasModuleChanged;
}

void PrefetcherCodegenPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
	AU.addRequired<PrefetcherPass>();
	AU.setPreservesCFG();

	return;
}

} // namespace
