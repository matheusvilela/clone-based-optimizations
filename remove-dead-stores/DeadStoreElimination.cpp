#include "DeadStoreElimination.h"
#include "llvm/Support/CallSite.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"


using namespace llvm;

char DeadStoreEliminationPass::ID = 0;
INITIALIZE_PASS(DeadStoreEliminationPass, "dead-store-elimination", "Remove dead stores", false, true)

ModulePass *llvm::createDeadStoreEliminationPassPass() {
  return new DeadStoreEliminationPass;
}

void DeadStoreEliminationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<PADriver>();
  AU.setPreservesAll();
}

DeadStoreEliminationPass::DeadStoreEliminationPass() : ModulePass(ID) {
  RemovedStores   = 0;
  FunctionsCount  = 0;
  FunctionsCloned = 0;
  ClonesCount     = 0;
  CallsCount      = 0;
  PromissorCalls  = 0;
  CallsReplaced   = 0;
 
}

bool DeadStoreEliminationPass::runOnModule(Module &M) {
  PAD          = &getAnalysis<PADriver>();


  /** ALIAS ANALYSIS INFO **/
  std::map<int, std::set<int> > all = PAD->pointerAnalysis-> allPointsTo();
  for(std::map<int, std::set<int> >::iterator it = all.begin(); it != all.end(); ++it) {
     std::string name = PAD->nameMap.count(it->first) > 0 ? PAD->nameMap.at(it->first) : "Unknown";
     DEBUG(errs() << it->first << " (" << name << ") points to { ");
     for(std::set<int>::iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
        DEBUG(errs() << *it2 << " ");
     }
     DEBUG(errs() << "}\n");
  }
  /***/

  bool changed = false;

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    runDeadStoreAnalysis(*F);
    FunctionsCount++;
  }

  getFnThatStoreOnArgs(M);
  getGlobalVarsInfo(M);

  DEBUG(errs() << "Global vars points to : { ");
  for (std::set<int>::iterator aliasIt = globalPositions.begin(); aliasIt != globalPositions.end(); ++aliasIt) {
     DEBUG(errs() << *aliasIt << " ");
  }
  DEBUG(errs() << "}\n");



  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    changed = changed | removeDeadStores(*F);
  }
  cloneFunctions();



  return changed;
}


std::set<int> DeadStoreEliminationPass::getRecursivePositions(int id) {
  std::set<int> ret;
  std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(id);
  for (std::set<int>::iterator aliasIt = aliasIDs.begin(); aliasIt != aliasIDs.end(); ++aliasIt) {
     ret.insert(*aliasIt);
     std::set<int> recSet = getRecursivePositions(*aliasIt);
     ret.insert(recSet.begin(), recSet.end());
  }
  return ret;
}

void DeadStoreEliminationPass::getGlobalVarsInfo(Module &M) {
   for (Module::global_iterator git = M.global_begin(), gitE = M.global_end();
         git != gitE; ++git) {
      int ptrID              = PAD->Value2Int(git);
      std::set<int> aliasIDs = getRecursivePositions(ptrID);
      for (std::set<int>::iterator aliasIt = aliasIDs.begin(); aliasIt != aliasIDs.end(); ++aliasIt) {
         globalPositions.insert(*aliasIt);
      }
   }
}

bool DeadStoreEliminationPass::cloneFunctions() {
  bool modified = false;
  for (std::map<Function*, std::vector<Instruction*> >::iterator it = fn2Clone.begin();
      it != fn2Clone.end(); ++it) {

    std::map< std::set<Value*> , Function*> clonedFns;
    int i = 0;
    FunctionsCloned++;
    for (std::vector<Instruction*>::iterator it2 = it->second.begin();
        it2 != it->second.end(); ++it2, ++i) {

      Instruction* caller = *it2;
      std::set<Value*> deadArgs = deadArguments[caller];

      if (!clonedFns.count(deadArgs)) {
        // Clone function if a proper clone doesnt already exist
        std::stringstream suffix;
        suffix << ".deadstores" << i;
        Function* NF = cloneFunctionWithoutDeadStore(it->first, caller, suffix.str());
        replaceCallingInst(caller, NF);
        clonedFns[deadArgs] = NF;
        ClonesCount++;
      } else {
        // Use existing clone
        Function* NF = clonedFns.at(deadArgs);
        replaceCallingInst(caller, NF);
      }
      CallsReplaced++;
      modified = true;
    }
 
  }
  return modified;
}

