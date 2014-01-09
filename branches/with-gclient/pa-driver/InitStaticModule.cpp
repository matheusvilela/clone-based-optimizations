#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePruneUnusedRetvals - Initialize all passes in the Custom library
void llvm::initializePA(PassRegistry &Registry) {
  initializePADriverPass(Registry);
}

/// LLVMInitializeCustom - C binding for initializeCustom.
void LLVMInitializePA(LLVMPassRegistryRef R) {
  initializePA(*unwrap(R));
}
