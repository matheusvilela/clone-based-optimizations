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
#include "PADriver.h"

namespace llvm {
  STATISTIC(RemovedStores, "Counts number of remove stores.");
  class DeadStoreEliminationPass : public FunctionPass {

    // Successors and Predecessor basic blocks on the CFG
    std::map<BasicBlock*, std::vector<BasicBlock*> > successors;
    std::map<BasicBlock*, std::vector<BasicBlock*> > predecessors;

    PADriver* PAD;
    Function *currentFn;

   public:
    static char ID;

    // IN and OUT sets for the Dead Store Analysis
    std::map<const Instruction*, std::set<int> > inValues;
    std::map<const Instruction*, std::set<int> > outValues;

    DeadStoreEliminationPass();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    bool runOnFunction(Function &F);
    bool removeDeadStores(BasicBlock &BB);
    bool analyzeBasicBlock(BasicBlock &BB);
    void print(raw_ostream &O, const Module *M) const;
    void printAnalysis(raw_ostream &O) const;
    void printSet(raw_ostream &O, const std::set<int> &myset) const;
  };
}
