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
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"


namespace llvm {

  class BlockEdgeFrequencyPass;

  class StaticFunctionCostPass : public FunctionPass {

   private:
    BlockEdgeFrequencyPass *BEFP;
    double cost;

    double getInstructionCost(Instruction *I) const;

   public:
    static char ID;

    StaticFunctionCostPass() : FunctionPass(ID) { }
    ~StaticFunctionCostPass() { }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual bool runOnFunction(Function &F);
    void print(raw_ostream &O, const Module *M) const;
    double getFunctionCost() const;
  };
}
