add_llvm_library(LLVMpa
  PADriver.cpp
  PointerAnalysis.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMpa intrinsics_gen)
