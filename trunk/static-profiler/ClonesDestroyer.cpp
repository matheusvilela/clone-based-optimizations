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

STATISTIC(ClonesRemoved,  "Number of cloned functions removed");
STATISTIC(OrphansDropped, "Number of ophan functions removed");
STATISTIC(CallsRestored,  "Number of calls restored");
class ClonesDestroyer : public ModulePass {

  std::map<std::string, std::vector<Function*> > functions;
  StaticFunctionCostPass *SFCP;
  public:

  static char ID;

  ClonesDestroyer() : ModulePass(ID) {
    ClonesRemoved   = 0;
    CallsRestored   = 0;
    OrphansDropped  = 0;
  }

  // +++++ METHODS +++++ //

  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &M);
  virtual void print(raw_ostream& O, const Module* M) const;
  void collectFunctions(Function &F);
  bool removeWorthlessClones();
  bool substituteCallSites(Function *Fn, Function *Clone, bool isNoRetOpt);
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
  return modified;
}

// ============================= //

void ClonesDestroyer::print(raw_ostream& O, const Module* M) const {
  O << "Number of clones removed: " << ClonesRemoved << '\n';
  O << "Number of calls restored: " << CallsRestored << '\n';
  O << "Number of orphans dropped: " << OrphansDropped << '\n';
}

void ClonesDestroyer::collectFunctions(Function &F) {
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

    for (std::vector<Function*>::iterator it2 = clonedFns.begin(); it2 != clonedFns.end(); ++it2) {
      Function* clonedFn = *it2;

      // Estimate cloned function cost with the static profiler
      SFCP = &getAnalysis<StaticFunctionCostPass>(*clonedFn);
      clonedCost = SFCP->getFunctionCost();

      // Try to remove worthless clones
      if (clonedCost >= originalCost) {
        Regex noretend("\\.noret");
        bool isNoRetOpt = noretend.match(clonedFn->getName());
        if (substituteCallSites(originalFn, clonedFn, isNoRetOpt)) {
          ClonesRemoved++;
        }

        if (clonedFn->use_empty()) {
          clonedFn->dropAllReferences();
          clonedFn->eraseFromParent();
        }
      }
    }
    // Drop orphan functions
    if(originalFn->use_empty()) {
      originalFn->dropAllReferences();
      originalFn->eraseFromParent();
      OrphansDropped++;
    }
  }

  return ClonesRemoved > 0;
}

// Substitutes all call sites by the original function
bool ClonesDestroyer::substituteCallSites(Function *Fn, Function *Clone, bool isNoRetOpt = false) {

  // If the original and clone parameters don't match, this substitution
  // is not possible anymore due to previous optimizations.
  FunctionType *FnTy = Fn->getFunctionType();
  FunctionType *CloneTy = Clone->getFunctionType();

  if (FnTy->getNumParams() != CloneTy->getNumParams())
    return false;

  if (!isNoRetOpt && FnTy->getReturnType() != CloneTy->getReturnType())
    return false;

  for (unsigned i = 0, e = FnTy->getNumParams(); i != e; ++i) {
    Type *FnParamTy = FnTy->getParamType(i);
    Type *CloneParamTy = CloneTy->getParamType(i);
    if (FnParamTy != CloneParamTy)
      return false;
  }

  // Loop over all of the callers of the clone, transforming the call sites
  // to pass in the loaded pointers.
  std::vector<Value*> Args;
  int callsRestored = 0;
  while (!Clone->use_empty()) {
    CallSite CS(Clone->use_back());
    Instruction *Call = CS.getInstruction();

    // Reuse same arguments
    std::vector<Value*> Args(CS.arg_begin(), CS.arg_end());

    Instruction *New; // Create the new call or invoke instruction.
    if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
      New = InvokeInst::Create(Fn, II->getNormalDest(),
          II->getUnwindDest(), Args, "", Call);
      cast<InvokeInst>(New)->setCallingConv(II->getCallingConv());
      cast<InvokeInst>(New)->setAttributes(II->getAttributes());
    } else {
      CallInst *CI = dyn_cast<CallInst>(Call);
      New = CallInst::Create(Fn, Args, "", Call);
      if (CI->isTailCall())
        cast<CallInst>(New)->setTailCall();
      cast<CallInst>(New)->setCallingConv(CI->getCallingConv());
      cast<CallInst>(New)->setAttributes(CI->getAttributes());
    }
    Args.clear();

    if (!Call->use_empty())
      Call->replaceAllUsesWith(New);

    // Finally, remove the old call from the program, reducing the
    // use-count of the clone.
    Call->getParent()->getInstList().erase(Call);
    callsRestored++;
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
