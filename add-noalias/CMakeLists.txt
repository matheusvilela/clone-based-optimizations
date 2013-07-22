include_directories(../pa-driver)

add_llvm_library(LLVMnoalias
  AddNoalias.cpp
  )

add_dependencies(LLVMnoalias intrinsics_gen)
