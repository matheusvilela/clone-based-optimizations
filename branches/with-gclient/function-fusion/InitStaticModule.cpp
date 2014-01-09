#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializeFunctionFusion(PassRegistry &Registry) {
  initializeFunctionFusionPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializeFunctionFusion(LLVMPassRegistryRef R) {
  initializeFunctionFusion(*unwrap(R));
}
