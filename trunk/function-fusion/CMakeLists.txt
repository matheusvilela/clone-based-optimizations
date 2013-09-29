add_llvm_library(LLVMfunction-fusion
  FunctionFusion.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMfunction-fusion intrinsics_gen)
