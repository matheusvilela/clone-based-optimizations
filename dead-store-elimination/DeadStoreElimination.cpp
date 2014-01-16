#include "DeadStoreElimination.h"
#include "llvm/Support/CallSite.h"

using namespace llvm;

static RegisterPass<DeadStoreEliminationPass>
X("dead-store-elimination", "Remove dead stores", false, true);

static uint64_t getPointerSize(const Value *V, AliasAnalysis &AA) {
  uint64_t Size;
  if (getObjectSize(V, Size, AA.getDataLayout(), AA.getTargetLibraryInfo()))
    return Size;
  else {
    return AA.getTypeStoreSize(V->getType());
  }
}

void DeadStoreEliminationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AliasAnalysis>();
  AU.addRequired<MemoryDependenceAnalysis>();
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

  //Get some stats before doing anything
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      FunctionsCount++;
      CallsCount += F->getNumUses();
    }
  }

  if (!getFnThatStoreOnArgs(M)) {
    return false;
  }

  bool changed = false;

  changed    = changed | changeLinkageTypes(M);
  AA         = &getAnalysis<AliasAnalysis>();

  // Analyse program
  runOverwrittenDeadStoreAnalysis(M);
  runNotUsedDeadStoreAnalysis();

  // Create clones
  changed = changed | cloneFunctions();
  return changed;
}

/* Change linkages of global values, in order to
 * improve alias analysis.
 */
bool DeadStoreEliminationPass::changeLinkageTypes(Module &M) {
  DEBUG(errs() << "Changing linkages to private...\n");
  for (Module::global_iterator git = M.global_begin(), gitE = M.global_end();
        git != gitE; ++git) {
    DEBUG(errs() << "  " << *git << "\n");
    if (!git->hasExternalLinkage() && !git->hasAppendingLinkage()) git->setLinkage(GlobalValue::PrivateLinkage);
  }
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
     if (!F->hasExternalLinkage() && !F->hasAppendingLinkage()) F->setLinkage(GlobalValue::PrivateLinkage);
      DEBUG(errs() << "  " << F->getName() << "\n");
    }
  }
  DEBUG(errs() << "\n");
  return true;
}

/*
 * Build information about functions that store on pointer arguments
 * For simplification, we only consider a function to store on an argument
 * if it has exactly one StoreInst to that argument and the arg has no other use.
 */
int DeadStoreEliminationPass::getFnThatStoreOnArgs(Module &M) {
  int numStores = 0;
  DEBUG(errs() << "Getting functions that store on arguments...\n");
  for (Module::iterator F = M.begin(); F != M.end(); ++F) {
    if (F->arg_empty() || F->isDeclaration()) continue;

    // Get args
    std::set<Value*> args;
    for (Function::arg_iterator formalArgIter = F->arg_begin();
          formalArgIter != F->arg_end(); ++formalArgIter) {
      Value *formalArg = formalArgIter;
      if (formalArg->getType()->isPointerTy()) {
        args.insert(formalArg);
      }
    }

    // Find stores on arguments
    for (Function::iterator BB = F->begin(); BB != F->end(); ++BB) {
      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
        Instruction *inst = I;
        if (!isa<StoreInst>(inst)) continue;
        StoreInst *SI = dyn_cast<StoreInst>(inst);
        Value *ptrOp = SI->getPointerOperand();

        if (args.count(ptrOp) && ptrOp->hasNUses(1)) {
          fnThatStoreOnArgs[F].insert(ptrOp);
          numStores++;
          DEBUG(errs() << "  " << F->getName() << " stores on argument "
                << ptrOp->getName() << "\n"); }
      }
    }
  }
  DEBUG(errs() << "\n");
  return numStores;
}

/*
 * Find stores to arguments that are not read on the caller function. If the
 * corresponding actual argument is locally declared on the caller, the
 * store can be removed with cloning.
 */
