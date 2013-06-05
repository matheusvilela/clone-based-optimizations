//===--- BlockEdgeFrequencyPass.h - Block and Edge Frequencies -*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describe the interface to the pass that calculates blocks and edge
// frequencies for a function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BLOCK_EDGE_FREQUENCY_PASS_H
#define LLVM_ANALYSIS_BLOCK_EDGE_FREQUENCY_PASS_H

#include "llvm/Pass.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Compiler.h"
#include <map>

namespace llvm {

  class BasicBlock;
  class Loop;
  class LoopInfo;
  class BranchPredictionPass;

  class BlockEdgeFrequencyPass : public FunctionPass {
  public:
    // An edge is a relation between a pair of two basic blocks in which
    // the firsts defines the source block and the second the destination.
    // Since they will only be used to index the map, used it constified.
    typedef std::pair<const BasicBlock *, const BasicBlock *> Edge;
  private:
    // For loops that does not terminates, the cyclic_probability can have a
    // probability higher than 1.0, which is an undesirable condition. Epsilon
    // is used as a threshold of cyclic_probability, limiting its use below 1.0.
    static const double epsilon;

    // Required pass to identify loop in functions.
    LoopInfo *LI;

    // List of basic blocks not visited.
    SmallPtrSet<const BasicBlock *, 128> NotVisited;

    // List of loops visited.
    SmallPtrSet<const Loop *, 16> LoopsVisited;

    // Branch probabilities calculated.
    BranchPredictionPass *BPP;

    // Hold probabilities propagated to back edges.
    std::map<Edge, double> BackEdgeProbabilities;

    // Block and edge frequency information map.
    std::map<Edge, double> EdgeFrequencies;
    std::map<const BasicBlock *, double> BlockFrequencies;

    /// MarkReachable - Mark all blocks reachable from root block as not
    /// visited.
    void MarkReachable(BasicBlock *root);

    /// PropagateLoop - Propagate frequencies from the inner most loop to the
    /// outer most loop.
    void PropagateLoop(const Loop *loop);

    /// PropagateFreq - Compute basic block and edge frequencies by propagating
    /// frequencies.
    void PropagateFreq(BasicBlock *head);

    /// Clear - Clear all stored information.
    void Clear();

    /// VerifyIntegrity - The sum of frequencies of all edges leading to
    /// terminal nodes should match the entry frequency (that is always 1.0).
    bool VerifyIntegrity(Function &F) const;
  public:
    static char ID; // Class identification, replacement for typeinfo.

    BlockEdgeFrequencyPass();
    ~BlockEdgeFrequencyPass();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    virtual const char *getPassName() const;
    virtual bool runOnFunction(Function &F);
    virtual void releaseMemory();
    void print(raw_ostream &O, const Module *M) const;

    /// getEdgeFrequency - Find the edge frequency based on the source and
    /// the destination basic block.  If the edge is not found, return a
    /// default value.
    double getEdgeFrequency(const BasicBlock *src, const BasicBlock *dst) const;

    /// getEdgeFrequency - Find the edge frequency based on the edge. If the
    /// edge is not found, return a default value.
    double getEdgeFrequency(Edge &edge) const;

    /// getBlockFrequency - Find the basic block frequency based on the edge.
    /// If the basic block is not present, return a default value.
    double getBlockFrequency(const BasicBlock *BB) const;

    /// getBackEdgeProbabilities - Get updated probability of back edge. In case
    /// of not found, get the edge probability from the branch prediction.
    double getBackEdgeProbabilities(Edge &edge);

    // Block and edge iterators.
    typedef std::map<Edge, double>::const_iterator edge_iterator;
    typedef std::map<const BasicBlock *, double>::const_iterator block_iterator;

    /// block_freq_begin - Constant begin iterator for block frequencies.
    inline block_iterator block_freq_begin() const {
      return BlockFrequencies.begin();
    }

    /// block_freq_end - Constant end iterator for block frequencies.
    inline block_iterator block_freq_end() const {
      return BlockFrequencies.end();
    }

    /// edge_freq_begin - Constant begin iterator for edges frequencies.
    inline edge_iterator edge_freq_begin() const {
      return EdgeFrequencies.begin();
    }

    /// edge_freq_end - Constant end iterator for edges frequencies.
    inline edge_iterator edge_freq_end() const {
      return EdgeFrequencies.end();
    }
  };

} // End of llvm namespace.

#endif // LLVM_ANALYSIS_BLOCK_EDGE_FREQUENCY_PASS_H
