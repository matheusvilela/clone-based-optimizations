#include "DeadStoreElimination.h"
#include "llvm/Support/CallSite.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "dse"

using namespace llvm;

char DeadStoreEliminationPass::ID = 0;
INITIALIZE_PASS(DeadStoreEliminationPass, "dead-store-elimination", "Remove dead stores", false, true)

ModulePass *llvm::createDeadStoreEliminationPassPass() {
  return new DeadStoreEliminationPass;
}

void DeadStoreEliminationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AliasAnalysis>();
  AU.setPreservesAll();
}

DeadStoreEliminationPass::DeadStoreEliminationPass() : ModulePass(ID) {
  RemovedStores = 0;
}

bool DeadStoreEliminationPass::runOnModule(Module &M) {
  AA          = &getAnalysis<AliasAnalysis>();
  bool changed = false;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    changed = changed | runOnFunction(*F);
  }
  for (std::map<const Instruction*, AliasSetTracker* >::iterator it = inValues.begin();
        it != inValues.end(); ++it) {
     if (it->second != NULL) delete it->second;
  }
  for (std::map<const Instruction*, AliasSetTracker* >::iterator it = outValues.begin();
        it != outValues.end(); ++it) {
     if (it->second != NULL) delete it->second;
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

static uint64_t getPointerSize(const Value *V, AliasAnalysis &AA) {
  uint64_t Size;
  if (getObjectSize(V, Size, AA.getDataLayout(), AA.getTargetLibraryInfo()))
    return Size;
  return AliasAnalysis::UnknownSize;
}

bool DeadStoreEliminationPass::removeDeadStores(BasicBlock &BB, Function &F) {
 
  std::vector<Instruction*> toRemove;
  bool changed = false;
 
  AliasSetTracker* argAST = new AliasSetTracker(*AA);
  for (Function::arg_iterator formalArgIter = F.arg_begin(); formalArgIter !=
      F.arg_end(); ++formalArgIter) {
    Value *formalArg = formalArgIter;
    if (formalArg->getType()->isPointerTy()) {
      uint64_t size = getPointerSize(formalArg, *AA);
      if (size == AliasAnalysis::UnknownSize) {
        errs() << "UnknownSize\n";
        size = AA->getTypeStoreSize(formalArg->getType());
        if (size == AliasAnalysis::UnknownSize) errs() << "UnknownSize[2]\n";
      }
      argAST->add(formalArg, size, NULL); //formalArg->getMetadata(LLVMContext::MD_tbaa));
    }
  }
  for (BasicBlock::iterator it = BB.begin(), E = BB.end(); it != E; ++it) {
    Instruction *inst = it;
    if (isa<StoreInst>(inst)) {
      // Remove store if:
      // 1) pointer points to only one position (isMustAlias)
      //   * given by alias analysis
      // 2) pointer points to position that is not live outside function
      //   * its position is not pointed by any pointer argument
      // 3) it stores on a position that has no live uses after it
      //   * given by the analysis
      StoreInst *SI = dyn_cast<StoreInst>(inst);
      AliasSetTracker* storeAST = new AliasSetTracker(*AA);
      storeAST->add(*outValues[inst]);
      storeAST->add(*argAST);
      errs() << "===\n";
      for (AliasSetTracker::iterator it = storeAST->begin(); it != storeAST->end(); ++it) {
        (*it).print(errs());
      }
      if (!storeAST->remove(SI)) { //(2) e (3)
        DEBUG(errs() << "Passed (3)\n");
        delete storeAST;
        storeAST = new AliasSetTracker(*AA);
        storeAST->add(SI);
        if (storeAST->begin()->isMustAlias()) { //(1)
          DEBUG(errs() << "Removing dead store\n");
          toRemove.push_back(inst);
        }
      } else {
        DEBUG(errs() << "Didnt passed (3)\n");
        for (AliasSetTracker::iterator it = storeAST->begin(); it != storeAST->end(); ++it) {
          (*it).print(errs());
        }
        errs() << "...\n";
      }
      delete storeAST;
    }
  }
  delete argAST;
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
  Instruction *successor = NULL;
  bool changed = false;
  for (BasicBlock::iterator it = BB.end(), E = BB.begin(); it != E; ) {
    Instruction *inst = --it;

    if (inValues[inst] == NULL) {
      inValues[inst] = new AliasSetTracker(*AA);
    }
    if (outValues[inst] == NULL) {
      outValues[inst] = new AliasSetTracker(*AA);
    }

    //Compute out set
    if (successor == NULL) {
      for (std::vector<BasicBlock*>::iterator succ = successors[&BB].begin(); succ != successors[&BB].end(); ++succ) {
        AliasSetTracker* successorIn = inValues[(*succ)->begin()];
        if (successorIn != NULL) outValues[inst]->add(*successorIn);
      }
    } else {
      AliasSetTracker* successorIn = inValues[successor];
      outValues[inst]->add(*successorIn);
    }

    //Compute in set
    unsigned long size = inValues[inst]->getAliasSets().size();
    AliasSetTracker* myOut = outValues[inst];
    inValues[inst]->add(*myOut);
    if (inValues[inst]->getAliasSets().size() != size) {
     changed = true;
    }

    if (isa<StoreInst>(inst)) {
      StoreInst *SI = dyn_cast<StoreInst>(inst);
      AliasSetTracker* storeAST = new AliasSetTracker(*AA);
      storeAST->add(SI);
      if (storeAST->begin()->isMustAlias()) {
        if(inValues[inst]->remove(SI) && inst == BB.begin()) changed = true;
      }
      delete storeAST;
    }

    else if (isa<LoadInst>(inst)) {// || isa<GetElementPtrInst>(inst)) {
      LoadInst *LI = dyn_cast<LoadInst>(inst);
      if(inValues[inst]->add(LI) && inst == BB.begin()) changed = true;
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
      printSet(O, *inValues.at(inst));
      O << "  ";
      inst->print(O);
      O << "\n";
      printSet(O, *outValues.at(inst));
    }
  }
}

void DeadStoreEliminationPass::printSet(raw_ostream &O, AliasSetTracker &myset) const {
  O << "       { ";
  for (AliasSetTracker::const_iterator it = myset.begin(); it != myset.end(); ++it) {
    (*it).print(O);
  }
  O << "}\n";
}
