//===- BlockEdgeFrequencyPass.cpp - Calculates Block and Edge Frequencies -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass calculates basic block and edge frequencies based on the branch
// probability calculated previously. To calculate the block frequency, sum all
// predecessors edges reaching the block. If the block is the function entry
// block define the execution frequency of 1. To calculate edge frequencies,
// multiply the block frequency by the edge from this block to it's successors.
//
// To avoid cyclic problem, this algorithm propagate frequencies to edges by
// calculating a cyclic probability. More information can be found in Wu (1994).
//
// References:
// Youfeng Wu and James R. Larus. Static branch frequency and program profile
// analysis. In MICRO 27: Proceedings of the 27th annual international symposium
// on Microarchitecture. IEEE, 1994.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "block-edge-frequency"

#include "BlockEdgeFrequencyPass.h"
#include "BranchPredictionInfo.h"
#include "BranchPredictionPass.h"

#include "llvm/Pass.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>
#include <algorithm>

using namespace llvm;

char BlockEdgeFrequencyPass::ID = 0;
const double BlockEdgeFrequencyPass::epsilon = 0.000001;

static RegisterPass<BlockEdgeFrequencyPass> X("block-edge-frequency",
                "Statically estimate basic block and edge frequencies", false, true);


// FunctionPass *llvm::createBlockEdgeFrequencyPass() {
//   return new BlockEdgeFrequencyPass();
// }

BlockEdgeFrequencyPass::BlockEdgeFrequencyPass() : FunctionPass(ID) {
}

BlockEdgeFrequencyPass::~BlockEdgeFrequencyPass() {
}

void BlockEdgeFrequencyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfo>();
  AU.addRequired<BranchPredictionPass>();
  AU.setPreservesAll();
}

const char *BlockEdgeFrequencyPass::getPassName() const {
  return "Block and Edge Frequency Pass - Statically estimate basic block and " \
         "edge frequencies";
}

bool BlockEdgeFrequencyPass::runOnFunction(Function &F) {
  LI = &getAnalysis<LoopInfo>();
  BPP = &getAnalysis<BranchPredictionPass>();

  // Clear previously calculated data.
  Clear();

  // Some debug output.
  DEBUG(errs() << "========== Block Edge Frequency Pass ------------" << "\n");
  DEBUG(errs() << "Function: " << F.getName() << "\n");

  // Find all loop headers of this function.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ++FI) {
    BasicBlock *BB = FI;

    // If it is a loop head, add it to the list.
    if (LI->isLoopHeader(BB))
      PropagateLoop(LI->getLoopFor(BB));
  }

  // After calculating frequencies for all the loops, calculate the frequencies
  // for the remaining blocks by faking a loop for the function. Assuming that
  // the entry block of the function is a loop head, propagate frequencies.

  // Propagate frequencies assuming entry block is a loop head.
  BasicBlock *entry = F.begin();
  MarkReachable(entry);

  DEBUG(errs() << "  Processing Fake Loop: " << entry->getName() << "\n");
  PropagateFreq(entry);

  // Verify frequency integrity.
  DEBUG(VerifyIntegrity(F) ? (errs() << "    No integrity error\n") :
                             (errs() << "    Unable to calculate correct local " \
                                        "block/edge frequencies for function: "
                                     << F.getName() << "\n"));

  // Clean up unnecessary information.
  NotVisited.clear();
  LoopsVisited.clear();
  BackEdgeProbabilities.clear();

  return false;
}

void BlockEdgeFrequencyPass::releaseMemory() {
  Clear();
}

void BlockEdgeFrequencyPass::print(raw_ostream &O, const Module *M) const {

  O << "\n\n---- Block Freqs ----\n";
  for (std::map<const BasicBlock *, double>::const_iterator it =
      BlockFrequencies.begin(); it != BlockFrequencies.end(); ++it) {

    const  BasicBlock* BB = it->first;
    double frequency      = it->second;
    O << "  " << BB->getName() << " = " << format("%.3f", frequency)
      << "\n";

    /*
    // Print the edges frequencies for all successor of this block.
    const TerminatorInst *TI = BB->getTerminator();
    for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
      BasicBlock *successor = TI->getSuccessor(s);
      Edge edge = std::make_pair(BB, successor);

      // Print the edge frequency for debugging purposes.
      O << "   " << BB->getName() << " -> " << successor->getName()
                   << " = " << format("%.3f", EdgeFrequencies.at(edge)) << "\n";
    }
    */
  }
}

