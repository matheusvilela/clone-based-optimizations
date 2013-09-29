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
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/Cloning.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "clone-constant-args"
namespace llvm {
  STATISTIC(FunctionsCount,  "Number of functions");
  STATISTIC(FunctionsCloned, "Number of cloned functions");
  STATISTIC(ClonesCount,     "Number of functions that are clones");
  STATISTIC(CallsCount,      "Number of calls");
  STATISTIC(PromissorCalls,  "Number of promissor calls");
  STATISTIC(CallsReplaced,   "Number of replaced calls");
  class CloneConstantArgs : public ModulePass {

    std::map< User*, std::vector< std::pair<Argument*, Value*> > > arguments;
    std::map< Function*, std::vector <User*> > fn2Clone;


    void findConstantArgs(Module &M);
    bool cloneFunctions();
    void collectFn2Clone();
    Function* cloneFunctionWithConstArgs(Function *Fn, User* caller, std::string suffix);
    void replaceCallingInst(User* caller, Function* fn);

   public:

    static char ID;

    CloneConstantArgs();
    bool runOnModule(Module &M);
    virtual void print(raw_ostream& O, const Module* M) const;
  };
}
