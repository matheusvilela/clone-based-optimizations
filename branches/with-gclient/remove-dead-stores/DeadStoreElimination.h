#undef DEBUG_TYPE
#define DEBUG_TYPE "dead-store-elimination"
#include <sstream>
#include <string>
#include <iostream>
#include <set>
#include <queue>

#include "llvm/Pass.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "PADriver.h"

namespace llvm {
  STATISTIC(RemovedStores,   "Number of removed stores.");
  STATISTIC(FunctionsCount,  "Total number of functions.");
  STATISTIC(FunctionsCloned, "Number of functions cloned.");
  STATISTIC(ClonesCount,     "Number of functions that are clones.");
  STATISTIC(CallsCount,      "Total number of calls.");
  STATISTIC(PromissorCalls,  "Number of promissor calls.");
  STATISTIC(CallsReplaced,   "Number of calls replaced.");
 
  class DeadStoreEliminationPass : public ModulePass {

    // Successors and Predecessor basic blocks on the CFG
    std::map<BasicBlock*, std::vector<BasicBlock*> > successors;
    std::map<BasicBlock*, std::vector<BasicBlock*> > predecessors;

    // Functions that store on arguments
    std::map<Function*, std::set<Value*> > fnThatStoreOnArgs;

    // Positions that global vars points to
    std::set<int> globalPositions;

    std::map< Instruction*, std::set<Value*> > deadArguments;
    std::map<Function*, std::vector<Instruction*> > fn2Clone;

    PADriver* PAD;
    Function *currentFn;

   public:
    static char ID;

    // IN and OUT sets for the Dead Store Analysis
    std::map<const Instruction*, std::set<int> > inValues;
    std::map<const Instruction*, std::set<int> > outValues;

    DeadStoreEliminationPass();

    std::set<int> getRecursivePositions(int id);
    void getGlobalVarsInfo(Module &M);
    bool canBeRemoved(Value* ptr, Instruction* inst, Function &F, bool verifyArgs);
    bool cloneFunctions();
    Function* cloneFunctionWithoutDeadStore(Function *Fn, Instruction* caller, std::string suffix);
    void replaceCallingInst(Instruction* caller, Function* fn);
    void getFnThatStoreOnArgs(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    bool runOnModule(Module &M);
    void runDeadStoreAnalysis(Function &F);
    bool removeDeadStores(Function &F);
    bool analyzeBasicBlock(BasicBlock &BB);
    void print(raw_ostream &O, const Module *M) const;
    void printAnalysis(raw_ostream &O) const;
    void printSet(raw_ostream &O, const std::set<int> &myset) const;
  };
}