void DeadStoreEliminationPass::runNotUsedDeadStoreAnalysis() {

  DEBUG(errs() << "Running not used dead store analysis...\n");
  for(std::map<Function*, std::set<Value*> >::iterator it =
        fnThatStoreOnArgs.begin(); it != fnThatStoreOnArgs.end(); ++it) {
    Function* F = it->first;
    DEBUG(errs() << "  Verifying function " << F->getName() << ".\n");

    // Verify each callsite of functions that store on arguments
    for (Value::use_iterator UI = F->use_begin(), E = F->use_end();
          UI != E; ++UI) {
       User *U = *UI;

      if (isa<BlockAddress>(U)) continue;
      if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;

      Instruction* inst = cast<Instruction>(U);
      if (deadArguments.count(inst)) continue;

      CallSite CS(inst);
      if (!CS.isCallee(UI)) continue;

      CallSite::arg_iterator actualArgIter = CS.arg_begin();
      Function::arg_iterator formalArgIter = F->arg_begin();
      int size = F->arg_size();

      std::set<Value*> storedArgs = fnThatStoreOnArgs[F];
      for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
        Value *formalArg = formalArgIter;
        Value *actualArg = *actualArgIter;

        if (storedArgs.count(formalArg)) {
          DEBUG(errs() << "    Store on " << formalArg->getName()
                << " may be removed with cloning on instruction " << *inst << "\n");
          //TODO: handle malloc and other allocation functions
          Instruction* argDeclaration = dyn_cast<Instruction>(actualArg);
          if (!argDeclaration || !isa<AllocaInst>(argDeclaration)) {
            DEBUG(errs() << "    Can't remove because actual arg was not locally allocated.\n");
            continue;
          }
          if (hasAddressTaken(argDeclaration, CS)) {
            DEBUG(errs() << "    Can't remove because actual arg has its address taken.\n");
            continue;
          }
          if (isRefAfterCallSite(actualArg, CS)) {
            DEBUG(errs() << "    Can't remove because actual arg is used after callSite.\n");
            continue;
          }
          DEBUG(errs() << "  Store on " << formalArg->getName() << " will be removed with cloning\n");
          deadArguments[inst].insert(formalArg);
        }
      }
      if (deadArguments.count(inst)) {
        fn2Clone[F].push_back(inst);
      }
    }
  }
  DEBUG(errs() << "\n");
}

/*
 * Check if a given instruction has its address taken.
 */
bool DeadStoreEliminationPass::hasAddressTaken(const Instruction *AI, CallSite& CS) {
  const Instruction* callInst = CS.getInstruction();
  for (Value::const_use_iterator UI = AI->use_begin(), UE = AI->use_end();
      UI != UE; ++UI) {
    const User *U = *UI;
    if (const StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (AI == SI->getValueOperand())
        return true;
    } else if (const PtrToIntInst *SI = dyn_cast<PtrToIntInst>(U)) {
      if (AI == SI->getOperand(0))
        return true;
    } else if (isa<CallInst>(U) && dyn_cast<Instruction>(U) != callInst) {
      return true;
    } else if (isa<InvokeInst>(U) && dyn_cast<Instruction>(U) != callInst) {
      return true;
    } else if (const SelectInst *SI = dyn_cast<SelectInst>(U)) {
      if (hasAddressTaken(SI, CS))
        return true;
    } else if (const PHINode *PN = dyn_cast<PHINode>(U)) {
      // Keep track of what PHI nodes we have already visited to ensure
      // they are only visited once.
      if (VisitedPHIs.insert(PN))
        if (hasAddressTaken(PN, CS))
          return true;
    } else if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
      if (hasAddressTaken(GEP, CS))
        return true;
    } else if (const BitCastInst *BI = dyn_cast<BitCastInst>(U)) {
      if (hasAddressTaken(BI, CS))
        return true;
    }
  }
  return false;
}

/*
 * Verify if a given value has references after a call site.
 */
