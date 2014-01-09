//===- CountFunctionCalls.cpp - Count diverse kinds of function calls -----===//
//
// This pass servers to count the number of occurrences of certain kinds of
// function calls.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "fncount"

#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/InstVisitor.h"

#include "IsUnusedRetval.h"

using namespace llvm;

STATISTIC(NrInternalUnusedRetvals, "Number of unused retvals in internal calls");
STATISTIC(NrExternalUnusedRetvals, "Number of unused retvals in external calls");
STATISTIC(NrInternalFunctionCalls, "Number of internal function calls");
STATISTIC(NrExternalFunctionCalls, "Number of external function calls");
STATISTIC(NrIndirectInvocations, "Number of indirect invocations");

namespace {
  struct CountFunctionCalls :
    public ModulePass,
    public InstVisitor<CountFunctionCalls> {

    static char ID; // Pass identification, replacement for typeid

    CountFunctionCalls() : ModulePass(ID) {}

    // We're interested only in function call instructions
    void visitCallInst(CallInst &I) {
      Function *calledFunction = I.getCalledFunction();
      if (!calledFunction) {
        // If the instruction doesn't point directly to a function, then
        // this is an indirect invocation (using function pointer).
        NrIndirectInvocations++;
      }
      else if (calledFunction->isDeclaration()) {
        // Declared functions are external to this module
        NrExternalFunctionCalls++;
        if (isUnusedRetval(I))
          NrExternalUnusedRetvals++;
      }
      else {
        // Defined functions are internal to this module
        NrInternalFunctionCalls++;
        if (isUnusedRetval(I))
          NrInternalUnusedRetvals++;
      }
    }

    // Start module counting starting the visitor
    virtual bool runOnModule(Module &M) {
      visit(M);
      return false;
    }

    // We're just counting, so nothing will be changed
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }
  };
}

char CountFunctionCalls::ID = 0;

static RegisterPass<CountFunctionCalls>
X("fncount", "Counts the various types of function calls");
