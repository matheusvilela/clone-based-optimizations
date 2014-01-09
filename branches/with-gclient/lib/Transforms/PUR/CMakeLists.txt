add_llvm_library(LLVMpur
  PUR.cpp
  CloneUnusedRetvals.cpp
  PruneClones.cpp
  ClonesCount.cpp
  )

add_dependencies(LLVMpur intrinsics_gen)
