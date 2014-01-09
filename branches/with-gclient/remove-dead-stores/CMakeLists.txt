include_directories(../pa-driver)

add_llvm_library(LLVMdse
  DeadStoreElimination.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMdse intrinsics_gen)
