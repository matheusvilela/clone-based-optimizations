#include "RecursionIdentifier.h"

using namespace llvm;

bool RecursionIdentifier::runOnModule(Module& M) {
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

void RecursionIdentifier::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<CallGraph>();
  AU.setPreservesAll();
}

bool RecursionIdentifier::isRecursive(Function* F) {
  return recursiveFuncs.count(F) != 0;
}

int RecursionIdentifier::getRecursiveFunctionsCount() {
  return recursiveFuncs.size();
}

std::set<Function*>& RecursionIdentifier::getAllRecursiveFunctions() {
  return recursiveFuncs;
}

void RecursionIdentifier::print(raw_ostream &O, const Module *M) const {
  O << "Recursive functions on this module:\n";
  for (std::set<Function*>::iterator FNIter = recursiveFuncs.begin();
      FNIter != recursiveFuncs.end(); ++FNIter) {
    O << (*FNIter)->getName() << "\n";
  }
}

char RecursionIdentifier::ID = 0;

INITIALIZE_PASS(RecursionIdentifier, "recursion-identifier",
    "Extracts a few useful informations about the recursive functions in a program, including mutually recursive functions.", false, true)

ModulePass *llvm::createRecursionIdentifierPass() {
  return new RecursionIdentifier;
}
