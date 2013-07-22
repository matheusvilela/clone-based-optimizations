set(LLVM_LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD} bitreader asmparser bitwriter irreader instrumentation scalaropts objcarcopts ipo vectorize pur pta)

add_llvm_tool(opt
  ../../llvm/tools/opt/AnalysisWrappers.cpp
  ../../llvm/tools/opt/GraphPrinters.cpp
  ../../llvm/tools/opt/PrintSCC.cpp
  opt.cpp
  )
