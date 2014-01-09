//=== BranchPredictionPass.cpp - LLVM Pass to Predict Branch Probabilities ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements the branch predictor proposed by Wu (1994) that
// estimates, in compilation time, branch probabilities. It calculates edge
// probabilities for all the branches in a CFG for a function. The algorithm
// takes into consideration the heuristics proposed by Ball (1993) to make the
// predictions.
//
// References:
// Ball, T. and Larus, J. R. 1993. Branch prediction for free. In Proceedings of
// the ACM SIGPLAN 1993 Conference on Programming Language Design and
// Implementation (Albuquerque, New Mexico, United States, June 21 - 25, 1993).
// R. Cartwright, Ed. PLDI '93. ACM, New York, NY, 300-313.
//
// Youfeng Wu and James R. Larus. Static branch frequency and program profile
// analysis. In MICRO 27: Proceedings of the 27th annual international symposium
// on Microarchitecture. IEEE, 1994.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "branch-prediction"

#include "BranchPredictionPass.h"
#include "BranchPredictionInfo.h"
#include "BranchHeuristicsInfo.h"

#include "llvm/Pass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

char BranchPredictionPass::ID = 0;

static RegisterPass<BranchPredictionPass> X("branch-prediction",
                "Predict branch probabilities", false, true);

// FunctionPass *llvm::createBranchPredictionPass() {
//   return new BranchPredictionPass();
// }

BranchPredictionPass::BranchPredictionPass() : FunctionPass(ID) {
  BPI = NULL;
  BHI = NULL;
}

BranchPredictionPass::~BranchPredictionPass() {
  Clear();
}

void BranchPredictionPass::print(raw_ostream &O, const Module *M) const {

  O << "---- Branch Probabilities ----\n";
  for(std::map<Edge, double>::const_iterator it = EdgeProbabilities.begin();
      it != EdgeProbabilities.end(); ++it) {

    Edge edge = it->first;
    double probability = it->second;
    const BasicBlock* BB = edge.first;
    const BasicBlock* succ = edge.second;

    O << "  edge " << BB->getName() << " -> " << succ->getName()
      << " probability is " << format("%.3f", probability*100)
      << "%\n";
  }
}

void BranchPredictionPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTree>();
  AU.addRequired<PostDominatorTree>();
  AU.addRequired<LoopInfo>();
  AU.setPreservesAll();
}

const char *BranchPredictionPass::getPassName() const {
  return "Branch Prediction Pass - Predict branch successors probabilities";
}

bool BranchPredictionPass::runOnFunction(Function &F) {
  // To perform the branch prediction, the following passes are required.
  DT =  &getAnalysis<DominatorTree>();
  PDT = &getAnalysis<PostDominatorTree>();
  LI =  &getAnalysis<LoopInfo>();

  // Some debug output.
  DEBUG(errs() << "=========== Branch Prediction Pass --------------" << "\n");
  DEBUG(errs() << "Function: " << F.getName() << "\n");

  // Clear previously calculated data.
  Clear();

  // Build all required information to run the branch prediction pass.
  BPI = new BranchPredictionInfo(DT, LI, PDT);
  BPI->BuildInfo(F);

  // Create the class to check branch heuristics.
  BHI = new BranchHeuristicsInfo(BPI);

  // Run over all basic blocks of a function calculating branch probabilities.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ++FI)
    // Calculate the likelihood of the successors of this basic block.
    CalculateBranchProbabilities(FI);

  // Delete unnecessary branch heuristic info.
  delete BHI;
  BHI = NULL;

  return false;
}

void BranchPredictionPass::releaseMemory() {
  Clear();
}

/// getEdgeProbability - Find the edge probability based on the source and
/// the destination basic block.  If the edge is not found, return 1.0
/// (probability of 100% of being taken).
double BranchPredictionPass::getEdgeProbability(const BasicBlock *src,
                                                const BasicBlock *dst) const {
  // Create the edge.
  Edge edge = std::make_pair(src, dst);

  // Find the profile based on the edge.
  return getEdgeProbability(edge);
}

/// getEdgeProbability - Find the edge probability. If the edge is not found,
/// return 1.0 (probability of 100% of being taken).
double BranchPredictionPass::getEdgeProbability(Edge &edge) const {
  // Search for the edge on the list.
  std::map<Edge, double>::const_iterator I = EdgeProbabilities.find(edge);

  // If edge was found, return it. Otherwise return the default value,
  // meaning that there is no profile known for this edge. The default value
  // is 1.0, meaning that the branch is taken with 100% likelihood.
  return I != EdgeProbabilities.end() ? I->second : 1.0;
}

/// getInfo - Get branch prediction information regarding edges and blocks.
const BranchPredictionInfo *BranchPredictionPass::getInfo() const {
  return BPI;
}

/// Clear - Empty all stored information.
void BranchPredictionPass::Clear() {
  // Clear edge probabilities.
  EdgeProbabilities.clear();

  // Free previously calculated branch prediction info class.
  if (BPI) {
    delete BPI;
    BPI = NULL;
  }

  // Free previously calculated branch heuristics class.
  if (BHI) {
    delete BHI;
    BHI = NULL;
  }
}

