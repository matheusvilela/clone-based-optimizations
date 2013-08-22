#undef DEBUG_TYPE
#define DEBUG_TYPE "remove-worthless-clones"
#include <sstream>
#include <unistd.h>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"
#include "StaticFunctionCost.h"

using namespace llvm;

STATISTIC(ClonesRemoved, "Number of cloned functions removed");
STATISTIC(CallsRestored, "Number of calls restored");
STATISTIC(OrphansDropped, "Number of ophan functions removed");
STATISTIC(HighestProfit, "Highest profit cloning a function");
class ClonesDestroyer : public ModulePass {

  std::map<std::string, std::vector<Function*> > functions;
  StaticFunctionCostPass *SFCP;
  std::string highestProfitFn;
  public:

  static char ID;

  ClonesDestroyer() : ModulePass(ID) {
    ClonesRemoved  = 0;
    CallsRestored  = 0;
    OrphansDropped = 0;
    HighestProfit = 0;
  }

  // +++++ METHODS +++++ //

  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &M);
  virtual void print(raw_ostream& O, const Module* M) const;
  void collectFunctions(Function &F);
  bool removeWorthlessClones();
  bool substCallingInstructions(Function* oldFn, Function* newFn);
};

// ============================= //

void ClonesDestroyer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<StaticFunctionCostPass>();
  AU.setPreservesAll();
}

bool ClonesDestroyer::runOnModule(Module &M) {

  // Collect information
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      collectFunctions(*F);
    }
  }
  bool modified = removeWorthlessClones();

  DEBUG(errs() << "Number of clones removed: " << ClonesRemoved << '\n');
  DEBUG(errs() << "Number of calls restored: " << CallsRestored << '\n');
  DEBUG(errs() << "Number of orphans dropped: " << OrphansDropped << '\n');
  DEBUG(errs() << "Highest profit: " << HighestProfit << '\n');
  DEBUG(errs() << "Obtained on function " << highestProfitFn << '\n');
  return modified;
}

// ============================= //

void ClonesDestroyer::print(raw_ostream& O, const Module* M) const {
  O << "Number of clones removed: " << ClonesRemoved << '\n';
  O << "Number of calls restored: " << CallsRestored << '\n';
  O << "Number of orphans dropped: " << OrphansDropped << '\n';
  O << "Highest profit: " << HighestProfit << '\n';
  O << "Obtained on function " << highestProfitFn << '\n';
}

void ClonesDestroyer::collectFunctions(Function &F) {
  std::string fnName = F.getName();

  Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+))+");
  bool isCloned = ending.match(fnName);

  std::string originalName = fnName;
  if (isCloned) {
    Regex noaliasend("\\.noalias");
    Regex constargsend("\\.constargs[0-9]+");

    if (noaliasend.match(fnName)) {
      originalName = noaliasend.sub("", originalName);
    }
    if (constargsend.match(fnName)) {
      originalName = constargsend.sub("", originalName);
    }
  }
  functions[originalName].push_back(&F);
}

bool ClonesDestroyer::removeWorthlessClones() {

  // Get cloned functions and their base function
  std::map<Function*, std::vector<Function*> > fn2clonedFns;
  for (std::map<std::string, std::vector<Function*> >::iterator it = functions.begin(); it != functions.end(); ++it) {
    int numFunctions = it->second.size();

    Function *originalFn = NULL;
    std::vector<Function*> clonedFns;
    for (int i = 0; i < numFunctions; ++i) {
      Function* F = it->second[i];
      std::string fnName = F->getName();
      Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+))+");
      bool isCloned = ending.match(fnName);
      if (isCloned) {
        clonedFns.push_back(F);
      } else {
        originalFn = F;
      }
    }
    if (originalFn == NULL) continue;
    for (std::vector<Function*>::iterator it2 = clonedFns.begin(); it2 != clonedFns.end(); ++it2) {
      Function* clonedFn = *it2;
      fn2clonedFns[originalFn].push_back(clonedFn);
    }
  }

  for (std::map<Function*, std::vector<Function*> >::iterator it = fn2clonedFns.begin(); it != fn2clonedFns.end(); ++it) {
    Function *originalFn = it->first;
    std::vector<Function*> clonedFns = it->second;
    double originalCost, clonedCost;

    // Estimate original function cost with the static profiler
    SFCP = &getAnalysis<StaticFunctionCostPass>(*originalFn);
    originalCost = SFCP->getFunctionCost();

    for (std::vector<Function*>::iterator it2 = clonedFns.begin(); it2 != clonedFns.end(); ++it2) {
      Function* clonedFn = *it2;

      // Estimate cloned function cost with the static profiler
      SFCP = &getAnalysis<StaticFunctionCostPass>(*clonedFn);
      clonedCost = SFCP->getFunctionCost();

      // Try to remove worthless clones
      if (clonedCost >= originalCost) {
        if(substCallingInstructions(clonedFn, originalFn)) {
          ClonesRemoved++;
        }

        if (clonedFn->use_empty()) {
          clonedFn->dropAllReferences();
          clonedFn->removeFromParent();
        }
      } else {
        // Get profit
        unsigned int profit = originalCost - clonedCost;
        if (profit > HighestProfit) {
          HighestProfit = profit;
          highestProfitFn = originalFn->getName();
        }
      }
    }
    // Drop orphan functions
    if(originalFn->use_empty()) {
      originalFn->dropAllReferences();
      originalFn->removeFromParent();
      OrphansDropped++;
    }
  }

  return ClonesRemoved > 0;
}

bool ClonesDestroyer::substCallingInstructions(Function* oldFn, Function* newFn) {

  // Verify if functions are 'compatible'
  FunctionType *newFTy = newFn->getFunctionType();
  FunctionType *oldFTy = oldFn->getFunctionType();
  if (newFTy->getNumParams() != oldFTy->getNumParams() || newFTy->getReturnType() != oldFTy->getReturnType()) {
    return false;
  }
  for (unsigned i = 0, e = oldFTy->getNumParams(); i != e; ++i) {
    if (oldFTy->getParamType(i) != newFTy->getParamType(i)) {
      return false;
    }
  }

  // Get uses
  std::vector<User*> callers;
  for (Value::use_iterator UI = oldFn->use_begin(); UI != oldFn->use_end(); ++UI) {
    User *U = *UI;
    if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;
    callers.push_back(U);
  }

  // Restore calls
  int callsRestored = 0;
  for (std::vector<User*>::iterator it = callers.begin(); it != callers.end(); ++it) {
    User* caller = *it;
    callsRestored++;
    if (isa<CallInst>(caller)) {
      CallInst *callInst = dyn_cast<CallInst>(caller);
      callInst->setCalledFunction(newFn);
    } else if (isa<InvokeInst>(caller)) {
      InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
      invokeInst->setCalledFunction(newFn);
    }
  }

  CallsRestored += callsRestored;

  return callsRestored > 0;
}

// Register the pass to the LLVM framework
char ClonesDestroyer::ID = 0;

INITIALIZE_PASS(ClonesDestroyer, "remove-worthless-clones",
                "Statically estimate if a worthless clone should be removed", false, true)

ModulePass *llvm::createClonesDestroyerPass() {
  return new ClonesDestroyer;
}
