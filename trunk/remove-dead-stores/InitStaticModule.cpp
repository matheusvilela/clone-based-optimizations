#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializeCustom - Initialize all passes in the Custom library
void llvm::initializeDSE(PassRegistry &Registry) {
  initializeDeadStoreEliminationPassPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializeDSE(LLVMPassRegistryRef R) {
  initializeDSE(*unwrap(R));
}
