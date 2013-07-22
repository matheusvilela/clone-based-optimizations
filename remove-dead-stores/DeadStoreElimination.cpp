#include "DeadStoreElimination.h"
#include "llvm/Support/CallSite.h"
#undef DEBUG_TYPE
#define DEBUG_TYPE "dse"

using namespace llvm;

char DeadStoreEliminationPass::ID = 0;

static RegisterPass<DeadStoreEliminationPass>
X("dead-store-elimination", "Remove dead stores",
    false, true);

void DeadStoreEliminationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<PADriver>();
  AU.setPreservesAll();
}

DeadStoreEliminationPass::DeadStoreEliminationPass() : ModulePass(ID) {
  RemovedStores = 0;
}

bool DeadStoreEliminationPass::runOnModule(Module &M) {
  PAD          = &getAnalysis<PADriver>();
  bool changed = false;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    changed = changed | runOnFunction(*F);
  }
  return changed;
}

bool DeadStoreEliminationPass::runOnFunction(Function &F) {
  currentFn = &F;

  std::queue<BasicBlock*> workList;
  // Collect successors and predecessors and build the work list
  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    TerminatorInst* terminator = BB->getTerminator();
    successors[BB];
    predecessors[BB];
    workList.push(BB);
    if (terminator && terminator->getNumSuccessors() > 0) {
      unsigned numSuccessors = terminator->getNumSuccessors();
      for (unsigned i = 0; i < numSuccessors; ++i) {
        successors[BB].push_back(terminator->getSuccessor(i));
        predecessors[terminator->getSuccessor(i)].push_back(BB);
      }
    }
  }

  // Find dead stores
  while (!workList.empty()) {
    BasicBlock* BB = workList.front();
    workList.pop();
    if(analyzeBasicBlock(*BB)) {
      for (std::vector<BasicBlock*>::iterator pred = predecessors[BB].begin(); pred != predecessors[BB].end(); ++pred) {
        workList.push(*pred);
      }
    }
  }

  DEBUG(printAnalysis(errs()));

  // Remove dead stores
  bool changed = false;
  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    changed = changed | removeDeadStores(*BB, F);
  }
  return changed;
}

bool DeadStoreEliminationPass::removeDeadStores(BasicBlock &BB, Function &F) {

  std::vector<Instruction*> toRemove;
  bool changed = false;

  std::set<int> argsPositions;
  for (Function::arg_iterator formalArgIter = F.arg_begin(); formalArgIter !=
      F.arg_end(); ++formalArgIter) {
    Value *formalArg = formalArgIter;
    if (formalArg->getType()->isPointerTy()) {
      int ptrID = PAD->Value2Int(formalArg);
      std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);
      argsPositions.insert(aliasIDs.begin(), aliasIDs.end());
    }
  }
  for (BasicBlock::iterator it = BB.begin(), E = BB.end(); it != E; ++it) {
    Instruction *inst = it;
    if (isa<StoreInst>(inst)) {
      StoreInst *SI = dyn_cast<StoreInst>(inst);
      Value *ptr = SI->getPointerOperand();
      int ptrID        = PAD->Value2Int((Value*)ptr);
      std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);
      // Remove store if:
      // 1) pointer points to only one position
      //   * given by alias analysis
      // 2) pointer points to position that is not live outside function
      //   * its position is not pointed by any pointer argument
      // 3) it stores on a position that has no live uses after it
      //   * given by the analysis
      if (aliasIDs.size() == 1 && !argsPositions.count(*aliasIDs.begin()) && !outValues[inst].count(*aliasIDs.begin())) {
        DEBUG(errs() << "Removing dead store\n");
        toRemove.push_back(inst);
      }
    }
  }
  for (std::vector<Instruction*>::iterator it = toRemove.begin(); it != toRemove.end(); ++it) {
    Instruction *inst = *it;
    inst->eraseFromParent();
    changed = true;
    RemovedStores++;
  }
  return changed;
}

