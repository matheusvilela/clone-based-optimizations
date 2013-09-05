add_llvm_library(LLVMutils
  RecursionIdentifier.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMutils intrinsics_gen)