// Clone the given function removing dead stores
Function* DeadStoreEliminationPass::cloneFunctionWithoutDeadStore(Function *Fn, Instruction* caller, std::string suffix) {

  // Start by computing a new prototype for the function, which is the
  // same as the old function
  Function *NF = Function::Create(Fn->getFunctionType(), Fn->getLinkage());
  NF->copyAttributesFrom(Fn);

  // After the parameters have been copied, we should copy the parameter
  // names, to ease function inspection afterwards.
  Function::arg_iterator NFArg = NF->arg_begin();
  for (Function::arg_iterator Arg = Fn->arg_begin(), ArgEnd = Fn->arg_end(); Arg != ArgEnd; ++Arg, ++NFArg) {
    NFArg->setName(Arg->getName());
  }

  // To avoid name collision, we should select another name.
  NF->setName(Fn->getName() + suffix);

  // fill clone content
  ValueToValueMapTy VMap;
  SmallVector<ReturnInst*, 8> Returns;
  Function::arg_iterator NI = NF->arg_begin();
  for (Function::arg_iterator I = Fn->arg_begin();
      NI != NF->arg_end(); ++I, ++NI) {
    VMap[I] = NI;
  }
  CloneAndPruneFunctionInto(NF, Fn, VMap, false, Returns);

  // remove dead stores
  std::set<Value*> deadArgs = deadArguments[caller];
  std::set<Value*> removeStoresTo;
  Function::arg_iterator NFArgIter = NF->arg_begin();
  for (Function::arg_iterator FnArgIter = Fn->arg_begin(); FnArgIter !=
      Fn->arg_end(); ++FnArgIter, ++NFArgIter) {
    Value *FnArg = FnArgIter;
    if (deadArgs.count(FnArg)) {
      removeStoresTo.insert(NFArgIter);
    }
  }

  std::vector<Instruction*> toRemove;
  for (Function::iterator BB = NF->begin(); BB != NF->end(); ++BB) {
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
      Instruction *inst = I;
      if (!isa<StoreInst>(inst)) continue;
      StoreInst *SI = dyn_cast<StoreInst>(inst);
      Value *ptrOp = SI->getPointerOperand();
      if (removeStoresTo.count(ptrOp)) {
        DEBUG(errs() << "will remove this store\n");
        toRemove.push_back(inst);
      }
    }
  }
  for (std::vector<Instruction*>::iterator it = toRemove.begin();
      it != toRemove.end(); ++it) {
    Instruction* inst = *it;
    inst->eraseFromParent();
    RemovedStores++;
  }
 
  // Insert the clone function before the original
  Fn->getParent()->getFunctionList().insert(Fn, NF);

  return NF;
}


void DeadStoreEliminationPass::replaceCallingInst(Instruction* caller, Function* fn) {
  if (isa<CallInst>(caller)) {
    CallInst *callInst = dyn_cast<CallInst>(caller);
    callInst->setCalledFunction(fn);
  } else if (isa<InvokeInst>(caller)) {
    InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
    invokeInst->setCalledFunction(fn);
  }
}

void DeadStoreEliminationPass::getFnThatStoreOnArgs(Module &M) {
  for (Module::iterator F = M.begin(); F != M.end(); ++F) {
    if (F->arg_empty()) continue;

    // Get args
    std::set<Value*> args;
    for (Function::arg_iterator formalArgIter = F->arg_begin(); formalArgIter !=
        F->arg_end(); ++formalArgIter) {
      Value *formalArg = formalArgIter;
      if (formalArg->getType()->isPointerTy()) {
        args.insert(formalArg);
      }
    }
   
    // Find stores that can be removed
    for (Function::iterator BB = F->begin(); BB != F->end(); ++BB) {
      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
        Instruction *inst = I;
        if (!isa<StoreInst>(inst)) continue;
        StoreInst *SI = dyn_cast<StoreInst>(inst);
        Value *ptrOp = SI->getPointerOperand();

        if (args.count(ptrOp) && canBeRemoved(ptrOp, inst, *F, false) ) {
          fnThatStoreOnArgs[F].insert(ptrOp);
        }
      }
    }
  }
}


void DeadStoreEliminationPass::runDeadStoreAnalysis(Function &F) {
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
}

