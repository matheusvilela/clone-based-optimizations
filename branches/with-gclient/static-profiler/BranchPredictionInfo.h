//== BranchPredictionInfo.h - Auxiliary Info for Branch Predictor -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is an auxiliary class to the branch prediction pass. It is responsible
// to find all back edges and exit edges of a function. Moreover, it calculates
// which basic blocks contains calls and store instructions. This information
// will be used by the heuristics routines in order to calculate branch 
// probabilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BRANCH_PREDICTION_INFO_H
#define LLVM_ANALYSIS_BRANCH_PREDICTION_INFO_H

#include "llvm/ADT/SmallSet.h"
#include <map>

namespace llvm {
  class BasicBlock;
  class Function;
  class DominatorTree;
  class LoopInfo;
  struct PostDominatorTree;

  /// BranchPredictionInfo - Hold required information required to calculate
  /// branch prediction pass.
  class BranchPredictionInfo {
  public:
    // An edge is a relation between a pair of two basic blocks in which
    // the firsts defines the source block and the second the destination,
    // a successor block. Since they will only be used to index the map,
    // constify it. is required.
    typedef std::pair<const BasicBlock *, const BasicBlock *> Edge;
  private:
    // Required passes to calculate information.
    DominatorTree *DT;
    PostDominatorTree *PDT;
    LoopInfo *LI;

    // List of back edges and exit edges for functions.
    SmallSet<Edge, 128> listBackEdges, listExitEdges;

    // Count successors of a basic block are back edges.
    std::map<const BasicBlock *, unsigned> BackEdgesCount;

    // List of basic blocks that contains calls, stores and exits.
    SmallPtrSet<const BasicBlock *, 128> listCalls, listStores;

    /// FindBackAndExitEdges - Search for back and exit edges for all blocks
    /// within the function loops, calculated using loop information.
    void FindBackAndExitEdges(Function &F);

    /// FindCallsAndStores - Search for call and store instruction on basic blocks.
    void FindCallsAndStores(Function &F);
  public:
    explicit BranchPredictionInfo(DominatorTree *DT, LoopInfo *LI,
                                  PostDominatorTree *PDT = NULL);
    ~BranchPredictionInfo();

    /// BuildInfo - Build the list of back edges, exit edges, calls and stores.
    void BuildInfo(Function &F);

    /// Clear - Make the list of back edges, exit edges, calls and stores empty.
    void Clear();

    /// CountBackEdges - Given a basic block, count the number of successor
    /// that are back edges.
    unsigned CountBackEdges(BasicBlock *BB) const;

    /// CallsExit - Check whenever a basic block contains a call to exit.
    bool CallsExit(BasicBlock *BB) const;

    /// isBackEdge - Verify if an edge is a back edge.
    bool isBackEdge(const Edge &edge) const;

    /// isExitEdge - Verify if an edge is an exit edge.
    bool isExitEdge(const Edge &edge) const;

    /// hasCall - Verify if a basic block contains a call.
    bool hasCall(const BasicBlock *BB) const;

    /// hasStore - Verify if any instruction of a basic block is a store.
    bool hasStore(const BasicBlock *BB) const;

    inline DominatorTree *getDominatorTree() const { return DT; }
    inline PostDominatorTree *getPostDominatorTree() const { return PDT; }
    inline LoopInfo *getLoopInfo() const { return LI; }
  };
} // End of llvm namespace.

#endif // LLVM_ANALYSIS_BRANCH_PREDICTION_INFO_H
