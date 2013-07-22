include_directories(../../lib/PointsToAnalysis)

set(LLVM_LINK_COMPONENTS
  asmparser
  core
  ipa
  pta
  ScalarOpts
  )

add_llvm_unittest(PointsToAnalysisTests
  PointsToAnalysisTest.cpp
  AndersensTest.cpp
  ConstraintsGraphTest.cpp
  )