bool DeadStoreEliminationPass::analyzeBasicBlock(BasicBlock &BB) {
  DEBUG(errs() << "running on bb " << BB.getName() << "\n");
  const Instruction *successor = NULL;
  bool changed = false;
  for (BasicBlock::const_iterator it = BB.end(), E = BB.begin(); it != E; ) {
    const Instruction *inst = --it;

    inValues[inst];
    outValues[inst];

    //Compute out set
    if (successor == NULL) {
      for (std::vector<BasicBlock*>::iterator succ = successors[&BB].begin(); succ != successors[&BB].end(); ++succ) {
        std::set<int> successorIn = inValues[(*succ)->begin()];
        outValues[inst].insert(successorIn.begin(), successorIn.end());
      }
    } else {
      std::set<int> successorIn = inValues[successor];
      outValues[inst].insert(successorIn.begin(), successorIn.end());
    }

    //Compute in set
    unsigned long inSize = inValues[inst].size();
    std::set<int> myOut = outValues[inst];
    inValues[inst].insert(myOut.begin(), myOut.end());
    if (inst == BB.begin() && inSize != inValues[inst].size()) {
      changed = true;
    }

    if (isa<StoreInst>(inst)) {
      const StoreInst *SI = dyn_cast<StoreInst>(inst);
      const Value *ptr = SI->getPointerOperand();
      int ptrID        = PAD->Value2Int((Value*)ptr);
      std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);
      if (aliasIDs.size() == 1) {
        DEBUG(errs() << "store to value that points to only one position: " << *aliasIDs.begin() << "\n");
        if(inValues[inst].count(*aliasIDs.begin())) {
          inValues[inst].erase(*aliasIDs.begin());
          if (inst == BB.begin()) changed = true;
        }
      } else if (aliasIDs.size() == 0) {
        DEBUG(errs() << "store to value that points to no position (?)\n");
      } else {
        DEBUG(errs() << "store to value that points to more than one position: ");
        for(std::set<int>::iterator ait = aliasIDs.begin(); ait != aliasIDs.end(); ++ait) {
          DEBUG(errs () << *ait << " ");
        }
        DEBUG(errs() << "\n");
      }
    }

    else if (isa<LoadInst>(inst) || isa<GetElementPtrInst>(inst)) {
      const Value *ptr;
      if (isa<LoadInst>(inst)) {
        const LoadInst *LI = dyn_cast<LoadInst>(inst);
        ptr = LI->getPointerOperand();
      } else {
        const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(inst);
        ptr = GEPI->getPointerOperand();
      }

      int ptrID = PAD->Value2Int((Value*)ptr);
      std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);

      if (aliasIDs.size() == 0) {
        DEBUG(errs() << "load to value that points to no position (?)\n");
      } else {
        DEBUG(errs() << "load to value that points to positions: ");
        for(std::set<int>::iterator aliasIt = aliasIDs.begin(); aliasIt != aliasIDs.end(); ++aliasIt) {
          DEBUG(errs() << *aliasIt << " ");
          if (inValues[inst].count(*aliasIt) == 0) {
            inValues[inst].insert(*aliasIt);
            if (inst == BB.begin()) changed = true;
          }
        }
        DEBUG(errs() << "\n");

      }
    }
    successor = inst;
  }
  DEBUG(errs() << "running on bb " << BB.getName() << " returned" << changed << "\n");
  return changed;
}

void DeadStoreEliminationPass::print(raw_ostream &O, const Module *M) const {
  O << "Number of dead stores removed: " << RemovedStores << "\n";
}

void DeadStoreEliminationPass::printAnalysis(raw_ostream &O) const {
  for (Function::const_iterator I = currentFn->begin(), E = currentFn->end(); I != E; ++I) {
    const BasicBlock *BB = I;
    O << BB->getName() << "\n";
    for (BasicBlock::const_iterator A = BB->begin(), B = BB->end(); A != B; ++A) {
      const Instruction *inst = A;
      printSet(O, inValues.at(inst));
      O << "  ";
      inst->print(O);
      O << "\n";
      printSet(O, outValues.at(inst));
    }
  }
}

void DeadStoreEliminationPass::printSet(raw_ostream &O, const std::set<int> &myset) const {
  O << "       { ";
  for (std::set<int>::const_iterator it = myset.begin(); it != myset.end(); ++it) {
    O << *it << " ";
  }
  O << "}\n";
}