bool DeadStoreEliminationPass::canBeRemoved(Value* ptr, Instruction* inst, Function &F, bool verifyArgs = true) {
  int ptrID              = PAD->Value2Int(ptr);
  std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);

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

  // Remove store if:
  // 1) it stores on a position that has no live uses after it
  //   * given by the analysis
  // 2) pointer points to position that is not live outside function
  //   * its position is not pointed by any pointer argument
  // 3) pointer poinst to at least one position

  bool hasUses     = false;
  bool aliasArgs   = false;
  bool aliasGlobal = false;

  DEBUG(errs() << "Verifying store...\n");
  if (isa<GlobalValue>(ptr))
    DEBUG(errs() << "store: is a global value: " << ptr->getName() << "\n");
  if (aliasIDs.size() == 0)
    DEBUG(errs() << "store to value that points to no position (?)" << ptr->getName() << "\n");

  if (aliasIDs.size() == 0) return false;
  DEBUG(errs() << "store points to positions ( ");
  for (std::set<int>::iterator aliasIt = aliasIDs.begin(); aliasIt != aliasIDs.end(); ++aliasIt) {
     DEBUG(errs() << *aliasIt << " ");
    hasUses     = hasUses || outValues[inst].count(*aliasIt);
    aliasGlobal = aliasGlobal || globalPositions.count(*aliasIt);
    if (verifyArgs) aliasArgs = aliasArgs || argsPositions.count(*aliasIt);
  }
  DEBUG(errs() << ")\n");
  if (hasUses) DEBUG(errs() <<  "cannot remove due to use. ");
  if (aliasArgs) DEBUG(errs() <<  "cannot remove due to alias args. ");
  if (aliasGlobal) DEBUG(errs() <<  "cannot remove due to alias global. ");
  if (hasUses || aliasArgs || aliasGlobal)  {
     DEBUG(errs() << "\n");
     return false;
  }
  return true;
}

bool DeadStoreEliminationPass::removeDeadStores(Function &F) {

  std::vector<Instruction*> toRemove;
  bool changed = false;

  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    for (BasicBlock::iterator it = BB->begin(), E = BB->end(); it != E; ++it) {
      Instruction *inst = it;
      if (isa<StoreInst>(inst)) {
        StoreInst *SI = dyn_cast<StoreInst>(inst);
        Value *ptr = SI->getPointerOperand();

        if (canBeRemoved(ptr, inst, F)) {
          toRemove.push_back(inst);
        }
      } else if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {

        CallsCount++;
        Function* calledFn;
        if (isa<CallInst>(inst)) {
          CallInst* CI = dyn_cast<CallInst>(inst);
          calledFn = CI->getCalledFunction();
        } else {
          InvokeInst* II = dyn_cast<InvokeInst>(inst);
          calledFn = II->getCalledFunction();
        }

        if(fnThatStoreOnArgs.count(calledFn)) {
          PromissorCalls++;
          DEBUG(errs() << "found a call that store on args\n");

          CallSite CS(inst);

          CallSite::arg_iterator actualArgIter = CS.arg_begin();
          Function::arg_iterator formalArgIter = calledFn->arg_begin();
          int size = calledFn->arg_size();

          std::set<Value*> storedArgs = fnThatStoreOnArgs[calledFn];
          for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
            Value *formalArg = formalArgIter;
            if (storedArgs.count(formalArg)) {
              DEBUG(errs() << "store on " << formalArg->getName() << "\n");
              Value *actualArg = *actualArgIter;

              if (canBeRemoved(actualArg, inst, F)) {
                deadArguments[inst].insert(formalArg);
                DEBUG(errs() << "should remove dead store with cloning\n");
              }
            }
          }
          if (deadArguments.count(inst)) fn2Clone[calledFn].push_back(inst);
        }
      }
    }
  }
  for (std::vector<Instruction*>::iterator it = toRemove.begin(); it != toRemove.end(); ++it) {
    Instruction *inst = *it;
    inst->eraseFromParent();
    changed = true;
    RemovedStores++;
    DEBUG(errs() << "Removing dead store\n");
  }
  return changed;
}

bool DeadStoreEliminationPass::analyzeBasicBlock(BasicBlock &BB) {
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

    if (isa<LoadInst>(inst) || isa<GetElementPtrInst>(inst) || isa<ReturnInst>(inst)) {
      const Value *ptr;
      if (isa<LoadInst>(inst)) {
        const LoadInst *LI = dyn_cast<LoadInst>(inst);
        ptr = LI->getPointerOperand();
      } else if (isa<GetElementPtrInst>(inst))  {
        const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(inst);
        ptr = GEPI->getPointerOperand();
      } else {
        const ReturnInst *RI = dyn_cast<ReturnInst>(inst);
        ptr = RI->getReturnValue();
        if (!ptr) {
          successor = inst;
          continue;
        }
      }

      int ptrID = PAD->Value2Int((Value*)ptr);
      std::set<int> aliasIDs = PAD->pointerAnalysis->pointsTo(ptrID);

      DEBUG(errs() << "Verifying instruction: " << inst << "...");
      if (isa<GlobalValue>((Value*)ptr))
        DEBUG(errs() << "load to value that is a global value: " << ptr->getName() << "\n");
      if (aliasIDs.size() == 0) {
        DEBUG(errs() << "load to value that points to no position (?)" << ptr->getName() << "\n");
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
