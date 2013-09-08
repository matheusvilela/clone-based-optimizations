add_llvm_library(LLVMutils
  RecursionIdentifier.h
  RecursionIdentifier.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMutils intrinsics_gen)
