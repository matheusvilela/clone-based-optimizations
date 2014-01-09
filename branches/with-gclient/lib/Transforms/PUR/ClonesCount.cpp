//===- ClonesCount.cpp - Count clones -------------------------------------===//
//
// This pass counts the number of cloned functions and classify call sites to
// provide an idea of the cloning range.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "clones-count"
#include "llvm/Pass.h"
#include "llvm/PassSupport.h"
#include "llvm/InstVisitor.h"
#include "llvm/IR/Attributes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/PUR.h"

using namespace llvm;

STATISTIC(NrIndifferentCalls, "Number of indifferent calls");
STATISTIC(NrPromisingCalls, "Number of promising calls");
STATISTIC(NrCloneFns, "Number of cloned functions");
STATISTIC(NrCloneCalls, "Number of calls to clones");
STATISTIC(NrInnocuousCalls, "Number of innocuous calls");

namespace {

  struct ClonesCount :
    public ModulePass,
    public InstVisitor<ClonesCount> {

    static char ID; // Pass identification

    typedef std::vector<CallSite> CallRefsList;
    typedef std::map<Function*,CallRefsList> CallRefsMap;
    typedef std::map<Function*,Function*> Fn2CloneMap;

    CallRefsMap unusedRetvals;
    Fn2CloneMap clonedFunctions;

    ClonesCount() : ModulePass(ID) {
      initializeClonesCountPass(*PassRegistry::getPassRegistry());
    }

    void visitFunction(Function &F) {
      if (F.getName().endswith(".noret"))
        NrCloneFns++;
    }

    void visitCallSite(CallSite CS) {
      Function *calledFunction = CS.getCalledFunction();

      // We're not interested in indirect function calls.
      if (!calledFunction // Indirect function calls.
          || calledFunction->isDeclaration()) { // External function calls.
        NrIndifferentCalls++;
        return; 
      }

      // The called function doesn't return value.
      if (calledFunction->getReturnType()->getTypeID() == Type::VoidTyID) {
        NrIndifferentCalls++;
        return;
      }

      // If the instruction has one or more uses, it means that the return
      // value is being used by the calling function.
      Instruction *Call = CS.getInstruction();
      if (Call->hasNUsesOrMore(1)) {
        NrInnocuousCalls++;
        return;
      }

      NrPromisingCalls++;

      if (calledFunction->getName().endswith(".noret"))
        NrCloneCalls++;
    }

    virtual bool runOnModule(Module &M) {
      visit(M);
      return false;
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    }
  };

} // end empty namespace.

char ClonesCount::ID = 0;
INITIALIZE_PASS(ClonesCount, "clonescount", "Collect stats about cloned functions", false, false)

ModulePass *llvm::createClonesCountPass() {
  return new ClonesCount();
}
