#include <sstream>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>
#include <set>

#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/InstVisitor.h"
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
#define DEBUG_TYPE "function-fusion"
namespace llvm {
  STATISTIC(FunctionsCount,  "Number of functions");
  STATISTIC(FunctionsCloned, "Number of cloned functions");
  STATISTIC(ClonesCount,     "Number of functions that are clones");
  STATISTIC(CallsCount,      "Number of calls");
  STATISTIC(PromissorCalls,  "Number of promissor calls");
  STATISTIC(CallsReplaced,   "Number of replaced calls");
  STATISTIC(FunctionsFused,   "Number of functions fused");
  class FunctionFusion : public ModulePass, public InstVisitor<FunctionFusion> {

    std::set < CallSite* > toBeModified;
    std::map < std::pair < std::pair < Function*, Function* >, unsigned >, std::vector< std::pair<Instruction* ,Instruction*> > > functions2fuse;
    std::map < std::pair < std::pair < Function*, Function* >, unsigned >, int > functions2fuseHistogram;
    std::map < Function*, Function* > alwaysInlineFns;
    std::map < std::pair < std::pair < Function*, Function* >, unsigned >, Function*> clonedFunctions;

    Function* getAlwaysInlineFunction(Function *F);
    Function* cloneFunctionWithAlwaysInlineAttr(Function* Fn);
    bool isExternalFunctionCall(CallSite& CS);
    void selectToClone(CallSite& use, CallSite& definition);
    bool cloneFunctions();
    Function* fuseFunctions(Function* use, Function* definition, unsigned argPosition);
    void ReplaceCallSitesWithFusion(Function* fn, Instruction* use, Instruction* definition, unsigned argPosition);
   public:

    static char ID;

    FunctionFusion();
    bool runOnModule(Module &M);
    void visitCallSite(CallSite CS);
    virtual void print(raw_ostream& O, const Module* M) const;
  };
}
