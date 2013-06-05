//===- BranchHeuristicsInfo.h - Interface to Branch Heuristics -*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is an auxiliary class to the branch heuristics class. It is responsible
// to match the successors taken and not taken when a heuristic matches a basic
// block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BRANCH_HEURISTICS_INFO_H
#define LLVM_ANALYSIS_BRANCH_HEURISTICS_INFO_H

// To get the definition of std::pair.
#include <utility>
#include "BranchPredictionInfo.h"

namespace llvm {
  class BasicBlock;
  class BranchPredictionInfo;
  class LoopInfo;
  class DominatorTree;
  struct PostDominatorTree;

  // All possible branch heuristics.
  enum BranchHeuristics {
    LOOP_BRANCH_HEURISTIC = 0,
    POINTER_HEURISTIC,
    CALL_HEURISTIC,
    OPCODE_HEURISTIC,
    LOOP_EXIT_HEURISTIC,
    RETURN_HEURISTIC,
    STORE_HEURISTIC,
    LOOP_HEADER_HEURISTIC,
    GUARD_HEURISTIC
  };

  // Hold the information regarding the heuristic, the probability of taken and
  // not taken the branches and the heuristic name, for debugging purposes.
  // The probabilities were taken from Table 1 in Wu's (1994) paper.
  // Note that the enumeration order is respected, to allow a direct access to
  // the probabilities.
  struct BranchProbabilities {
    // The current heuristic.
    enum BranchHeuristics heuristic;

    // Both probabilities are represented in this structure for faster access.
    const float probabilityTaken; // probability of taken a branch
    const float probabilityNotTaken; // probability of not taken a branch

    // The name of the heuristic. Used by debugging purposes.
    const char *name;
  };

  // A Prediction is a pair of basic blocks, in which the first indicates the
  // successor taken and the second the successor not taken.
  typedef std::pair<const BasicBlock *, const BasicBlock *> Prediction;

  /// BranchHeuristicsInfo - Verify whenever heuristic match to a branch in
  /// order to calculate its probabilities.
  class BranchHeuristicsInfo {
  private:
    // Required passes to calculate information.
    BranchPredictionInfo *BPI;
    DominatorTree *DT;
    PostDominatorTree *PDT;
    LoopInfo *LI;

    // Prediction empty contains null values and indicates an error.
    Prediction empty;

    // There are 9 branch prediction heuristics.
    static const unsigned numBranchHeuristics = 9;

    static const struct BranchProbabilities probList[numBranchHeuristics];

    // The below procedures are handlers to check
    // for heuristics matched. In case of successful
    // match, define which successor was taken
    // and which was not in the HeuristicMatch structure.

    /// MatchLoopBranchHeuristic - Predict as taken an edge back to a loop's
    /// head. Predict as not taken an edge exiting a loop.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchLoopBranchHeuristic(BasicBlock *root) const;

    /// MatchPointerHeuristic - Predict that a comparison of a pointer against
    /// null or of two pointers will fail.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchPointerHeuristic(BasicBlock *root) const;

    /// MatchCallHeuristic - Predict a successor that contains a call and does
    /// not post-dominate will not be taken.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchCallHeuristic(BasicBlock *root) const;

    /// MatchOpcodeHeuristic - Predict that a comparison of an integer for less
    /// than zero, less than or equal to zero, or equal to a constant, will
    /// fail.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchOpcodeHeuristic(BasicBlock *root) const;

    /// MatchLoopExitHeuristic - Predict that a comparison in a loop in which no
    /// successor is a loop head will not exit the loop.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchLoopExitHeuristic(BasicBlock *root) const;

    /// MatchReturnHeuristic - Predict a successor that contains a return will
    /// not be taken.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchReturnHeuristic(BasicBlock *root) const;

    /// MatchStoreHeuristic - Predict a successor that contains a store
    /// instruction and does not post-dominate will not be taken.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchStoreHeuristic(BasicBlock *root) const;

    /// MatchLoopHeaderHeuristic - Predict a successor that is a loop header or
    /// a loop pre-header and does not post-dominate will be taken.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchLoopHeaderHeuristic(BasicBlock *root) const;

    /// MatchGuardHeuristic - Predict that a comparison in which a register is
    /// an operand, the register is used before being defined in a successor
    /// block, and the successor block does not post-dominate will reach the
    /// successor block.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchGuardHeuristic(BasicBlock *root) const;
  public:
    // Define an edge as an pair of basic blocks.
    typedef std::pair<const BasicBlock *, const BasicBlock *> Edge;

    explicit BranchHeuristicsInfo(BranchPredictionInfo *BPI);
    ~BranchHeuristicsInfo();

    /// MatchHeuristic - Wrapper for the heuristics handlers meet above.
    /// This procedure assumes that root basic block has exactly two successors.
    /// @returns a Prediction that is a pair in which the first element is the
    /// successor taken, and the second the successor not taken.
    Prediction MatchHeuristic(BranchHeuristics bh, BasicBlock *root) const;

    /// getNumHeuristics - Obtain the total number of heuristics implemented.
    inline static unsigned getNumHeuristics() {
      return numBranchHeuristics;
    }

    /// getHeuristic - Find the heuristic based on the list index.
    inline static enum BranchHeuristics getHeuristic(unsigned idx) {
      return probList[idx].heuristic;
    }

    /// getProbabilityTaken - Get the branch taken probability for the
    /// heuristic bh.
    inline static float getProbabilityTaken(enum BranchHeuristics bh) {
      return probList[bh].probabilityTaken;
    }

    /// getProbabilityNotTaken - Get the branch not taken probability for the
    /// heuristic bh.
    inline static float getProbabilityNotTaken(enum BranchHeuristics bh) {
      return probList[bh].probabilityNotTaken;
    }

    /// getHeuristicName - Find the name of the heuristic given a heuristic bh.
    inline static const char *getHeuristicName(enum BranchHeuristics bh) {
      return probList[bh].name;
    }

  };
} // End of llvm namespace.

#endif // LLVM_ANALYSIS_BRANCH_HEURISTICS_INFO_H

