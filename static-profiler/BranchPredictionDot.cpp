#include "BranchPredictionDot.h"
#include "BranchPredictionPass.h"

using namespace llvm;

void BranchPredictionDot::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BranchPredictionPass>();
  AU.addRequired<BranchProbabilityInfo>();
  AU.setPreservesAll();
}

bool BranchPredictionDot::runOnFunction(Function &F) {

  int BasicBlockID = 0;
  BPP = &getAnalysis<BranchPredictionPass>();
  BPI = &getAnalysis<BranchProbabilityInfo>();
  FunctionName = F.getName();
  for (Function::iterator it = F.begin(); it != F.end(); ++it) {
    BasicBlock* BB = it;

    // Assign numbers to BasicBlocks
    BasicBlockIDs[BB] = ++BasicBlockID;

    TerminatorInst* terminator = BB->getTerminator();
    if (terminator && terminator->getNumSuccessors() > 0) {
      unsigned numSuccessors = terminator->getNumSuccessors();
      for (unsigned i = 0; i < numSuccessors; ++i) {
        // Collect successors
        graph[BB].push_back(terminator->getSuccessor(i));
      }
    } else {
        // Insert BB on graph if it does not have any successor
        graph[BB];
    }
  }
  
  return false;
}

void BranchPredictionDot::print(raw_ostream& O, const Module *M) const {
  std::stringstream dotFileSS;
  dotFileSS << "digraph \""<< FunctionName <<"\" {" << std::endl;
  dotFileSS << "rankdir=LR;" << std::endl;

  printDot(dotFileSS, graph);

  dotFileSS << "}" << std::endl;
  O << dotFileSS.str();
}
 
void BranchPredictionDot::printDot(std::ostream& output, const CFG &graph) const {

  // Declare the vertices
  for (CFG::const_iterator it = graph.begin(); it != graph.end(); ++it) {

    BasicBlock* BB = it->first;
    output << "    " << BasicBlockIDs.at(BB) << " [label=\"";
    output << (std::string) BB->getName() << ":|";
    output << "\\l\\" << std::endl;

    // for(BasicBlock::const_iterator it2 = BB->begin(); it2 != BB->end(); ++it2) {
    //   if (const Instruction *inst = dyn_cast<Instruction>(it2)) {
    //     output << inst;
    //     output << "\\l\\" << std::endl;
    //   }
    // }
    
    std::string style = "shape=record";
    output << "\"," << style << "];" << std::endl;
  }

  // Print the Edges
  for (CFG::const_iterator it = graph.begin(); it != graph.end(); ++it) {
    std::vector<BasicBlock*> successors = it->second;
    int id1 = BasicBlockIDs.at(it->first);
    for (std::vector<BasicBlock*>::iterator it2 = successors.begin(); it2 != successors.end(); ++it2) {
      int id2     = BasicBlockIDs.at(*it2);

      output.unsetf ( std::ios::floatfield );
      output.precision(3);
      const BranchProbability llvm_prob = BPI->getEdgeProbability(it->first, *it2);
      double prob1 = BPP->getEdgeProbability(it->first, *it2);
      double prob2 = (double) llvm_prob.getNumerator() / llvm_prob.getDenominator();

      output << "    " << id1 << " -> " << id2 << "[ label=\"" << prob1 <<
      " , " << prob2 << "\" ];" << std::endl;
    }
  }
}

// Register the pass to the LLVM framework
char BranchPredictionDot::ID = 0;
static RegisterPass<BranchPredictionDot> X("branch-prediction-dot", "Print a dot file with branch prediction info.");
