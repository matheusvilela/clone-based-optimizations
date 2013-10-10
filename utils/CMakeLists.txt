include_directories(../static-profiler)

add_llvm_library(LLVMutils
  RecursionIdentifier.h
  RecursionIdentifier.cpp
  ClonesStatistics.cpp
  ClonesCleaner.cpp
  InitStaticModule.cpp
  )

add_dependencies(LLVMutils intrinsics_gen)
