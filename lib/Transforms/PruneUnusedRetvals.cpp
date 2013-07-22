//===- UnusedRetvals.cpp - Count and/or prune unused return values --------===//
//
// This pass servers to count the occurrences of unused return values per
// module, as well as to clone them and prune the return value and substitute
// call instances to the pruned one.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "unused-retvals"
#include "llvm/Pass.h"
#include "llvm/PassSupport.h"
#include "llvm/IR/Attributes.h"
#include "llvm/InstVisitor.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/PUR.h"

#include <map>
#include <vector>

#include "IsUnusedRetval.h"

using namespace llvm;

STATISTIC(NrInstInUnusedRetvalFns, "Number of instructions in unused retval functions");
STATISTIC(NrInstInCloneFns, "Number of instructions in cloned functions");
STATISTIC(NrCloneFns, "Number of cloned functions");
STATISTIC(NrDiscardedCloneFns, "Number of discarded cloned functions");
STATISTIC(NrSubstCallInst, "Number of substituted instructions");

namespace {

  cl::opt<float> Ratio ("cloning-ratio",
    cl::desc("Only clone functions which new size "
             "is equal or smaller to a certain ratio [0, 1]."),
    cl::init(1.0));

  struct CountInstructions {
    int operator()(Function *Fn) {
      int count = 0;
      for (inst_iterator i = inst_begin(Fn), ie = inst_end(Fn);
           i != ie; ++i, ++count);
      return count;
    }
  };

