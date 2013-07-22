add_llvm_library(LLVMcca
  CloneConstantArgs.cpp
  )

add_dependencies(LLVMcca intrinsics_gen)
