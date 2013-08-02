//====-- llvm/poolalloc/poolalloc.h - poolalloc passes -----------------======//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the poolalloc library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_POOLALLOC_POOLALLOC_H
#define LLVM_POOLALLOC_POOLALLOC_H

namespace llvm {

ModulePass *createAddressTakenAnalysisPass();
ModulePass *createAllocIdentifyPass();
ModulePass *createBasicDataStructuresPass();
ModulePass *createBUDataStructuresPass();
ModulePass *createCallTargetFinder_EQTDDataStructuresPass();
ModulePass *createCallTargetFinder_TDDataStructuresPass();
ModulePass *createCompleteBUDataStructuresPass();
FunctionPass *createDataStructureStatsPass();
ModulePass *createEntryPointAnalysisPass();
ModulePass *createEquivBUDataStructuresPass();
FunctionPass *createDSGCPass();
ModulePass *createLocalDataStructuresPass();
ModulePass *createSanityCheckPass();
ModulePass *createStdLibDataStructuresPass();
ModulePass *createTDDataStructuresPass();
ModulePass *createEQTDDataStructuresPass();
ModulePass *createTypeSafety_EQTDDataStructuresPass();
ModulePass *createTypeSafety_TDDataStructuresPass();

} // End llvm namespace

#endif
