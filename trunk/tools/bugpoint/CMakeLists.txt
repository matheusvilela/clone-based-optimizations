set(LLVM_LINK_COMPONENTS asmparser instrumentation scalaropts ipo
    linker bitreader bitwriter irreader vectorize objcarcopts pur pta
    pa noalias cca dse static-profiler utils DataStructure)

include_directories(../../llvm/tools/bugpoint)

add_llvm_tool(bugpoint
  ../../llvm/tools/bugpoint/BugDriver.cpp
  ../../llvm/tools/bugpoint/CrashDebugger.cpp
  ../../llvm/tools/bugpoint/ExecutionDriver.cpp
  ../../llvm/tools/bugpoint/ExtractFunction.cpp
  ../../llvm/tools/bugpoint/FindBugs.cpp
  ../../llvm/tools/bugpoint/Miscompilation.cpp
  ../../llvm/tools/bugpoint/OptimizerDriver.cpp
  ../../llvm/tools/bugpoint/ToolRunner.cpp
  bugpoint.cpp
  )