  struct PruneUnusedRetvals :
    public ModulePass,
    public InstVisitor<PruneUnusedRetvals> {

    static char ID; // Pass identification, replacement for typeid

    typedef std::vector<CallInst*> CallRefsList;
    typedef std::map<Function*,CallRefsList> CallRefsMap;
    typedef std::map<Function*,Function*> Fn2CloneMap;

    CallRefsMap unusedRetvals;
    Fn2CloneMap clonedFunctions;

    PruneUnusedRetvals() : ModulePass(ID) {
      initializePruneUnusedRetvalsPass(*PassRegistry::getPassRegistry());
    }

    // Collect unused retvals
    void visitCallInst(CallInst &I) {
      Function *calledFunction = I.getCalledFunction();

      // We're not interested in indirect function calls.
      if (!calledFunction)
        return; 

      // There's no way to optimize external function calls.
      if (calledFunction->isDeclaration())
        return;

      // And we're interested in unused retvals...
      if (!isUnusedRetval(I))
        return;

      // Merge the found pair in the call refs map
      CallRefsMap::iterator ref = unusedRetvals.find(calledFunction);
      if (ref == unusedRetvals.end()) {
        CallInst *RI = &I;
        unusedRetvals.insert(std::make_pair(calledFunction,
          CallRefsList(&RI, (&RI)+1)));
      }
      else {
        ref->second.push_back(&I);
      }
    }

    // Clone referenced functions
    void cloneFunctions() {
      std::vector<Function*> recook;

      do {
        recook.clear();

        // First clone and add to clonedFunctions list
        for (CallRefsMap::iterator r = unusedRetvals.begin(),
             re = unusedRetvals.end(); r != re; ++r) {
          // Clone only if it wasn't cloned yet
          Function *Fn = r->first;
          Fn2CloneMap::iterator i = clonedFunctions.find(Fn);
          if (i == clonedFunctions.end()) {

            // Clone function and perform ADCE over it
            Function *Clone = cloneFunctionAsVoid(Fn);
            ADCE(Clone);

            // Perform the weight calculation
            std::pair<int,int> res;
            CountInstructions Count;
            res = std::make_pair(Count(Fn), Count(Clone));

            // If the clone function is not below the specified ratio,
            // maintain only the original function.
            float reduced = (float) res.second / (float) res.first;
            if (reduced > Ratio) {
              Clone->eraseFromParent(); // <-- Remove clone function
              errs() << "Not cloned: " << Fn->getName();
              NrDiscardedCloneFns++;
            }
            else {
              errs() << "Cloned: " << Fn->getName();
              clonedFunctions.insert(std::make_pair(Fn, Clone));
              recook.push_back(Clone);
              NrInstInUnusedRetvalFns += res.first;
              NrInstInCloneFns += res.second;
              NrCloneFns++;
            }
            errs() << " (in/out=" << res.first << "/" << res.second
                   << ";ratio=" << format("%.2f", reduced)
                   << ";refs=" << r->second.size()
                   << ")\n";
          }
        }

        // Recook cloned functions adding unused retvals
        // into the unusedRetvals map.
        for (std::vector<Function*>::iterator f = recook.begin(),
             fe = recook.end(); f != fe; ++f) {
          errs() << "Recooking: " << (*f)->getName() << "\n";
          visit(*f); // just revist it!...
        }

      } while (recook.size() > 0);
    }

    // Remove attributes such as zeroext, signext, inreg and noalias
    // attributes in return types; they could transform the output in
    // invalid call instructions such as 'call signext void'.
    template<class T>
    void removeRetvalAttributes(T *V) {
      // Set up to build a new list of retval attributes.
      AttributeSet RAttrs = V->getAttributes().getRetAttributes();

      // We want to obtain a void return type
      Type *VoidTy = Type::getVoidTy(V->getContext());

      // Remove all incompatible attributes for the void return type
      RAttrs =
        AttributeSet::get(V->getContext(), AttributeSet::ReturnIndex,
                          AttrBuilder(RAttrs, AttributeSet::ReturnIndex).
           removeAttributes(AttributeFuncs::
                            typeIncompatible(VoidTy, AttributeSet::ReturnIndex),
                            AttributeSet::ReturnIndex));

      // Set them back
      V->setAttributes(RAttrs);
    }

    // Clone the given function pruning the return value
    Function *cloneFunctionAsVoid(Function *Fn) {

      // Start by computing a new prototype for the function, which is the
      // same as the old function, but the return type is void.
      FunctionType *FTy = Fn->getFunctionType();

      std::vector<Type*> Params(FTy->param_begin(), FTy->param_end());
      FunctionType *NFTy = FunctionType::get(Type::getVoidTy(Fn->getContext()),
                                                      Params, Fn->isVarArg());

      Function *NF = Function::Create(NFTy, Fn->getLinkage());

      // Instead of using copyAttributesFrom, we should use our own version,
      // as we don't want to copy attributes used in the retval.
      NF->copyAttributesFrom(Fn);
      removeRetvalAttributes(NF);

      // After the parameters have been copied, we should copy the parameter
      // names, to ease function inspection afterwards.
      Function::arg_iterator NFArg = NF->arg_begin();
      for (Function::arg_iterator Arg = Fn->arg_begin(),
           ArgEnd = Fn->arg_end(); Arg != ArgEnd; ++Arg, ++NFArg) {
        NFArg->setName(Arg->getName());
      }

      // To avoid name collision, we should select another name.
      NF->setName(Fn->getName() + ".noret");

      // Now, fill the function contents
      {
        ValueToValueMapTy VMap;
        SmallVector<ReturnInst*, 8> Returns;

        Function::arg_iterator NI = NF->arg_begin();
        for (Function::arg_iterator I = Fn->arg_begin();
             NI != NF->arg_end(); ++I, ++NI) {
          VMap[I] = NI;
        }

        CloneAndPruneFunctionInto(NF, Fn, VMap, false, Returns);
      }

      // Insert the clone function before the original
      Fn->getParent()->getFunctionList().insert(Fn, NF);

      return removeReturnInst(NF);
    }

    // Substitute the return value instructions by return void
    Function* removeReturnInst(Function* F) {

      // Collect them all first, as we can't remove them while iterating.
      // While iterating, we can add the new retvals (ret void).
      SmallPtrSet<ReturnInst*, 4> rets;
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {

        if (!I->isTerminator())
          continue; // ignore non terminals

        if (ReturnInst* retInst = dyn_cast<ReturnInst>(&*I)) {

          // Create the return void instruction
          ReturnInst::Create(F->getContext(), 0, retInst);

          // Save return value instruction for removal
          rets.insert(retInst);
        }
      }

      // Now, remove all return values
      for (SmallPtrSet<ReturnInst*, 4>::iterator i = rets.begin(),
           ie = rets.end(); i != ie; ++i) {
        (*i)->eraseFromParent();
      }

      return F;
    }

    // Substitute calling instructions by the noret clone
    void substCallingInstructions() {
      for (CallRefsMap::iterator r = unusedRetvals.begin(),
           re = unusedRetvals.end(); r != re; ++r) {
        for (CallRefsList::iterator i = r->second.begin(),
             ie = r->second.end(); i != ie; ++i) {
          CallInst *I = (*i);
          if (clonedFunctions.find(r->first) != clonedFunctions.end()) {
            *i = cloneCallInstAsVoid(I, clonedFunctions[r->first]);
            I->eraseFromParent();
            NrSubstCallInst++;
          }
        }
      }
      // Now the final mappings contain the result of the computations
    }

    // Clone the existing call instruction by a void function call
    CallInst *cloneCallInstAsVoid(CallInst *I, Function *FVoid) {
      std::vector<Value*> Args(I->op_begin(), I->op_end());
      Args.pop_back(); // remove the referencing name, as it will be void

      // Create a custom clone of the original instruction. This code has been
      // copied from CallInst copy constructor (refer to it's source code).
      CallInst *callInstruction = cast<CallInst>(I);
      CallInst *clone = CallInst::Create(FVoid, Args, "", I);
      clone->setAttributes(callInstruction->getAttributes());
      clone->setTailCall(callInstruction->isTailCall());
      clone->setCallingConv(callInstruction->getCallingConv());

      // Remove zeroext, signext, inreg and noalias attributes.
      removeRetvalAttributes(clone);

      return clone;
    }

    // Apply the ADCE algorithm over the function.
    // -- Taken from lib/Transforms/Scalar/ADCE.cpp
    void ADCE(Function *F) {
      SmallPtrSet<Instruction*, 128> alive;
      SmallVector<Instruction*, 128> worklist;

      // Collect the set of "root" instructions that are known live.
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        if (isa<TerminatorInst>(I.getInstructionIterator()) ||
            isa<DbgInfoIntrinsic>(I.getInstructionIterator()) ||
            isa<LandingPadInst>(I.getInstructionIterator()) ||
            I->mayHaveSideEffects()) {
          alive.insert(I.getInstructionIterator());
          worklist.push_back(I.getInstructionIterator());
        }

      // Propagate liveness backwards to operands.
      while (!worklist.empty()) {
        Instruction* curr = worklist.pop_back_val();
        for (Instruction::op_iterator OI = curr->op_begin(), OE = curr->op_end();
             OI != OE; ++OI)
          if (Instruction* Inst = dyn_cast<Instruction>(OI))
            if (alive.insert(Inst))
              worklist.push_back(Inst);
      }

      // The inverse of the live set is the dead set.  These are those instructions
      // which have no side effects and do not influence the control flow or return
      // value of the function, and may therefore be deleted safely.
      // NOTE: We reuse the worklist vector here for memory efficiency.
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I)
        if (!alive.count(I.getInstructionIterator())) {
          worklist.push_back(I.getInstructionIterator());
          I->dropAllReferences();
        }

      for (SmallVector<Instruction*, 1024>::iterator I = worklist.begin(),
           E = worklist.end(); I != E; ++I) {
        (*I)->eraseFromParent();
      }
    }

    // Prune all unused retvals available in the module
    virtual bool runOnModule(Module &M) {
      visit(M); // Collect unused retvals
      cloneFunctions();
      substCallingInstructions();
      return clonedFunctions.size() > 0;
    }

    // As we're cloning functions, the CFG won't be preserved.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {}
  };
}

char PruneUnusedRetvals::ID = 0;
INITIALIZE_PASS(PruneUnusedRetvals, "prune-unused-retvals", "Prune unused retvals pass", false, false)

ModulePass *llvm::createPruneUnusedRetvalsPass() {
  return new PruneUnusedRetvals();
}

