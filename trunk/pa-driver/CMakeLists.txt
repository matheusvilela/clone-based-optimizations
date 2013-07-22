add_llvm_library(LLVMpa
  PADriver.cpp
  PointerAnalysis.cpp
  )

add_dependencies(LLVMpa intrinsics_gen)
