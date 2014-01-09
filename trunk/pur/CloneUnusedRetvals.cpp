//===- CloneUnusedRetvals.cpp - Clone all unused return values ------------===//
//
// This pass substitutes call sites, where the return value is not used, by a
// clone where the return value is pruned.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "clone-unused-retvals"
#include "llvm/Pass.h"
#include "llvm/PassSupport.h"
#include "llvm/InstVisitor.h"
#include "llvm/IR/Attributes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <map>
#include <vector>

using namespace llvm;

STATISTIC(NrFns,               "Number of functions");
STATISTIC(NrCloneFns,          "Number of cloned functions");
STATISTIC(NrCallInst,          "Number of calls");
STATISTIC(NrPotentialCallInst, "Number of promissor calls");
STATISTIC(NrSubstCallInst,     "Number of replaced calls");

namespace {

  struct CloneUnusedRetvals :
    public ModulePass,
    public InstVisitor<CloneUnusedRetvals> {

    static char ID; // Pass identification

    typedef std::vector<CallSite> CallRefsList;
    typedef std::map<Function*,CallRefsList> CallRefsMap;
    typedef std::map<Function*,Function*> Fn2CloneMap;

    CallRefsMap unusedRetvals;
    Fn2CloneMap clonedFunctions;

    CloneUnusedRetvals() : ModulePass(ID) {
      NrFns = 0;
      NrCloneFns = 0;
      NrCallInst = 0;
      NrPotentialCallInst = 0;
      NrSubstCallInst = 0;
    }

    /// Check whether the call site is referring to an unused retval
    bool isUnusedRetval(CallSite CS) {
      Function *calledFunction = CS.getCalledFunction();

      // The called function doesn't return value (void), ignore.
      if (calledFunction->getReturnType()->getTypeID() == Type::VoidTyID)
        return false;

      // If the instruction has one or more uses, it means that the return
      // value is being used by the calling function.
      Instruction *Call = CS.getInstruction();
      if (Call->hasNUsesOrMore(1))
        return false;

      // So, we can conclude that the return value is not being used.
      return true;
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

      // Remove all incompatible attributes for the void return type.
      // This approach maintains compatibility with upcoming LLVM versions.
      RAttrs =
        AttributeSet::get(V->getContext(), AttributeSet::ReturnIndex,
                          AttrBuilder(RAttrs, AttributeSet::ReturnIndex).
           removeAttributes(AttributeFuncs::
                            typeIncompatible(VoidTy, AttributeSet::ReturnIndex),
                            AttributeSet::ReturnIndex));

      // Set them back
      V->setAttributes(RAttrs);
    }

    // Collect unused retvals
    void visitCallSite(CallSite CS) {
      Function *calledFunction = CS.getCalledFunction();

      // We're not interested in indirect function calls.
      if (!calledFunction)
        return; 

      // There's no way to optimize external function calls.
      if (calledFunction->isDeclaration())
        return;

      // And we're interested in unused retvals...
      if (!isUnusedRetval(CS))
        return;

      // Merge the found pair in the call refs map
      CallRefsMap::iterator ref = unusedRetvals.find(calledFunction);
      if (ref == unusedRetvals.end()) {
        unusedRetvals[calledFunction].push_back(CS);
      }
      else {
        ref->second.push_back(CS);
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
          // Clone only if it was not cloned yet
          Function *Fn = r->first;
          Fn2CloneMap::iterator i = clonedFunctions.find(Fn);
          if (i == clonedFunctions.end()) {

            NrCloneFns++;
            NrPotentialCallInst += Fn->getNumUses();

            // Clone function
            Function *Clone = cloneFunctionAsVoid(Fn);

            clonedFunctions.insert(std::make_pair(Fn, Clone));
            recook.push_back(Clone);

            DEBUG(errs() << "Cloned: " << Fn->getName()
                         << " (refs=" << r->second.size() << ")\n");
          }
        }

        // Recook cloned functions adding unused retvals
        // into the unusedRetvals map.
        for (std::vector<Function*>::iterator f = recook.begin(),
             fe = recook.end(); f != fe; ++f) {
          DEBUG(errs() << "Recooking: " << (*f)->getName() << "\n");
          visit(*f); // just revisit it!...
        }

      } while (recook.size() > 0);
    }

    // Clone the given function pruning the return value
    Function *cloneFunctionAsVoid(Function *Fn) {

      // Start by computing a new prototype for the function, which is the
      // same as the old function, but the return type is void.
      FunctionType *FTy = Fn->getFunctionType();

      std::vector<Type*> Params(FTy->param_begin(), FTy->param_end());
      FunctionType *NFTy = FunctionType::get(Type::getVoidTy(Fn->getContext()),
                                                      Params, Fn->isVarArg());

      // Clone functions will have the same linkage as the original for now
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
          CallSite CS = *i;
          if (clonedFunctions.find(r->first) != clonedFunctions.end()) {
            // Count substituted call instructions, but do not consider
            // those in cloned functions.
            if (!CS.getCalledFunction()->getName().endswith(".noret"))
                NrSubstCallInst++;
            *i = cloneCallSiteAsVoid(CS, clonedFunctions[r->first]);
          }
        }
      }
      // Now the final mappings contain the result of the computations
    }

    // Clone the existing call instruction by a void function call
    CallSite cloneCallSiteAsVoid(CallSite CS, Function *FVoid) {
      Instruction *Call = CS.getInstruction();

      // Reuse same arguments
      std::vector<Value*> Args(CS.arg_begin(), CS.arg_end());
  
      Instruction *NC; // Create the new call or invoke instruction.
      if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
        NC = InvokeInst::Create(FVoid, II->getNormalDest(),
                                II->getUnwindDest(), Args, "", Call);
        cast<InvokeInst>(NC)->setCallingConv(II->getCallingConv());
        cast<InvokeInst>(NC)->setAttributes(II->getAttributes());
        removeRetvalAttributes(cast<InvokeInst>(NC));
      } else {
        CallInst *CI = cast<CallInst>(Call);
        NC = CallInst::Create(FVoid, Args, "", Call);
        if (CI->isTailCall())
          cast<CallInst>(NC)->setTailCall();
        cast<CallInst>(NC)->setCallingConv(CI->getCallingConv());
        cast<CallInst>(NC)->setAttributes(CI->getAttributes());
        removeRetvalAttributes(cast<CallInst>(NC));
      }
  
      if (!Call->use_empty())
        Call->replaceAllUsesWith(NC);
      
      // Finally, remove the old call from the program, reducing the
      // use-count of Fn.
      Call->getParent()->getInstList().erase(Call);

      return CallSite(NC);
    }

    void getStats(Module &M) {
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        if (!F->isDeclaration()) {
          NrFns++;
          if (!F->use_empty()) NrCallInst += F->getNumUses();
        }
      }
    }

    // Prune all unused retvals available in the module
    virtual bool runOnModule(Module &M) {
      getStats(M);
      visit(M); // Collect unused retvals
      cloneFunctions();
      substCallingInstructions();
      return clonedFunctions.size() > 0;
    }

    // As we're cloning functions, the CFG won't be preserved.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {}
  };

} // end empty namespace.

char CloneUnusedRetvals::ID = 0;
static RegisterPass<CloneUnusedRetvals> X("clone-unused-retvals", "Clone unused retvals functions", false, false);
