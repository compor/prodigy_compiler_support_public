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

#include "llvm/Analysis/LoopInfo.h"
// using llvm::LoopInfoWrapperPass
// using llvm::LoopInfo

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

#define DEBUG_TYPE "prefetcher-codegen"

// plugin registration for opt

namespace {

struct PrefetcherRuntime {
  static constexpr char *SimRoiStart = "sim_roi_start";
  static constexpr char *SimRoiEnd = "sim_roi_end";

  static const std::vector<std::string> Functions;
};

const std::vector<std::string> PrefetcherRuntime::Functions = {
    PrefetcherRuntime::SimRoiStart, PrefetcherRuntime::SimRoiEnd};

class PrefetcherCodegen {
  llvm::Module *Mod;

  void insert() {
    for (auto e : PrefetcherRuntime::Functions) {
      auto *funcType = llvm::FunctionType::get(
          llvm::Type::getVoidTy(Mod->getContext()), true);

      Mod->getOrInsertFunction(e, funcType);
//      LLVM_DEBUG(llvm::dbgs() << "adding func << " << e << " to module "
//                              << Mod->getName() << "\n");
    }

    return;
  }

public:
  PrefetcherCodegen(llvm::Module &M) : Mod(&M) { insert(); };
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
    X("prefetcher-codegen", "Prefetcher Codegen Pass", false, false);

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

bool PrefetcherCodegenPass::runOnModule(llvm::Module &CurMod) {
  bool hasModuleChanged = false;

  PrefetcherCodegen pfcg(CurMod);
  hasModuleChanged = true;

  return hasModuleChanged;
}

void PrefetcherCodegenPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
  AU.addRequiredTransitive<llvm::LoopInfoWrapperPass>();
  AU.addPreserved<llvm::LoopInfoWrapperPass>();

  return;
}

} // namespace
