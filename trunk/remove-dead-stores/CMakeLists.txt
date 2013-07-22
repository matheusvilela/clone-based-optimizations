include_directories(../pa-driver)

add_llvm_library(LLVMdse
  DeadStoreElimination.cpp
  )

add_dependencies(LLVMdse intrinsics_gen)
