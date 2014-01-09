#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraphSCCPass.h"

#include <set>
#include <vector>

#undef DEBUG_TYPE
#define DEBUG_TYPE "recursion-identifier"
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
