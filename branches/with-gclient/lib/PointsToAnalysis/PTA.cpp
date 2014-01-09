#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializePTA - Initialize all passes in the Custom library
void llvm::initializePTA(PassRegistry &Registry) {
  initializeAndersensPass(Registry);
}

/// LLVMInitializePTA - C binding for initializeCustom.
void LLVMInitializePTA(LLVMPassRegistryRef R) {
  initializePTA(*unwrap(R));
}
