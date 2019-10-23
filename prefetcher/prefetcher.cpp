#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"

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
	}

	bool printAll(Function &F) {
		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				errs() << I << '\n'; // I.getOpcodeName()
			}
		}

		return false;
	}

	bool identifyGEP(Function &F) {

		bool trace = false;

		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				if (I.getOpcode() == Instruction::GetElementPtr) {
					trace = true;
					errs() << I << '\n';
					//					errs() << I.getOpcodeName() << " : "
					//							<< I.getOperand(0) << " : "
					//							<< I.getOperand(1) << "\n\n";
				}
				else if (trace) {
					errs() << I
							<< " " << I.getNumOperands() << '\n';
				}

				if (I.getOpcode() == Instruction::Load) {

					if (trace) {
						errs() << "\n";
					}

					trace = false;
				}
			}
		}

		return false;
	}

	bool identifyMemoryAllocations(Function & F) {
		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				CallSite CS(&I);
				if (!CS.getInstruction()) {
					continue;
				}
				Value *called = CS.getCalledValue()->stripPointerCasts();
				if (llvm::Function *f = dyn_cast<Function>(called)) {
					if (f->getName().equals("calloc")) {
						errs() << F.getName() << " -> " << f->getName() << ", elements: " << *(I.getOperand(0)) << ", size: " << *(I.getOperand(1)) << "\n";
					}
					else if (f->getName().equals("malloc")) {
						errs() << F.getName() << " -> " << f->getName() << ", size: " << *(I.getOperand(0)) << "\n";
					}
				}
			}
		}

		return false;
	}

	bool runOnFunction(Function &F) override {

		//LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		MemorySSA &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
		MSSA.print(errs());

		//		errs() << "Prefetcher: ";
		//		errs().write_escaped(F.getName()) << '\n';

//		identifyGEP(F);
//		identifyMemoryAllocations(F);
		//		printAll(F);

		return false;
	}
};
}

char Prefetcher::ID = 0; // Initialization value not important

static RegisterPass<Prefetcher> X("prefetcher", "Prefetcher Pass",
		false, /* Only looks at CFG */
		true /* Analysis Pass */);