bool DeadStoreEliminationPass::isRefAfterCallSite(Value* v, CallSite &CS) {
  BasicBlock* CSBB = CS.getInstruction()->getParent();

  // Collect basic blocks to inspect
  std::vector<BasicBlock*> BBToInspect;
  std::set<BasicBlock*> BBToInspectSet;
  BBToInspect.push_back(CSBB);
  BBToInspectSet.insert(CSBB);
  for (unsigned int i = 0; i < BBToInspect.size(); ++i) {
    BasicBlock* BB = BBToInspect.at(i);
    TerminatorInst* terminator = BB->getTerminator();
    if (terminator && terminator->getNumSuccessors() > 0) {
      unsigned numSuccessors = terminator->getNumSuccessors();
      for (unsigned i = 0; i < numSuccessors; ++i) {
        // Collect successors
        BasicBlock* successor = terminator->getSuccessor(i);
        if (!BBToInspectSet.count(successor)) {
          BBToInspect.push_back(successor);
          BBToInspectSet.insert(successor);
        }
      }
    }
  }

  // Inspect if any instruction after CS references v
  AliasAnalysis::Location loc(v, getPointerSize(v, *AA), NULL);
  for (unsigned int i = 0; i < BBToInspect.size(); ++i) {
    BasicBlock* BB = BBToInspect.at(i);
    BasicBlock::iterator I = BB->begin();
    if (BB == CSBB) {
      Instruction* callInst = CS.getInstruction();
      Instruction* inst;
      do {
        inst = I;
        ++I;
      } while (inst != callInst);
    }
    for (BasicBlock::iterator IE = BB->end(); I != IE; ++I) {
      Instruction* inst = I;
      DEBUG(errs() << "Verifying if instruction " << *inst << " refs " << *v << ": ");
      AliasAnalysis::ModRefResult mrf = AA->getModRefInfo(inst, loc);
      DEBUG(errs() << mrf << "\n");
      if (mrf == AliasAnalysis::Ref || mrf == AliasAnalysis::ModRef) {
        return true;
      }
    }
  }
  return false;
}

/*
 * Find stores to arguments that are overwritten before being read.
 */
void DeadStoreEliminationPass::runOverwrittenDeadStoreAnalysis(Module &M) {
  DEBUG(errs() << "Running overwritten dead store analysis...\n");
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      runOverwrittenDeadStoreAnalysisOnFn(*F);
    }
  }
  DEBUG(errs() << "\n");
}
void DeadStoreEliminationPass::runOverwrittenDeadStoreAnalysisOnFn(Function &F) {
  MDA       = &getAnalysis<MemoryDependenceAnalysis>(F);

  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    for (BasicBlock::iterator I = BB->begin(), IE = BB->end(); I != IE; ++I) {
      Instruction *inst = I;
      if (StoreInst* SI = dyn_cast<StoreInst>(inst)) {
        Value *ptr           = SI->getPointerOperand();
        MemDepResult mdr     = MDA->getDependency(inst);
        Instruction *depInst = mdr.getInst();
        if (depInst && (isa<CallInst>(depInst) || isa<InvokeInst>(depInst))) {
           Function *calledFn;

           if (CallInst* CI = dyn_cast<CallInst>(depInst)) {
             calledFn = CI->getCalledFunction();
           } else {
             InvokeInst *II = dyn_cast<InvokeInst>(depInst);
             calledFn = II->getCalledFunction();
           }
           if (!fnThatStoreOnArgs.count(calledFn)) continue;

           CallSite CS(depInst);

           CallSite::arg_iterator actualArgIter = CS.arg_begin();
           Function::arg_iterator formalArgIter = calledFn->arg_begin();
           int size = calledFn->arg_size();

           std::set<Value*> storedArgs = fnThatStoreOnArgs[calledFn];
           for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
             Value *formalArg = formalArgIter;
             Value *actualArg = *actualArgIter;
             if (ptr == actualArg && storedArgs.count(formalArg)) {
               int64_t InstWriteOffset, DepWriteOffset;
               DEBUG(errs() << "  Verifying if store is completely overwritten.\n");
               AliasAnalysis::Location Loc(ptr, getPointerSize(ptr, *AA), NULL);
               AliasAnalysis::Location DepLoc(actualArg, getPointerSize(actualArg, *AA), NULL);
               OverwriteResult OR = isOverwrite(Loc, DepLoc, *AA, DepWriteOffset, InstWriteOffset);
               if (OR == OverwriteComplete) {
                 DEBUG(errs() << "  Store on " << formalArg->getName() << " will be removed with cloning\n");
                 deadArguments[depInst].insert(formalArg);
               }
             }
           }
           if (deadArguments.count(depInst)) {
             fn2Clone[calledFn].push_back(depInst);
           }
        }
      }
    }
  }
}

