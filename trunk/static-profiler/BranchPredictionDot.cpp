#include "BranchPredictionDot.h"
#include "BranchPredictionPass.h"

using namespace llvm;

void BranchPredictionDot::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<BranchPredictionPass>();
  AU.setPreservesAll();
}

bool BranchPredictionDot::runOnFunction(Function &F) {

  int BasicBlockID = 0;
  BPP = &getAnalysis<BranchPredictionPass>();
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
  O << "digraph \""<< FunctionName <<"\" {\n";
  O << "rankdir=LR;\n";

  printDot(O, graph);

  O << "}\n";
}
 
void BranchPredictionDot::printDot(raw_ostream& output, const CFG &graph) const {

  // Declare the vertices
  for (CFG::const_iterator it = graph.begin(); it != graph.end(); ++it) {

    BasicBlock* BB = it->first;
    output << "    " << BasicBlockIDs.at(BB) << " [label=\"";
    output << (std::string) BB->getName() << ":|";
    output << "\\l\\\n";

    for(BasicBlock::const_iterator it2 = BB->begin(); it2 != BB->end(); ++it2) {
      if (const Instruction *inst = dyn_cast<Instruction>(it2)) {
        inst->print(output);
        output << "\\l\\\n";
      }
    }
    
    std::string style = "shape=record";
    output << "\"," << style << "];\n";
  }

  // Print the Edges
  for (CFG::const_iterator it = graph.begin(); it != graph.end(); ++it) {
    std::vector<BasicBlock*> successors = it->second;
    int id1 = BasicBlockIDs.at(it->first);
    for (std::vector<BasicBlock*>::iterator it2 = successors.begin(); it2 != successors.end(); ++it2) {
      int id2     = BasicBlockIDs.at(*it2);

      std::stringstream str;
      str.unsetf ( std::ios::floatfield );
      str.precision(3);
      double prob = BPP->getEdgeProbability(it->first, *it2);

      str << "    " << id1 << " -> " << id2 << "[ label=\"" << prob 
        << "\"];\n";
      output << str.str();
    }
  }
}

// Register the pass to the LLVM framework
char BranchPredictionDot::ID = 0;
static RegisterPass<BranchPredictionDot> X("branch-prediction-dot", "Print a dot file with branch prediction info.");
