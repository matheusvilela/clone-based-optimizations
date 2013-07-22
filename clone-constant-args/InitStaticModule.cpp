#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializeCCA(PassRegistry &Registry) {
  initializeCloneConstantArgsPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializeCCA(LLVMPassRegistryRef R) {
  initializeCCA(*unwrap(R));
}
