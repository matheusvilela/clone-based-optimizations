#include <sstream>
#include <unistd.h>
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
#include "../alias-analysis/PADriver/PADriver.h"

namespace llvm {
  STATISTIC(NoAliasPotentialFunctions, "Counts number of functions");
  STATISTIC(NoAliasClonedFunctions,    "Counts number of cloned functions");
  STATISTIC(NoAliasPotentialCalls,     "Counts number of replaceable calls");
  STATISTIC(NoAliasClonedCalls,        "Counts number of replaced calls");
  STATISTIC(NoAliasTotalCalls,         "Counts number of calls");

  class AddNoalias : public ModulePass {

    // store functions arguments to inspect later
    std::map< User*, std::vector< std::pair<Argument*, Value*> > > arguments;
    std::map< Function*, std::vector<User*> > fn2Clone;

    PADriver* PAD;
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