/// isOverwrite - Return 'OverwriteComplete' if a store to the 'Later' location
/// completely overwrites a store to the 'Earlier' location.
/// 'OverwriteEnd' if the end of the 'Earlier' location is completely
/// overwritten by 'Later', or 'OverwriteUnknown' if nothing can be determined
OverwriteResult DeadStoreEliminationPass::isOverwrite(const AliasAnalysis::Location &Later,
    const AliasAnalysis::Location &Earlier,
    AliasAnalysis &AA,
    int64_t &EarlierOff,
    int64_t &LaterOff) {
  const Value *P1 = Earlier.Ptr->stripPointerCasts();
  const Value *P2 = Later.Ptr->stripPointerCasts();

  // If the start pointers are the same, we just have to compare sizes to see if
  // the later store was larger than the earlier store.
  if (P1 == P2) {
    // If we don't know the sizes of either access, then we can't do a
    // comparison.
    if (Later.Size == AliasAnalysis::UnknownSize ||
        Earlier.Size == AliasAnalysis::UnknownSize) {
      // If we have no DataLayout information around, then the size of the store
      // is inferrable from the pointee type.  If they are the same type, then
      // we know that the store is safe.
      if (AA.getDataLayout() == 0 &&
          Later.Ptr->getType() == Earlier.Ptr->getType())
        return OverwriteComplete;

      return OverwriteUnknown;
    }

    // Make sure that the Later size is >= the Earlier size.
    if (Later.Size >= Earlier.Size)
      return OverwriteComplete;
  }

  // Otherwise, we have to have size information, and the later store has to be
  // larger than the earlier one.
  if (Later.Size == AliasAnalysis::UnknownSize ||
      Earlier.Size == AliasAnalysis::UnknownSize ||
      AA.getDataLayout() == 0)
    return OverwriteUnknown;

  // Check to see if the later store is to the entire object (either a global,
  // an alloca, or a byval argument).  If so, then it clearly overwrites any
  // other store to the same object.
  const DataLayout *TD = AA.getDataLayout();

  const Value *UO1 = GetUnderlyingObject(P1, TD),
        *UO2 = GetUnderlyingObject(P2, TD);

  // If we can't resolve the same pointers to the same object, then we can't
  // analyze them at all.
  if (UO1 != UO2)
    return OverwriteUnknown;

  // If the "Later" store is to a recognizable object, get its size.
  uint64_t ObjectSize = getPointerSize(UO2, AA);
  if (ObjectSize != AliasAnalysis::UnknownSize)
    if (ObjectSize == Later.Size && ObjectSize >= Earlier.Size)
      return OverwriteComplete;

  // Okay, we have stores to two completely different pointers.  Try to
  // decompose the pointer into a "base + constant_offset" form.  If the base
  // pointers are equal, then we can reason about the two stores.
  EarlierOff = 0;
  LaterOff = 0;
  const Value *BP1 = GetPointerBaseWithConstantOffset(P1, EarlierOff, TD);
  const Value *BP2 = GetPointerBaseWithConstantOffset(P2, LaterOff, TD);

  // If the base pointers still differ, we have two completely different stores.
  if (BP1 != BP2)
    return OverwriteUnknown;

  // The later store completely overlaps the earlier store if:
  //
  // 1. Both start at the same offset and the later one's size is greater than
  //    or equal to the earlier one's, or
  //
  //      |--earlier--|
  //      |--   later   --|
  //
  // 2. The earlier store has an offset greater than the later offset, but which
  //    still lies completely within the later store.
  //
  //        |--earlier--|
  //    |-----  later  ------|
  //
  // We have to be careful here as *Off is signed while *.Size is unsigned.
  if (EarlierOff >= LaterOff &&
      Later.Size >= Earlier.Size &&
      uint64_t(EarlierOff - LaterOff) + Earlier.Size <= Later.Size)
    return OverwriteComplete;

  // The other interesting case is if the later store overwrites the end of
  // the earlier store
  //
  //      |--earlier--|
  //                |--   later   --|
  //
  // In this case we may want to trim the size of earlier to avoid generating
  // writes to addresses which will definitely be overwritten later
  if (LaterOff > EarlierOff &&
      LaterOff < int64_t(EarlierOff + Earlier.Size) &&
      int64_t(LaterOff + Later.Size) >= int64_t(EarlierOff + Earlier.Size))
    return OverwriteEnd;

  // Otherwise, they don't completely overlap.
  return OverwriteUnknown;
}
/*
 * Clone functions, removing dead stores
 */
