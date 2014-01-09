#undef  DEBUG_TYPE
#define DEBUG_TYPE "dead-store-elimination"
#include <sstream>
#include <set>

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"

namespace llvm {
  STATISTIC(RemovedStores,   "Number of removed stores.");
  STATISTIC(FunctionsCount,  "Total number of functions.");
  STATISTIC(FunctionsCloned, "Number of functions cloned.");
  STATISTIC(ClonesCount,     "Number of functions that are clones.");
  STATISTIC(CallsCount,      "Total number of calls.");
  STATISTIC(PromissorCalls,  "Number of promissor calls.");
  STATISTIC(CallsReplaced,   "Number of calls replaced.");

  enum OverwriteResult {
    OverwriteComplete,
    OverwriteEnd,
    OverwriteUnknown
  };

  class DeadStoreEliminationPass : public ModulePass {

    // Functions that store on arguments
    std::map<Function*, std::set<Value*> > fnThatStoreOnArgs;

    // Arguments that have dead stores
    std::map< Instruction*, std::set<Value*> > deadArguments;

    // Function to be cloned
    std::map<Function*, std::vector<Instruction*> > fn2Clone;

    // VisitedPHIs - The set of PHI nodes visited when determining
    /// if a variable's reference has been taken.  This set
    /// is maintained to ensure we don't visit the same PHI node multiple
    /// times.
    SmallPtrSet<const PHINode*, 16> VisitedPHIs;

    AliasAnalysis *AA;
    MemoryDependenceAnalysis *MDA;

   public:
    static char ID;

    DeadStoreEliminationPass();

    Function* cloneFunctionWithoutDeadStore(Function *Fn, Instruction* caller, std::string suffix);
    OverwriteResult isOverwrite(const AliasAnalysis::Location &Later, const AliasAnalysis::Location &Earlier, AliasAnalysis &AA, int64_t &EarlierOff, int64_t &LaterOff);
    bool changeLinkageTypes(Module &M);
    bool cloneFunctions();
    bool hasAddressTaken(const Instruction *AI, CallSite& CS);
    bool isRefAfterCallSite(Value* v, CallSite &CS);
    bool runOnModule(Module &M);
    int getFnThatStoreOnArgs(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const;
    void print(raw_ostream &O, const Module *M) const;
    void printSet(raw_ostream &O, AliasSetTracker &myset) const;
    void replaceCallingInst(Instruction* caller, Function* fn);
    void runNotUsedDeadStoreAnalysis();
    void runOverwrittenDeadStoreAnalysis(Module &M);
    void runOverwrittenDeadStoreAnalysisOnFn(Function &F);
  };
  char DeadStoreEliminationPass::ID = 0;
}
