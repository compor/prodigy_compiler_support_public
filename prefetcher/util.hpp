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


#endif /* PREFETCHER_UTIL_HPP_ */