/// CalculateBranchProbabilities - Implementation of the algorithm proposed
/// by Wu (1994) to calculate the probabilities of all the successors of a
/// basic block.
void BranchPredictionPass::CalculateBranchProbabilities(BasicBlock *BB) {
  // Obtain the last instruction.
  TerminatorInst *TI = BB->getTerminator();

  // Find the total number of successors (variable "m" in Wu's paper)
  unsigned successors = TI->getNumSuccessors();

  // Find the total number of back edges (variable "n" in Wu's paper)
  unsigned backedges = BPI->CountBackEdges(BB);

  // Some debug output.
  DEBUG(errs() << "  Basic Block: " << BB->getName() << "\n");

  // The basic block must have successors,
  // so that we can have something to profile
  if (successors != 0) {
    // If a block calls exit, then assume that every successor of this
    // basic block is never going to be reached.
    if (BPI->CallsExit(BB)) {
      // According to the paper, successors that contains an exit call have a
      // probability of 0% to be taken.
      for (unsigned s = 0; s < successors; ++s) {
        BasicBlock *succ = TI->getSuccessor(s);
        Edge edge = std::make_pair(BB, succ);
        EdgeProbabilities[edge] = 0.0f;
        DEBUG(errs() << "    " << BB->getName() << "->" << succ->getName()
                     << ": " << format("%.3f", EdgeProbabilities[edge])
                     << "\n");
      }
    } else if (backedges > 0 && backedges < successors) {
      // Has some back edges, but not all.
      for (unsigned s = 0; s < successors; ++s) {
        BasicBlock *succ = TI->getSuccessor(s);
        Edge edge = std::make_pair(BB, succ);

        // Check if edge is a backedge.
        if (BPI->isBackEdge(edge)) {
          EdgeProbabilities[edge] =
              BHI->getProbabilityTaken(LOOP_BRANCH_HEURISTIC) / backedges;

          DEBUG(errs() << "    " << BB->getName() << "->" << succ->getName()
                       << ": " << format("%.3f", EdgeProbabilities[edge])
                       << "\n");
        } else {
          // The other edge, the one that is not a back edge, is in most cases
          // an exit edge. However, there are situations in which this edge is
          // an exit edge of an inner loop, but not for the outer loop. So,
          // consider the other edges always as an exit edge.

          EdgeProbabilities[edge] =
              BHI->getProbabilityNotTaken(LOOP_BRANCH_HEURISTIC) /
              (successors - backedges);

          DEBUG(errs() << "    " << BB->getName() << "->" << succ->getName()
                       << ": " << format("%.3f", EdgeProbabilities[edge])
                       << "\n");
        }
      }
    } else if (backedges > 0 || successors != 2) {
      // This part handles the situation involving switch statements.
      // Every switch case has a equal likelihood to be taken.
      // Calculates the probability given the total amount of cases clauses.
      for (unsigned s = 0; s < successors; ++s) {
        BasicBlock *succ = TI->getSuccessor(s);
        Edge edge = std::make_pair(BB, succ);
        EdgeProbabilities[edge] = 1.0f / successors;

        DEBUG(errs() << "    " << BB->getName() << "->" << succ->getName()
                     << ": " << format("%.3f", EdgeProbabilities[edge])
                     << "\n");
      }
    } else {
      // Here we can only handle basic blocks with two successors (branches).
      // This assertion might never occur due to conditions meet above.
      assert(successors == 2 && "Expected a two way branch");

      // Identify the two branch edges.
      Edge trueEdge = std::make_pair(BB, TI->getSuccessor(0));
      Edge falseEdge = std::make_pair(BB, TI->getSuccessor(1));

      // Initial branch probability. If no heuristic matches, than each edge
      // has a likelihood of 50% to be taken.
      EdgeProbabilities[trueEdge] = 0.5f;
      EdgeProbabilities[falseEdge] = 0.5f;

      // Run over all heuristics implemented in BranchHeuristics class.
      for (unsigned h = 0; h < BHI->getNumHeuristics(); ++h) {
        // Retrieve the next heuristic.
        BranchHeuristics heuristic = BHI->getHeuristic(h);

        // If the heuristic matched, add the edge probability to it.
        Prediction pred = BHI->MatchHeuristic(heuristic, BB);

        // Heuristic matched.
        if (pred.first)
          // Recalculate edge probability.
          addEdgeProbability(heuristic, BB, pred);
      }

      DEBUG(errs() << "    " << trueEdge.first->getName() << "->"
                   << trueEdge.second->getName() << ": "
                   << format("%.3f", EdgeProbabilities[trueEdge]) << "\n");

      DEBUG(errs() << "    " << falseEdge.first->getName() << "->"
                   << falseEdge.second->getName() << ": "
                   << format("%.3f", EdgeProbabilities[falseEdge]) << "\n");
    }
  }
}

/// addEdgeProbability - If a heuristic matches, calculates the edge probability
/// combining previous predictions acquired.
void BranchPredictionPass::addEdgeProbability(BranchHeuristics heuristic,
                                              const BasicBlock *root,
                                              Prediction pred) {
  const BasicBlock *successorTaken = pred.first;
  const BasicBlock *successorNotTaken = pred.second;

  // Show which heuristic matched
  DEBUG(errs() << "    " << BHI->getHeuristicName(heuristic) << " Matched: ("
               << successorTaken->getName() << " ; "
               << successorNotTaken->getName() << ")\n");

  // Get the edges.
  Edge edgeTaken = std::make_pair(root, successorTaken);
  Edge edgeNotTaken = std::make_pair(root, successorNotTaken);

  // The new probability of those edges.
  double probTaken = BHI->getProbabilityTaken(heuristic);
  double probNotTaken = BHI->getProbabilityNotTaken(heuristic);

  // The old probability of those edges.
  double oldProbTaken    = getEdgeProbability(edgeTaken);
  double oldProbNotTaken = getEdgeProbability(edgeNotTaken);

  // Combined the newly matched heuristic with the already given
  // probability of an edge. Uses the Dempster-Shafer theory to combine
  // probability of two events to occur simultaneously.
  double d = oldProbTaken    * probTaken +
             oldProbNotTaken * probNotTaken;

  EdgeProbabilities[edgeTaken] = oldProbTaken * probTaken / d;
  EdgeProbabilities[edgeNotTaken] = oldProbNotTaken * probNotTaken / d;
}
