add_llvm_library( LLVMRangedIndirection MODULE
  RangedIndirection.cpp

  PLUGIN_TOOL
  opt
  )
