#undef DEBUG_TYPE
#define DEBUG_TYPE "RecursionIdentifier"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"

#include <set>
#include <vector>

using namespace llvm;

STATISTIC(RecursiveFunctions, "Number of recursive functions.");
class RecursionIdentifier : public ModulePass {

  std::set<Function*> recursiveFuncs;

 public:

  static char ID;

  RecursionIdentifier(): ModulePass(ID) {
    RecursiveFunctions = 0;
  };

  virtual bool runOnModule(Module& M) {
    CallGraph& CG = getAnalysis<CallGraph>();
    for (scc_iterator<CallGraph*> CGIter = scc_begin(&CG); CGIter != scc_end(&CG); ++CGIter) {
      std::vector<CallGraphNode*> &NodeVec = *CGIter;
      if (NodeVec[0]->getFunction() == NULL || NodeVec[0]->getFunction()->isDeclaration()){
        continue;
      }
      if (CGIter.hasLoop()) {
        for (std::vector<CallGraphNode*>::iterator NVIter = NodeVec.begin(); 
            NVIter != NodeVec.end(); ++NVIter) {
          Function* fn = (*NVIter)->getFunction();
          recursiveFuncs.insert(fn);
          RecursiveFunctions++;
        }
      }
    }
    // input program is not modified so this function returns false
    return false;
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<CallGraph>();
    AU.setPreservesAll();
  }

  bool isRecursive(Function* F) {
    return recursiveFuncs.count(F) != 0;
  }

  int getRecursiveFunctionsCount() {
    return recursiveFuncs.size();
  }

  std::set<Function*>& getAllRecursiveFunctions() {
    return recursiveFuncs;
  }

  void print(raw_ostream &O, const Module *M) const {
    O << "Recursive functions on this module:\n";
    for (std::set<Function*>::iterator FNIter = recursiveFuncs.begin();
        FNIter != recursiveFuncs.end(); ++FNIter) {
      O << (*FNIter)->getName() << "\n";
    }
  }
};
char RecursionIdentifier::ID = 0;

INITIALIZE_PASS(RecursionIdentifier, "recursion-identifier",
    "Extracts a few useful informations about the recursive functions in a program, including mutually recursive functions.", false, true)

ModulePass *llvm::createRecursionIdentifierPass() {
  return new RecursionIdentifier;
}
