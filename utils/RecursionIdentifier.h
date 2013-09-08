#undef DEBUG_TYPE
#define DEBUG_TYPE "RecursionIdentifier"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"

#include <set>
#include <vector>

namespace llvm {
  STATISTIC(RecursiveFunctions, "Number of recursive functions.");
  class RecursionIdentifier : public ModulePass {

    std::set<Function*> recursiveFuncs;

   public:

    static char ID;

    RecursionIdentifier(): ModulePass(ID) {
      RecursiveFunctions = 0;
    };
    bool runOnModule(Module& M);
    void getAnalysisUsage(AnalysisUsage &AU) const;
    bool isRecursive(Function* F);
    int getRecursiveFunctionsCount();
    std::set<Function*>& getAllRecursiveFunctions();
    void print(raw_ostream &O, const Module *M) const;
  };
}
