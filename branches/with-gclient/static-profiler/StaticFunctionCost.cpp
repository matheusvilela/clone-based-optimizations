#include "StaticFunctionCost.h"
#include "BlockEdgeFrequencyPass.h"

using namespace llvm;

char StaticFunctionCostPass::ID = 0;

INITIALIZE_PASS(StaticFunctionCostPass, "static-function-cost",
                "Statically estimate a function cost based on basic block and edge frequencies", false, true)

FunctionPass *llvm::createStaticFunctionCostPassPass() {
  return new StaticFunctionCostPass;
}

void StaticFunctionCostPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BlockEdgeFrequencyPass>();
  AU.setPreservesAll();
}

bool StaticFunctionCostPass::runOnFunction(Function &F) {
  BEFP = &getAnalysis<BlockEdgeFrequencyPass>();

  cost = 0.0;
  for (Function::iterator it = F.begin(); it != F.end(); ++it) {
    BasicBlock* BB = it;
    double BB_freq = BEFP->getBlockFrequency(BB);
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
      cost += getInstructionCost(I) * BB_freq;
    }
  }
  return false;
}

void StaticFunctionCostPass::print(raw_ostream &O, const Module *M) const {
  std::stringstream output;
  output.unsetf ( std::ios::floatfield );
  output.precision(3);
  output << "cost = " << cost << std::endl;
  O << output.str();
}

double StaticFunctionCostPass::getInstructionCost(Instruction *I) const {
  return 1.0;
}

double StaticFunctionCostPass::getFunctionCost() const {
  return cost;
}
