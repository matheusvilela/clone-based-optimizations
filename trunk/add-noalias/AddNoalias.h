#include <sstream>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>
#include <set>

#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryBuiltins.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "add-noalias"
namespace llvm {
  STATISTIC(NoAliasPotentialFunctions, "Number of functions");
  STATISTIC(NoAliasClonedFunctions,    "Number of cloned functions");
  STATISTIC(NoAliasTotalCalls,         "Number of calls");
  STATISTIC(NoAliasPotentialCalls,     "Number of promissor calls");
  STATISTIC(NoAliasClonedCalls,        "Number of replaced calls");

  class AddNoalias : public ModulePass {

    // store functions arguments to inspect later
    std::map< User*, std::vector< std::pair<Argument*, Value*> > > arguments;
    std::map< Function*, std::vector<User*> > fn2Clone;

    AliasAnalysis *AA;
    void collectFn2Clone();
    bool cloneFunctions();
    void fillCloneContent(Function* original, Function* clonedFn);
    void substCallingInstructions(Function* NF, std::vector<User*> callers);
    Function* cloneFunctionWithNoAliasArgs(Function *Fn);

   public:

    static char ID;

    AddNoalias();
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    bool runOnModule(Module &M);
    virtual void print(raw_ostream& O, const Module* M) const;
  };
}

