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

#include <cxxabi.h>

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


inline std::string demangle(const char* name)
{
	int status = -1;

	std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
	return (status == 0) ? res.get() : std::string(name);
}

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

	bool usedInLoad(llvm::Instruction *I) {
		for (auto &u: I->uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if (user->getOpcode() == Instruction::Load) {
				return true;
			}
		}

		return false;
	}

	bool recurseUsesSilent(llvm::Instruction &I) {
		bool ret = false;

		for (auto &u: I.uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if (user->getOpcode() == Instruction::GetElementPtr) {
				return true;
			}

			ret = recurseUsesSilent(*user);
		}

		return ret;
	}
	
    llvm::Instruction* 
    recurseUsesSilent(llvm::Instruction &I, bool returnUser) {

		for (auto &u: I.uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if(user->getOpcode() == Instruction::GetElementPtr) {
                return user;
			}

			llvm::Instruction* userInst = recurseUsesSilent(*user, true);
            if(userInst != nullptr) {
                return userInst;
            }
		}

		return nullptr;
	}

	bool recurseUses(llvm::Instruction &I) {
		for (auto &u: I.uses()) {
			auto *user = llvm::dyn_cast<llvm::Instruction>(u.getUser());

			if (user->getOpcode() == Instruction::GetElementPtr) {
				errs() << *user << "\n";
			}

			recurseUses(*user);
		}

		return false;
	}

	bool identifyGEPDependence(Function &F, DependenceInfo & DI) {

		std::vector<llvm::Instruction*> insns;
		std::vector<llvm::Instruction*> loads;

		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				if (I.getOpcode() == Instruction::GetElementPtr) {
					insns.push_back(&I);
				}

				if (I.getOpcode() == Instruction::Load) {
					loads.push_back(&I);
				}
			}
		}

		if (insns.size() > 0) {
			for (auto I : insns) {
				if (I->getOpcode() == llvm::Instruction::GetElementPtr) {
                    // usedInLoad()        finds if a GEP instruction is used in load
                    // recurseUsesSilent() finds if the GEP instruction is 
                    //                     *eventually* used in another GEP instruction
                    // can detect loads of type A[B[i]] 
                    // and does not detect stores of type A[B[i]] 
					if(usedInLoad(I) 
                       && recurseUsesSilent(*I)  
                       && recurseUsesSilent(*I, true) != nullptr  
                       && usedInLoad(recurseUsesSilent(*I, true))) { 
						    errs() << "\n" << demangle(F.getName().str().c_str()) << "\n";
						    errs() << *I << "\n  is used by:\n";
						    recurseUses(*I);
						    errs() << "\n";
                        
					}
				}
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
						//errs() << "  Insert code to write to Node table: resulting ptr of calloc, num elements: << *(I.getOperand(0)), size of element: << *(I.getOperand(1)) \n";
					}
					else if (f->getName().equals("malloc")) {
						malloc_present = true;
						//errs() << "  Insert code to write to Node table: resulting ptr of malloc, size: << *(I.getOperand(0)), size of element: might be able to get this using LLVM type information, otherwise speculate - 4 bytes? \n";
					}
				}
				// Check for other allocation functions - C++ new, etc.
			}
		}

		if (malloc_present == true) {
			//errs() << "  Select and emit trigger node\n";
		}

		return false;
	}
    
    struct myAllocCallInfo {
        std::vector<llvm::Instruction*> allocInst;
        std::vector<llvm::Value*> inputArgument;
    };

    myAllocCallInfo
    identifyAlloc(Function &F) 
    {
        myAllocCallInfo allocInfo;
		for (llvm::BasicBlock &BB : F) {
			for (llvm::Instruction &I : BB) {
				CallSite CS(&I);
				if (!CS.getInstruction()) {
					continue;
				}
				Value *called = CS.getCalledValue()->stripPointerCasts();

				if (llvm::Function *f = dyn_cast<Function>(called)) {
					if (f->getName().equals("myIntMallocFn32")) {
                        //errs() << "Alloc: " << I << "\n";
                        //errs() << "Argument0:" << *(CS.getArgOperand(0)) << "\n";
                        allocInfo.allocInst.push_back(&I);
                        allocInfo.inputArgument.push_back(CS.getArgOperand(0));
                    }
                }
            }
        }
        return allocInfo;
    }

    // ***** helper function to print vector of basic blocks ****** //
    // This version of the function takes a vector of T* as input
    template<typename T>
    void
    printVector(std::string inStr, std::vector<T*> inVector) {
        errs() << inStr << ": < ";
        for(auto it = inVector.begin(); it != inVector.end(); ++it) {
            errs() << **it << " ";
        }
        errs() << ">\n";
    }

	bool runOnFunction(Function &F) override {
        
        myAllocCallInfo allocInfo;
		//LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
		//		MemorySSA &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
		DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>().getDI();

		//MSSA.print(errs());

		//		errs() << "Prefetcher: ";
		//		errs().write_escaped(F.getName()) << '\n';
        
        allocInfo = identifyAlloc(F);
        if(!allocInfo.allocInst.empty()) {
		    errs() << "\n --- \n" << F.getName() << "\n --- \n";
            printVector("startPointers", allocInfo.allocInst);
            printVector("sizeOfArray", allocInfo.inputArgument);
            errs() << "\n";
        }

		identifyGEPDependence(F, DI);
		//identifyMemoryAllocations(F);
		//		printAll(F);

		return false;
	}
};
}

char Prefetcher::ID = 0; // Initialization value not important

static RegisterPass<Prefetcher> X("prefetcher", "Prefetcher Pass",
		false, /* Only looks at CFG */
		true /* Analysis Pass */);
