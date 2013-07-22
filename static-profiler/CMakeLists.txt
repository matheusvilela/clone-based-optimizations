add_llvm_library(LLVMstatic-profiler
  BlockEdgeFrequencyPass.cpp
  BlockEdgeFrequencyPass.h
  BranchHeuristicsInfo.cpp
  BranchHeuristicsInfo.h
  BranchPredictionDot.cpp
  BranchPredictionDot.h
  BranchPredictionInfo.cpp
  BranchPredictionInfo.h
  BranchPredictionPass.cpp
  BranchPredictionPass.h
  StaticFunctionCost.cpp
  StaticFunctionCost.h
  InitStaticModule.cpp
  )

add_dependencies(LLVMstatic-profiler intrinsics_gen)
