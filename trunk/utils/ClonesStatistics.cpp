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
#include "RecursionIdentifier.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "clones-statistics"

using namespace llvm;

STATISTIC(HighestProfit,   "Highest profit cloning a function");
STATISTIC(RecursiveClones, "Number of clones that are recursive functions");
STATISTIC(CloningSize,     "Size of cloning");
STATISTIC(InliningSize,    "Size of inlining");
class ClonesStatistics : public ModulePass {

  std::map<std::string, std::vector<Function*> > functions;
  StaticFunctionCostPass *SFCP;
  RecursionIdentifier *RI;
  std::string highestProfitFn;

  public:

  static char ID;

  ClonesStatistics() : ModulePass(ID) {
    HighestProfit   = 0;
    RecursiveClones = 0;
    CloningSize     = 0;
    InliningSize    = 0;
  }

  // +++++ METHODS +++++ //

  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &M);
  virtual void print(raw_ostream& O, const Module* M) const;
  void collectFunctions(Function &F);
  void getStatistics();
  int getFunctionSize(Function &F);
};

// ============================= //

int ClonesStatistics::getFunctionSize(Function &F) {
  int functionSize = 0;
  for(Function::iterator it = F.begin(); it != F.end(); it++) {
     functionSize += it->size();
  }
  return functionSize;
}

void ClonesStatistics::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<StaticFunctionCostPass>();
  AU.addRequired<RecursionIdentifier>();
  AU.setPreservesAll();
}

bool ClonesStatistics::runOnModule(Module &M) {

  // Get information about recursive functions
  RI = &getAnalysis<RecursionIdentifier>();

  // Collect information
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      collectFunctions(*F);
    }
  }
  getStatistics();

  DEBUG(print(errs(), &M));
  return false;
}

// ============================= //

void ClonesStatistics::print(raw_ostream& O, const Module* M) const {
  O << "Highest profit: " << HighestProfit << '\n';
  O << "Obtained on function " << highestProfitFn << '\n';
}

void ClonesStatistics::collectFunctions(Function &F) {
  std::string fnName = F.getName();

  Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+)|(\\.noret))+");
  bool isCloned = ending.match(fnName);

  std::string originalName = fnName;
  if (isCloned) {
    Regex noaliasend("\\.noalias");
    Regex constargsend("\\.constargs[0-9]+");
    Regex noretend("\\.noret");

    if (noaliasend.match(fnName)) {
      originalName = noaliasend.sub("", originalName);
    }
    if (constargsend.match(fnName)) {
      originalName = constargsend.sub("", originalName);
    }
    if (noretend.match(fnName)) {
      originalName = noretend.sub("", originalName);
    }
  }
  functions[originalName].push_back(&F);
}

void ClonesStatistics::getStatistics() {

  // Get cloned functions and their base function
  std::map<Function*, std::vector<Function*> > fn2clonedFns;
  for (std::map<std::string, std::vector<Function*> >::iterator it = functions.begin(); it != functions.end(); ++it) {
    int numFunctions = it->second.size();

    Function *originalFn = NULL;
    std::vector<Function*> clonedFns;
    for (int i = 0; i < numFunctions; ++i) {
      Function* F = it->second[i];
      std::string fnName = F->getName();
      Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+)|(\\.noret))+");
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

    int originalSize = getFunctionSize(*originalFn);

    // Get original function uses
    std::vector<User*> uses;
    for (Value::use_iterator UI = originalFn->use_begin(); UI != originalFn->use_end(); ++UI) {
       User *U = *UI;
       if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;
       uses.push_back(U);
    }

    int clonesSize = 0;
    for (std::vector<Function*>::iterator it2 = clonedFns.begin(); it2 != clonedFns.end(); ++it2) {
      Function* clonedFn = *it2;

      // Verify if the clone is recursive
      if (RI->isRecursive(clonedFn)) {
        RecursiveClones++;
      }

      // Get clone size
      clonesSize += getFunctionSize(*clonedFn);

      // Get clones uses
      for (Value::use_iterator UI = clonedFn->use_begin(); UI != clonedFn->use_end(); ++UI) {
         User *U = *UI;
         if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;
         uses.push_back(U);
      }

      // Estimate cloned function cost with the static profiler
      SFCP = &getAnalysis<StaticFunctionCostPass>(*clonedFn);
      clonedCost = SFCP->getFunctionCost();

      // Get profit
      unsigned int profit = originalCost - clonedCost;
      if (profit > HighestProfit) {
         HighestProfit = profit;
         highestProfitFn = originalFn->getName();
      }
    }

    // Estimate cloning and inlining size
    CloningSize  += clonesSize + originalSize;
    InliningSize += originalSize * uses.size();
  }
}

// Register the pass to the LLVM framework
char ClonesStatistics::ID = 0;

INITIALIZE_PASS(ClonesStatistics, "clones-statistics",
                "Get statistics about the cloning optimizations", false, true)

ModulePass *llvm::createClonesStatisticsPass() {
  return new ClonesStatistics;
}
