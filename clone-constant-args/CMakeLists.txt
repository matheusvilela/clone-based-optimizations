add_llvm_library(LLVMcca
  CloneConstantArgs.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMcca intrinsics_gen)