bool DeadStoreEliminationPass::cloneFunctions() {
  bool modified = false;
  for (std::map<Function*, std::vector<Instruction*> >::iterator it =
      fn2Clone.begin(); it != fn2Clone.end(); ++it) {

    Function *F = it->first;
    std::vector<Instruction*> callSitesToClone = it->second;
    std::map< std::set<Value*> , Function*> clonedFns;
    int i = 0;
    FunctionsCloned++;
    PromissorCalls += F->getNumUses();
    for (std::vector<Instruction*>::iterator it2 = callSitesToClone.begin();
        it2 != callSitesToClone.end(); ++it2, ++i) {

      Instruction* caller = *it2;
      std::set<Value*> deadArgs = deadArguments[caller];

      if (!clonedFns.count(deadArgs)) {
        // Clone function if a proper clone doesnt already exist
        std::stringstream suffix;
        suffix << ".deadstores" << i;
        Function* NF = cloneFunctionWithoutDeadStore(F, caller, suffix.str());
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

/*
 * Clone a given function removing dead stores
 */
Function* DeadStoreEliminationPass::cloneFunctionWithoutDeadStore(Function *Fn,
    Instruction* caller, std::string suffix) {

  Function *NF = Function::Create(Fn->getFunctionType(), Fn->getLinkage());
  NF->copyAttributesFrom(Fn);

  // Copy the parameter names, to ease function inspection afterwards.
  Function::arg_iterator NFArg = NF->arg_begin();
  for (Function::arg_iterator Arg = Fn->arg_begin(), ArgEnd = Fn->arg_end();
      Arg != ArgEnd; ++Arg, ++NFArg) {
    NFArg->setName(Arg->getName());
  }

  // To avoid name collision, we should select another name.
  NF->setName(Fn->getName() + suffix);

  // Fill clone content
  ValueToValueMapTy VMap;
  SmallVector<ReturnInst*, 8> Returns;
  Function::arg_iterator NI = NF->arg_begin();
  for (Function::arg_iterator I = Fn->arg_begin();
      NI != NF->arg_end(); ++I, ++NI) {
    VMap[I] = NI;
  }
  CloneAndPruneFunctionInto(NF, Fn, VMap, false, Returns);

  // Remove dead stores
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
        DEBUG(errs() << "will remove this store: " << *inst << "\n");
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

/*
 * Replace called function of a given call site.
 */
void DeadStoreEliminationPass::replaceCallingInst(Instruction* caller,
    Function* fn) {
  if (isa<CallInst>(caller)) {
    CallInst *callInst = dyn_cast<CallInst>(caller);
    callInst->setCalledFunction(fn);
  } else if (isa<InvokeInst>(caller)) {
    InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
    invokeInst->setCalledFunction(fn);
  }
}


void DeadStoreEliminationPass::printSet(raw_ostream &O,
    AliasSetTracker &myset) const {
  O << "    {\n";
  for (AliasSetTracker::const_iterator it = myset.begin();
      it != myset.end(); ++it) {
    O << "    ";
    (*it).print(O);
  }
  O << "    }\n";
}

void DeadStoreEliminationPass::print(raw_ostream &O, const Module *M) const {
  O << "Number of dead stores removed: " << RemovedStores << "\n";
}
