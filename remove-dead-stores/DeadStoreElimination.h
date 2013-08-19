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
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "PADriver.h"

namespace llvm {
  STATISTIC(RemovedStores, "Number of removed stores.");
  class DeadStoreEliminationPass : public ModulePass {

    // Successors and Predecessor basic blocks on the CFG
    std::map<BasicBlock*, std::vector<BasicBlock*> > successors;
    std::map<BasicBlock*, std::vector<BasicBlock*> > predecessors;

    //TODO:
    // Functions that store on params and can potentially be optimized
    // std::map<Function*, std::set<Value*> > functionsThatStoreOnParams;
    // * Run intraprocedural dead store elimination. While doing this:
    //   - Get function that store on args and these args
    //   - If a store on an arg cannot be removed by rule 3,
    //     remove it from the args that are stored
    // * Take the possible stores out of the call.
    // * Run intraprocedural dead store elimination
    //   + If an artificial store can be removed, then:
    //     - Clone the function, removing the store
    //     - Change the call

    //PADriver* PAD;
    AliasAnalysis *AA;
    Function *currentFn;

   public:
    static char ID;

    // IN and OUT sets for the Dead Store Analysis
    std::map<const Instruction*, AliasSetTracker* > inValues;
    std::map<const Instruction*, AliasSetTracker* > outValues;

    DeadStoreEliminationPass();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    bool runOnModule(Module &M);
    bool runOnFunction(Function &F);
    bool removeDeadStores(BasicBlock &BB, Function &F);
    bool analyzeBasicBlock(BasicBlock &BB);
    void print(raw_ostream &O, const Module *M) const;
    void printAnalysis(raw_ostream &O) const;
    void printSet(raw_ostream &O, AliasSetTracker &myset) const;
  };
}
