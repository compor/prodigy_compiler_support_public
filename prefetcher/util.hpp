/*
 * util.hpp
 *
 *  Created on: 7 Nov 2019
 *      Author: kuba
 */

#ifndef PREFETCHER_UTIL_HPP_
#define PREFETCHER_UTIL_HPP_

#include <cxxabi.h>

inline std::string demangle(const char* name)
{
	int status = -1;

	std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
	return (status == 0) ? res.get() : std::string(name);
}

llvm::Function* getFunctionFromInst(llvm::Instruction &I, std::string s)
{
	llvm::BasicBlock * B = I.getParent();
	llvm::Function * F = B->getParent();
	llvm::Module * M = F->getParent();
	llvm::Function * R = M->getFunction(s);
	return R;
}


#endif /* PREFETCHER_UTIL_HPP_ */
