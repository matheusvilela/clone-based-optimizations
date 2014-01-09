//===-- BranchPredictionInfo.cpp - Calculates Info for Branch Prediction --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the auxiliary class for branch predictor routines. It
// calculates back edges and exit edges for functions and also find which
// basic blocks contains calls and store instructions. 
//
//===----------------------------------------------------------------------===//

#include "BranchPredictionInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;

BranchPredictionInfo::BranchPredictionInfo(DominatorTree *DT, LoopInfo *LI,
                                           PostDominatorTree *PDT) {
  this->DT = DT;
  this->PDT = PDT;
  this->LI = LI;
}

BranchPredictionInfo::~BranchPredictionInfo() {
}

/// FindBackAndExitEdges - Search for back and exit edges for all blocks
/// within the function loops, calculated using loop information.
void BranchPredictionInfo::FindBackAndExitEdges(Function &F) {
  SmallPtrSet<const BasicBlock *, 64> LoopsVisited;
  SmallPtrSet<const BasicBlock *, 64> BlocksVisited;

  for (LoopInfo::iterator LIT = LI->begin(), LIE = LI->end();
       LIT != LIE; ++LIT) {
    Loop *rootLoop = *LIT;
    BasicBlock *rootHeader = rootLoop->getHeader();

    // Check if we already visited this loop.
    if (LoopsVisited.count(rootHeader))
      continue;

    // Create a stack to hold loops (inner most on the top).
    SmallVector<Loop *, 8> Stack;
    SmallPtrSet<const BasicBlock *, 8> InStack;

    // Put the current loop into the Stack.
    Stack.push_back(rootLoop);
    InStack.insert(rootHeader);

    do {
      Loop *loop = Stack.back();

      // Search for new inner loops.
      bool foundNew = false;
      for (Loop::iterator I = loop->begin(), E = loop->end(); I != E; ++I) {
        Loop *innerLoop = *I;
        BasicBlock *innerHeader = innerLoop->getHeader();

        // Skip visited inner loops.
        if (!LoopsVisited.count(innerHeader)) {
          Stack.push_back(innerLoop);
          InStack.insert(innerHeader);
          foundNew = true;
          break;
        }
      }

      // If a new loop is found, continue.
      // Otherwise, it is time to expand it, because it is the most inner loop
      // yet unprocessed.
      if (foundNew)
        continue;

      // The variable "loop" is now the unvisited inner most loop.
      BasicBlock *header = loop->getHeader();

      // Search for all basic blocks on the loop.
      for (Loop::block_iterator LBI = loop->block_begin(),
           LBE = loop->block_end(); LBI != LBE; ++LBI) {
        BasicBlock *lpBB = *LBI;
        if (!BlocksVisited.insert(lpBB))
          continue;

        // Set the number of back edges to this loop head (lpBB) as zero.
        BackEdgesCount[lpBB] = 0;

        // For each loop block successor, check if the block pointing is
        // outside the loop.
        TerminatorInst *TI = lpBB->getTerminator();
        for (unsigned s = 0; s < TI->getNumSuccessors(); ++s) {
          BasicBlock *successor = TI->getSuccessor(s);
          Edge edge = std::make_pair(lpBB, successor);

          // If the successor matches any loop header on the stack,
          // then it is a backedge.
          if (InStack.count(successor)) {
            listBackEdges.insert(edge);
            ++BackEdgesCount[lpBB];
          }

          // If the successor is not present in the loop block list, then it is
          // an exit edge.
          if (!loop->contains(successor))
            listExitEdges.insert(edge);
        }
      }

      // Cleaning the visited loop.
      LoopsVisited.insert(header);
      Stack.pop_back();
      InStack.erase(header);
    } while (!InStack.empty());
  }
}

/// FindCallsAndStores - Search for call and store instruction on basic blocks.
void BranchPredictionInfo::FindCallsAndStores(Function &F) {
  // Run through all basic blocks of functions.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ++FI) {
    BasicBlock *BB = FI;

    // We only need to know if a basic block contains ONE call and/or ONE store.
    bool calls = false;
    bool stores = false;

    // If the terminator instruction is an InvokeInstruction, add it directly.
    // An invoke instruction can be interpreted as a call.
    if (isa<InvokeInst>(BB->getTerminator())) {
      listCalls.insert(BB);
      calls = true;
    }

    // Run over through all basic block searching for any calls.
    for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
         BI != BE; ++BI) {
      // If we found one of each, we don't need to search anymore.
      if (stores && calls)
        break;

      // Obtain the current basic block instruction.
      Instruction *I = BI;

      // If we haven't found a store yet, test the instruction
      // and mark it if it is a store instruction.
      if (!stores && isa<StoreInst>(I)) {
        listStores.insert(BB);
        stores = true;
      }

      // If we haven't found a call yet, test the instruction
      // and mark it if it is a call instruction.
      if (!calls && isa<CallInst>(I)) {
        listCalls.insert(BB);
        calls = true;
      }
    }
  }
}

/// BuildInfo - Build the list of back edges, exit edges, calls and stores.
void BranchPredictionInfo::BuildInfo(Function &F) {
  // clear the lists first.
  Clear();

  // Find the list of back edges and exit edges for all of the edges in the
  // respective function and build a list.
  FindBackAndExitEdges(F);

  // Find all the basic blocks in the function "F" that contains calls or
  // stores and build a list.
  FindCallsAndStores(F);
}

/// Clear - Make the list of back edges, exit edges, calls and stores empty.
void BranchPredictionInfo::Clear() {
  // Remove all elements.
  BackEdgesCount.clear();

  // Remove all elements.
  listBackEdges.clear();

  // Remove all elements.
  listExitEdges.clear();

  // Remove all elements.
  listCalls.clear();

  // Remove all elements.
  listStores.clear();
}

/// CountBackEdges - Given a basic block, count the number of successor
/// that are back edges.
unsigned BranchPredictionInfo::CountBackEdges(BasicBlock *BB) const {
  std::map<const BasicBlock *, unsigned>::const_iterator it =
      BackEdgesCount.find(BB);
  return it != BackEdgesCount.end() ? it->second : 0;
}

/// CallsExit - Check whenever a basic block contains a call to exit.
bool BranchPredictionInfo::CallsExit(BasicBlock *BB) const {
  TerminatorInst *TI = BB->getTerminator();
  return (isa<ResumeInst>(TI));
}

/// isBackEdge - Verify if an edge is a back edge.
bool BranchPredictionInfo::isBackEdge(const Edge &edge) const {
  return listBackEdges.count(edge);
}

/// isExitEdge - Verify if an edge is an exit edge.
bool BranchPredictionInfo::isExitEdge(const Edge &edge) const {
  return listExitEdges.count(edge);
}

/// hasCall - Verify if a basic block contains a call.
bool BranchPredictionInfo::hasCall(const BasicBlock *BB) const {
  return listCalls.count(BB);
}

/// hasStore - Verify if any instruction of a basic block is a store.
bool BranchPredictionInfo::hasStore(const BasicBlock *BB) const {
  return listStores.count(BB);
}

