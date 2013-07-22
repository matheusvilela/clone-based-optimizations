#include <sstream>
#include <string>
#include <iostream>

#include "llvm/Pass.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"


namespace llvm {

  class BranchPredictionPass;
  class BranchProbabilityInfo;
  class BranchPredictionDot : public FunctionPass {

    typedef std::map<BasicBlock*, std::vector<BasicBlock*> > CFG;
    CFG graph;
    std::map<BasicBlock*, int> BasicBlockIDs;
    std::string FunctionName;
    BranchPredictionPass *BPP;

   public:

    static char ID;
    BranchPredictionDot() : FunctionPass(ID) { }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    bool runOnFunction(Function &F);
    void print(raw_ostream &O, const Module *M) const;

   private:

    void printDot(raw_ostream& output, const CFG& graph) const;

  };
}