/// getEdgeFrequency - Find the edge frequency based on the source and
/// the destination basic block.  If the edge is not found, return a
/// default value.
double BlockEdgeFrequencyPass::getEdgeFrequency(const BasicBlock *src,
                                                const BasicBlock *dst) const {
  // Create the edge.
  Edge edge = std::make_pair(src, dst);

  // Find the profile based on the edge.
  return getEdgeFrequency(edge);
}

/// getEdgeFrequency - Find the edge frequency based on the edge. If the
/// edge is not found, return a default value.
double BlockEdgeFrequencyPass::getEdgeFrequency(Edge &edge) const {
  // Search for the edge on the list.
  std::map<Edge, double>::const_iterator I = EdgeFrequencies.find(edge);
  return I != EdgeFrequencies.end() ? I->second : 0.0;
}

/// getBlockFrequency - Find the basic block frequency based on the edge.
/// If the basic block is not present, return a default value.
double BlockEdgeFrequencyPass::getBlockFrequency(const BasicBlock *BB) const {
  // Search for the block on the list.
  std::map<const BasicBlock *, double>::const_iterator I =
      BlockFrequencies.find(BB);
  return I != BlockFrequencies.end() ? I->second : 0.0;
}

/// getBackEdgeProbabilities - Get updated probability of back edge. In case
/// of not found, get the edge probability from the branch prediction.
double BlockEdgeFrequencyPass::getBackEdgeProbabilities(Edge &edge) {
  // Search for the back edge on the list. In case of not found, search on the
  // edge frequency list.
  std::map<Edge, double>::const_iterator I = BackEdgeProbabilities.find(edge);
  return I != BackEdgeProbabilities.end() ?
                  I->second : BPP->getEdgeProbability(edge);
}

/// MarkReachable - Mark all blocks reachable from root block as not visited.
void BlockEdgeFrequencyPass::MarkReachable(BasicBlock *root) {
  // Clear the list first.
  NotVisited.clear();

  // Use an artificial stack.
  SmallVector<BasicBlock *, 16> stack;
  stack.push_back(root);

  // Visit all childs marking them as visited in depth-first order.
  while (!stack.empty()) {
    BasicBlock *BB = stack.pop_back_val();
    if (!NotVisited.insert(BB))
      continue;

    // Put the new successors into the stack.
    TerminatorInst *TI = BB->getTerminator();
    for (unsigned s = 0; s < TI->getNumSuccessors(); ++s)
      stack.push_back(TI->getSuccessor(s));
  }
}

/// PropagateLoop - Propagate frequencies from the inner most loop to the
/// outer most loop.
void BlockEdgeFrequencyPass::PropagateLoop(const Loop *loop) {
  // Check if we already processed this loop.
  if (LoopsVisited.count(loop))
    return;

  // Mark the loop as visited.
  LoopsVisited.insert(loop);

  // Find the most inner loops and process them first.
  for (Loop::iterator LIT = loop->begin(), LIE = loop->end();
       LIT != LIE; ++LIT) {
    const Loop *inner = *LIT;
    PropagateLoop(inner);
  }

  // Find the header.
  BasicBlock *head = loop->getHeader();

  // Mark as not visited all blocks reachable from the loop head.
  MarkReachable(head);

  // Propagate frequencies from the loop head.
  DEBUG(errs() << "  Processing Loop: " << head->getName() << "\n");
  PropagateFreq(head);
}

