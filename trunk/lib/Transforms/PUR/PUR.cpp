#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializePUR(PassRegistry &Registry) {
  initializeCloneUnusedRetvalsPass(Registry);
  initializePruneClonesPass(Registry);
  initializeClonesCountPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializePUR(LLVMPassRegistryRef R) {
  initializePUR(*unwrap(R));
}
