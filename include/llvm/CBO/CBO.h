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
// in the CBO library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CBO_H
#define LLVM_ANALYSIS_CBO_H

namespace llvm {

ModulePass *createPADriverPass();
ModulePass *createCloneConstantArgsPass();
ModulePass *createNoAliasPass();
ModulePass *createDeadStoreEliminationPassPass();
FunctionPass *createBlockEdgeFrequencyPassPass();
FunctionPass *createBranchPredictionPassPass();

} // End llvm namespace

#endif
