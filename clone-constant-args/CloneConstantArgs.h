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

namespace llvm {
  STATISTIC(FunctionsCloned, "Number of functions cloned.");
  STATISTIC(CallsReplaced, "Number of calls replaced.");
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
