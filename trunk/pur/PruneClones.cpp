//===- PruneClonesClones.cpp - Prune unused retval clones ----------===//
//
// This pass prunes unused retvals clones based on size reduction criteria.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "prune-unused-retvals-clones"
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
#include "llvm/Transforms/Utils/Cloning.h"

#include <map>
#include <vector>

using namespace llvm;

STATISTIC(NrCloneFns, "Number of remaining clone functions");
STATISTIC(NrOrphanedClones, "Number of orphaned clone functions");
STATISTIC(NrInstInUnusedRetvalFns, "Number of instructions in unused retval functions");
STATISTIC(NrInstInCloneFns, "Number of instructions in clone functions");
STATISTIC(NrPrunedCloneFns, "Number of pruned clone functions");
STATISTIC(NrRestoredCallSites, "Number of restored call sites");

namespace {

  cl::opt<float> Ratio ("prune-clones-ratio",
    cl::desc("Only maintain clones which new size "
             "is equal or smaller to a certain ratio [0, 1]. "
             "Defaults to 0.98."),
    cl::init(0.98));

  struct CountInstructions {
    int operator()(Function *Fn) {
      int count = 0;
      for (inst_iterator i = inst_begin(Fn), ie = inst_end(Fn);
           i != ie; ++i, ++count);
      return count;
    }
  };

  struct PruneClones :
    public ModulePass,
    public InstVisitor<PruneClones> {

    static char ID; // Pass identification

    typedef std::vector<CallInst*> CallRefsList;
    typedef std::map<Function*,CallRefsList> CallRefsMap;
    typedef std::map<StringRef,Function*> Name2FuncMap;
    typedef std::map<Function*,Function*> Fn2CloneMap;

    Name2FuncMap originals;
    Name2FuncMap clones;
    Fn2CloneMap pairs;
    bool changed;

    PruneClones() : ModulePass(ID), changed(false) { }

    // Separate functions into two distinct sets: clones and originals
    void visitFunction(Function &F) {
      if (F.getName().endswith(".noret")) {
        clones.insert(std::make_pair(F.getName(), &F));
      }
      else {
        originals.insert(std::make_pair(F.getName(), &F));
      }
    }

    // Collect pairs of originals and cloned functions
    void collectPairs() {
      for (Name2FuncMap::iterator i = clones.begin(), ie = clones.end();
           i != ie; ++i) {
        std::string name(i->first.data(),
          i->first.size() - std::strlen(".noret"));
        Name2FuncMap::iterator j = originals.find(name);
        if (j != originals.end()) {
          pairs.insert(std::make_pair(j->second, i->second));
        }
        else {
          // In this case, the previous passes could have removed all
          // references to the original function, and could have pruned it.
          DEBUG(errs() << "Orphaned: " << i->first << "\n");
          NrOrphanedClones++;
          NrCloneFns++;
        }
      }
    }

    // Prune clones based on the size reduction ratio
    void pruneClones() {

      // Substitute all call sites
      for (Fn2CloneMap::iterator i = pairs.begin(), ie = pairs.end();
           i != ie; ++i) {

        Function *Fn = i->first;
        Function *Clone = i->second;

        // Perform the weight calculation
        std::pair<int,int> res;
        CountInstructions Count;
        res = std::make_pair(Count(Fn), Count(Clone));

        // If the clone function is not below the specified ratio,
        // maintain only the original function.
        float reduced = (float) res.second / (float) res.first;
        if (reduced > Ratio && substituteCallSites(Fn, Clone)) {
          DEBUG(errs() << "Pruned clone: " << Clone->getName()
                       << " (" << format("%.2f", reduced) << ")\n");
          Clone->eraseFromParent();
          NrPrunedCloneFns++;
          changed = true;
        }
        else {
          // Count only remaining cloned functions
          DEBUG(errs() << "Kept clone: " << Clone->getName()
                       << " (" << format("%.2f", reduced) << ", "
                       << "uses=" << Clone->getNumUses() << ")\n");
          NrCloneFns++;
          NrInstInUnusedRetvalFns += res.first;
          NrInstInCloneFns += res.second;
        }
      }
    }

    // Substitutes all call sites by the original function
    bool substituteCallSites(Function *Fn, Function *Clone) {

      // If the original and clone parameters don't match, this substitution
      // is not possible anymore due to previous optimizations.
      FunctionType *FnTy = Fn->getFunctionType();
      FunctionType *CloneTy = Clone->getFunctionType();

      if (FnTy->getNumParams() != CloneTy->getNumParams())
        return false;

      for (unsigned i = 0, e = FnTy->getNumParams(); i != e; ++i) {
        Type *FnParamTy = FnTy->getParamType(i);
        Type *CloneParamTy = CloneTy->getParamType(i);
        if (FnParamTy != CloneParamTy)
          return false;
      }

      // Loop over all of the callers of the clone, transforming the call sites
      // to pass in the loaded pointers.
      std::vector<Value*> Args;
      while (!Clone->use_empty()) {
        CallSite CS(Clone->use_back());
        Instruction *Call = CS.getInstruction();

        // Reuse same arguments
        std::vector<Value*> Args(CS.arg_begin(), CS.arg_end());
  
        Instruction *New; // Create the new call or invoke instruction.
        if (InvokeInst *II = dyn_cast<InvokeInst>(Call)) {
          New = InvokeInst::Create(Fn, II->getNormalDest(),
                                   II->getUnwindDest(), Args, "", Call);
          cast<InvokeInst>(New)->setCallingConv(II->getCallingConv());
          cast<InvokeInst>(New)->setAttributes(II->getAttributes());
        } else {
          CallInst *CI = dyn_cast<CallInst>(Call);
          New = CallInst::Create(Fn, Args, "", Call);
          if (CI->isTailCall())
            cast<CallInst>(New)->setTailCall();
          cast<CallInst>(New)->setCallingConv(CI->getCallingConv());
          cast<CallInst>(New)->setAttributes(CI->getAttributes());
        }
        Args.clear();
  
        if (!Call->use_empty())
          Call->replaceAllUsesWith(New);
        
        // Finally, remove the old call from the program, reducing the
        // use-count of the clone.
        Call->getParent()->getInstList().erase(Call);
        NrRestoredCallSites++;
      }

      return true;
    }

    // Prune all unused retvals available in the module
    virtual bool runOnModule(Module &M) {
      visit(M); // Collect data
      collectPairs();
      pruneClones();
      return changed;
    }

    // As we're removing cloned functions, the CFG won't be preserved.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {}
  };
}

char PruneClones::ID = 0;
static RegisterPass<PruneClones> X("prune-clones", "Prune worthless function clones", false, false);
