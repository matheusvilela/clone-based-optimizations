#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializePoolAlloc(PassRegistry &Registry) {
  initializeAddressTakenAnalysisPass(Registry);
  initializeAllocIdentifyPass(Registry);
  initializeBasicDataStructuresPass(Registry);
  initializeBUDataStructuresPass(Registry);
  initializeCallTargetFinder_EQTDDataStructuresPass(Registry);
  initializeCallTargetFinder_TDDataStructuresPass(Registry);
  initializeCompleteBUDataStructuresPass(Registry);
  initializeDSGraphStatsPass(Registry);
  initializeEntryPointAnalysisPass(Registry);
  initializeEquivBUDataStructuresPass(Registry);
  initializeDSGCPass(Registry);
  initializeLocalDataStructuresPass(Registry);
  initializeSanityCheckPass(Registry);
  initializeStdLibDataStructuresPass(Registry);
  initializeTDDataStructuresPass(Registry);
  initializeEQTDDataStructuresPass(Registry);
  initializeTypeSafety_EQTDDataStructuresPass(Registry);
  initializeTypeSafety_TDDataStructuresPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializePoolAlloc(LLVMPassRegistryRef R) {
  initializePoolAlloc(*unwrap(R));
}
