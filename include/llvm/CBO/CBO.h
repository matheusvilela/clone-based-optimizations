//====-- llvm/CBO/CBO.h - Clone-based optimizations --------------------======//
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

#ifndef LLVM_CBO_CBO_H
#define LLVM_CBO_CBO_H

namespace llvm {

ModulePass *createPADriverPass();
ModulePass *createCloneConstantArgsPass();
ModulePass *createNoAliasPass();
ModulePass *createDeadStoreEliminationPassPass();
ModulePass *createClonesDestroyerPass();
FunctionPass *createBlockEdgeFrequencyPassPass();
FunctionPass *createBranchPredictionPassPass();
FunctionPass *createStaticFunctionCostPassPass();

} // End llvm namespace

#endif
