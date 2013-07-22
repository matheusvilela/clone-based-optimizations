//====-- llvm/Analysis/PTA.h - Points-to Alias Analysisused Retvals ----======//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the Points-to Analysis library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PUR_H
#define LLVM_ANALYSIS_PUR_H

namespace llvm {

//===----------------------------------------------------------------------===//
/// createAndersensPass - This pass implements Andersen's interprocedural alias
/// analysis.
ModulePass *createAndersensPass();

} // End llvm namespace

#endif
