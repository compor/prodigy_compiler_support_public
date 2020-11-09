//
//
//

#ifndef PREFETCHER_HPP_
#define PREFETCHER_HPP_

// LLVM
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Instruction.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

#include "llvm/ADT/SmallVector.h"
// using llvm::SmallVector

#include "llvm/Support/Debug.h"
// using DEBUG macro
// using llvm::dbgs

// standard
#include <vector>
// using std::vector

#include <string>
// using std::string

// project
#include "util.hpp"

#define DEBUG_TYPE "prefetcher-analysis"

namespace llvm {
class Value;
class Instruction;
}; // namespace llvm

struct myAllocCallInfo {
	llvm::Instruction *allocInst;
	llvm::SmallVector<llvm::Value *, 3> inputArguments;
};

struct GEPDepInfo {
	llvm::Value *source;
	llvm::Value *target;
	llvm::Function *funcSource;
	llvm::Function *funcTarget;
	llvm::Instruction * source_use;
	llvm::Instruction * load_to_copy;
	llvm::Instruction * phi_node;
	bool phi = false;

	bool operator<(const GEPDepInfo &Other) const {
		return source < Other.source && target < Other.target;
	}

	bool operator==(const GEPDepInfo &Other) const {
		return (source == Other.source && target == Other.target);
	}
};

struct PrefetcherAnalysisResult {
	llvm::SmallVector<myAllocCallInfo, 8> allocs;
	llvm::SmallVector<GEPDepInfo, 8> geps;
	llvm::SmallVector<GEPDepInfo, 8> ri_geps;
	// TODO: Kuba add results from edge analysis
};

using namespace llvm;

// ***** helper function to print vectors ****** //
// This version of the function takes a vector of T* as input
template <typename T> void printVector(std::string inStr, T begin, T end) {
	errs() << inStr << ": < ";
	for (auto it = begin; it != end; ++it) {
		errs() << **it << "\n";
	}
	errs() << ">\n";
}

class PrefetcherPass : public llvm::FunctionPass {
public:
	static char ID;

	using ResultT = PrefetcherAnalysisResult;

	ResultT * Result;

	PrefetcherPass() : llvm::FunctionPass(ID) {Result = new ResultT();}

	~PrefetcherPass(){delete Result;}

	const ResultT *getPFA() const { return Result; }

	ResultT *getPFA() { return Result; }

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

	bool runOnFunction(llvm::Function &F) override;
};

class SinValIndirectionPass : public llvm::ModulePass {
public:
	static char ID;

	using ResultT = PrefetcherAnalysisResult;

	ResultT Result;

	const ResultT &getPFA() const { return Result; }

	ResultT &getPFA() { return Result; }

	SinValIndirectionPass() : ModulePass(ID) {}

	virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

	bool runOnModule(Module &M) override;
};

struct RangedIndirectionPass : public ModulePass {
  static char ID;
  RangedIndirectionPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool getUserFromInstrTypeRI(Instruction &I, std::vector<Instruction *> &uses, const char *InstrType);
};
#endif // PREFETCHER_HPP_
