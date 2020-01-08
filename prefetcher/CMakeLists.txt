# build config

#add_llvm_loadable_module(LLVMPrefetcher prefetcher.cpp prefetcher_codegen.cpp)

add_llvm_library( LLVMPrefetcher MODULE prefetcher.cpp prefetcher_codegen.cpp PLUGIN_TOOL opt)
