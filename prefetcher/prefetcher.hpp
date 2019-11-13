//
//
//

#ifndef PREFETCHER_HPP_
#define PREFETCHER_HPP_

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include <vector>

namespace llvm {
class Value;
class Instruction;
}; // namespace llvm

struct myAllocCallInfo {
  llvm::Instruction * allocInst;
  std::vector<llvm::Value *> inputArguments;
};

myAllocCallInfo identifyAlloc(llvm::Function &F);

#endif // PREFETCHER_HPP_