/// PropagateFreq - Compute basic block and edge frequencies by propagating
/// frequencies.
void BlockEdgeFrequencyPass::PropagateFreq(BasicBlock *head) {
  const BranchPredictionInfo *info = BPP->getInfo();

  // Use an artificial stack to avoid recursive calls to PropagateFreq.
  std::vector<BasicBlock *> stack;
  stack.push_back(head);

  do {
    // Get the current basic block.
    BasicBlock *BB = stack.back();
    stack.pop_back();

    // Debug information.
    DEBUG(errs() << "  PropagateFreq: " << BB->getName() << ", "
                 << head->getName() << "\n");

    // If BB has been visited.
    if (!NotVisited.count(BB))
      continue;

    // Define the block frequency. If it's a loop head, assume it executes only
    // once.
    BlockFrequencies[BB] = 1.0;

    // If it is not a loop head, calculate the block frequencies by summing all
    // edge frequencies reaching this block. If it contains back edges, take
    // into consideration the cyclic probability.
    if (BB != head) {
      // We can't calculate the block frequency if there is a back edge still
      // not calculated.
      bool InvalidEdge = false;
      for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
           PI != PE; ++PI) {
        BasicBlock *pred = *PI;
        if (NotVisited.count(pred) &&
            !info->isBackEdge(std::make_pair(pred, BB))) {
          InvalidEdge = true;
          break;
        }
      }

      // There is an unprocessed predecessor edge.
      if (InvalidEdge)
        continue;

      // Sum the incoming frequencies edges for this block. Updated
      // the cyclic probability for back edges predecessors.
      double bfreq = 0.0;
      double cyclic_probability = 0.0;

      // Verify if BB is a loop head.
      bool loop_head = LI->isLoopHeader(BB);

      // Calculate the block frequency and the cyclic_probability in case
      // of back edges using the sum of their predecessor's edge frequencies.
      for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
           PI != PE; ++PI) {
        BasicBlock *pred = *PI;

        Edge edge = std::make_pair(pred, BB);
        if (info->isBackEdge(edge) && loop_head)
          cyclic_probability += getBackEdgeProbabilities(edge);
        else
          bfreq += EdgeFrequencies[edge];
      }

      // For loops that seems not to terminate, the cyclic probability can be
      // higher than 1.0. In this case, limit the cyclic probability below 1.0.
      if (cyclic_probability > (1.0 - epsilon))
        cyclic_probability = 1.0 - epsilon;

      // Calculate the block frequency.
      BlockFrequencies[BB] = bfreq / (1.0 - cyclic_probability);
    }

    // Print the block frequency for debugging purposes.
    DEBUG(errs() << "    [" << BB->getName() << "]: "
                 << format("%.3f", BlockFrequencies[BB]) << "\n");

    // Mark the block as visited.
    NotVisited.erase(BB);

    // Calculate the edges frequencies for all successor of this block.
    TerminatorInst *TI = BB->getTerminator();
    for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
      BasicBlock *successor = TI->getSuccessor(s);
      Edge edge = std::make_pair(BB, successor);
      double prob = BPP->getEdgeProbability(edge);

      // The edge frequency is the probability of this edge times the block
      // frequency.
      double efreq = prob * BlockFrequencies[BB];
      EdgeFrequencies[edge] = efreq;

      // If a successor is the loop head, update back edge probability.
      if (successor == head)
        BackEdgeProbabilities[edge] = efreq;

      // Print the edge frequency for debugging purposes.
      DEBUG(errs() << "      " << BB->getName() << "->" << successor->getName()
                   << ": " << format("%.3f", EdgeFrequencies[edge]) << "\n");
    }

    // Propagate frequencies for all successor that are not back edges.
    SmallVector<BasicBlock *, 64> backedges;
    for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
      BasicBlock *successor = TI->getSuccessor(s);
      Edge edge = std::make_pair(BB, successor);
      if (!info->isBackEdge(edge))
        backedges.push_back(successor);
    }

    // This was done just to ensure that the algorithm would process the
    // left-most child before, to simulate normal PropagateFreq recursive calls.
    SmallVector<BasicBlock *, 64>::reverse_iterator RI, RE;
    for (RI = backedges.rbegin(), RE = backedges.rend(); RI != RE; ++RI)
      stack.push_back(*RI);
  } while (!stack.empty());
}

/// Clear - Clear all stored information.
void BlockEdgeFrequencyPass::Clear() {
  NotVisited.clear();
  LoopsVisited.clear();
  BackEdgeProbabilities.clear();
  EdgeFrequencies.clear();
  BlockFrequencies.clear();
}

/// VerifyIntegrity - The sum of frequencies of all edges leading to
/// terminal nodes should match the entry frequency (that is always 1.0).
bool BlockEdgeFrequencyPass::VerifyIntegrity(Function &F) const {
  // The sum of all predecessors edge frequencies.
  double freq = 0.0;

  // If the function has only one block, then the input frequency matches
  // automatically the output frequency.
  if (F.size() == 1)
    return true;

  // Find all terminator nodes.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ++FI) {
    BasicBlock *BB = FI;

    // If the basic block has no successors, then it s a termination node.
    TerminatorInst *TI = BB->getTerminator();
    if (!TI->getNumSuccessors()) {
      // Find all predecessor edges leading to BB.
      for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
           PI != PE; ++PI) {
        BasicBlock *pred = *PI;

        // Sum the predecessors edge frequency.
        freq += getEdgeFrequency(pred, BB);
      }
    }
  }

  DEBUG(errs() << "  Predecessor's outgoing edge frequency sum: "
               << format("%.3f", freq) << "\n");

  // Check if frequency matches 1.0 (with a 0.01 slack).
  return (freq > 0.99 && freq < 1.01);
}
