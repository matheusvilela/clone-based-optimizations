//=== BranchPredictionPass.h - Interface to BranchPredictionPass-*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describe the branch prediction pass that calculates edges
// probabilities for all edges of a given function. The probabilities
// represents the likelihood of an edge to be taken.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STATIC_BRANCH_PREDICTION_PASS_H
#define LLVM_ANALYSIS_STATIC_BRANCH_PREDICTION_PASS_H

#include "BranchHeuristicsInfo.h"

#include "llvm/Pass.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Compiler.h"
#include <map>

namespace llvm {
  class BasicBlock;
  class DominatorTree;
  class LoopInfo;
  class BranchPredictionInfo;
  struct PostDominatorTree;

  /// BranchPredictionPass - This class implement the branch predictor proposed
  /// by Wu (1994). This class was designed as a function pass so it can be used
  /// without the hassle of calculating full program branch prediction. It
  /// inherits from EdgeProfileInfo to hold edges probabilities that ranges from
  /// values between 0.0 and 1.0. Therefore it does not require a double
  /// precision type.
  class BranchPredictionPass : public FunctionPass {
  public:
    // An edge is a relation between a pair of two basic blocks in which
    // the firsts defines the source block and the second the destination.
    // Since they will only be used to index the map, used it constified.
    typedef std::pair<const BasicBlock *, const BasicBlock *> Edge;
  private:
    // Passes that are required by this branch predictor.
    DominatorTree *DT;
    PostDominatorTree *PDT;
    LoopInfo *LI;

    // Hold required information for this pass.
    BranchPredictionInfo *BPI;
    BranchHeuristicsInfo *BHI;

    // Edge probability map.
    std::map<Edge, double> EdgeProbabilities;

    /// CalculateBranchProbabilities - Implementation of the algorithm proposed
    /// by Wu (1994) to calculate the probabilities of all the successors of a
    /// basic block.
    void CalculateBranchProbabilities(BasicBlock *BB);

    /// addEdgeProbability - If a heuristic matches, calculates the edge
    /// probability combining previous predictions acquired.
    void addEdgeProbability(BranchHeuristics heuristic, const BasicBlock *root,
                            Prediction pred);
  public:
    // Class identification, replacement for typeinfo.
    static char ID;

    BranchPredictionPass();
    ~BranchPredictionPass();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual const char *getPassName() const;
    virtual bool runOnFunction(Function &F);
    virtual void releaseMemory();
    void print(raw_ostream &O, const Module *M) const;

    /// getEdgeProbability - Find the edge probability based on the source and
    /// the destination basic block.  If the edge is not found, return 1.0
    /// (probability of 100% of being taken).
    double getEdgeProbability(const BasicBlock *src, const BasicBlock *dst)
      const;

    /// getEdgeProbability - Find the edge probability. If the edge is not
    /// found, return 1.0 (probability of 100% of being taken).
    double getEdgeProbability(Edge &edge) const;

    /// getInfo - Get branch prediction information regarding edges and blocks.
    const BranchPredictionInfo *getInfo() const;

    /// Clear - Empty all stored information.
    void Clear();

    typedef std::map<Edge, double>::const_iterator edge_iterator;

    /// edge_prob_begin - Constant begin iterator for edge probabilities.
    inline edge_iterator edge_prob_begin() const {
      return EdgeProbabilities.begin();
    }

    /// edge_prob_end - Constant end iterator for edge probabilities.
    inline edge_iterator edge_prob_end() const {
      return EdgeProbabilities.end();
    }
  };

} // End of llvm namespace.

#endif // LLVM_ANALYSIS_STATIC_BRANCH_PREDICTION_PASS_H

