#include "llvm/InitializePasses.h"
#include "llvm-c/Initialization.h"

using namespace llvm;

/// initializeUtils - Initialize all passes in the Utils library
void llvm::initializeUtils(PassRegistry &Registry) {
  initializeRecursionIdentifierPass(Registry);
  initializeClonesStatisticsPass(Registry);
}

/// LLVMInitializeUtlis - C binding for initializeUtils
void LLVMInitializeUtils(LLVMPassRegistryRef R) {
  initializeUtils(*unwrap(R));
}
