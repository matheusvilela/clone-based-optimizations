#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializeNoAlias(PassRegistry &Registry) {
  initializeAddNoaliasPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializeNoAlias(LLVMPassRegistryRef R) {
  initializeNoAlias(*unwrap(R));
}
