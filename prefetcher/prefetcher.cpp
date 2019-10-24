#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/DependenceAnalysis.h"

#include <vector>

using namespace llvm;

namespace {


struct Prefetcher_Module : public ModulePass {
	static char ID;
	Prefetcher_Module() :ModulePass(ID) {}

	bool runOnModule(Module & M) override {

		return false;
	}
};
}

char Prefetcher_Module::ID = 0; // Initialization value not important

static RegisterPass<Prefetcher_Module> Y("prefetcher_module", "Module Prefetcher Pass",
		false, /* Only looks at CFG */
		true /* Analysis Pass */);


namespace {
struct Prefetcher : public FunctionPass {
	static char ID;
	Prefetcher() : FunctionPass(ID) {}

	virtual void getAnalysisUsage(AnalysisUsage& AU) const override {
		//AU.addRequired<LoopInfoWrapperPass>();
		AU.addRequired<MemorySSAWrapperPass>();
		AU.addRequired<DependenceAnalysisWrapperPass>();
	}

	bool printAll(Function &F) {
		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				errs() << I << '\n'; // I.getOpcodeName()
			}
		}

		return false;
	}

	bool identifyGEP(Function &F, DependenceInfo & DI) {

		bool trace = false;

		std::vector<llvm::Instruction*> insns;

		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				if (I.getOpcode() == Instruction::GetElementPtr) {
					errs() << I << '\n';
					insns.push_back(&I);
				}
			}
		}

		if (insns.size() > 0) {

			for (int i = 0; i < insns.size() -1; ++i) {
				if (DI.depends(insns.at(i), insns.at(i+1), true) ||
						DI.depends(insns.at(i+1), insns.at(i), true)) {
					errs() << "GEP DEPENDENCE!" << "\n";
				}
				else {
					errs() << "NO GEP DEPENDENCE!\n" << "\n";
				}

				// identify if GEP depends on another GEP
			}
		}
		return false;
	}

	bool identifyMemoryAllocations(Function & F) {

		bool malloc_present = false;

		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				CallSite CS(&I);
				if (!CS.getInstruction()) {
					continue;
				}
				Value *called = CS.getCalledValue()->stripPointerCasts();

				if (llvm::Function *f = dyn_cast<Function>(called)) {
					if (f->getName().equals("calloc")) {
						malloc_present = true;
						errs() << "  Insert code to write to Node table: resulting ptr of calloc, num elements: << *(I.getOperand(0)), size of element: << *(I.getOperand(1)) \n";
					}
					else if (f->getName().equals("malloc")) {
						malloc_present = true;
						errs() << "  Insert code to write to Node table: resulting ptr of malloc, size: << *(I.getOperand(0)), size of element: might be able to get this using LLVM type information, otherwise speculate - 4 bytes? \n";
					}
				}
				// Check for other allocation functions - C++ new, etc.
			}
		}

		if (malloc_present == true) {
			errs() << "  Select and emit trigger node\n";
		}

		return false;
	}

	bool runOnFunction(Function &F) override {

		errs() << "\n" << F.getName() << "\n";

		//LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		//		MemorySSA &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
		DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>().getDI();

		//MSSA.print(errs());

		//		errs() << "Prefetcher: ";
		//		errs().write_escaped(F.getName()) << '\n';

		identifyGEP(F, DI);
		identifyMemoryAllocations(F);
		//		printAll(F);

		return false;
	}
};
}

char Prefetcher::ID = 0; // Initialization value not important

static RegisterPass<Prefetcher> X("prefetcher", "Prefetcher Pass",
		false, /* Only looks at CFG */
		true /* Analysis Pass */);
