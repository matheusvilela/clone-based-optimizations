add_llvm_library(LLVMpta
  LCD.cpp
  LCD.h
  PointsToSolver.h
  Andersens.cpp
  ConstraintsGraph.cpp
  PTA.cpp
  )

add_dependencies(LLVMpta intrinsics_gen)
