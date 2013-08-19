#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializeCustom - Initialize all passes in the Custom library
void llvm::initializeStaticProfiler(PassRegistry &Registry) {
  initializeBlockEdgeFrequencyPassPass(Registry);
  initializeBranchPredictionPassPass(Registry);
  initializeStaticFunctionCostPassPass(Registry);
  initializeClonesDestroyerPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializeStaticProfiler(LLVMPassRegistryRef R) {
  initializeStaticProfiler(*unwrap(R));
}
