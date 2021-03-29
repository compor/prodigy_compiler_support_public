# build config

set(LIB_SOURCES prefetcher.cpp prefetcher_codegen.cpp)

add_llvm_library(LLVMPrefetcher MODULE ${LIB_SOURCES} PLUGIN_TOOL opt)
