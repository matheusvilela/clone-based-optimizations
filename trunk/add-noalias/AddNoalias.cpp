#include "AddNoalias.h"

using namespace llvm;

void AddNoalias::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<PADriver>();
  AU.setPreservesAll();
}

AddNoalias::AddNoalias() : ModulePass(ID) {
  NoAliasPotentialFunctions = 0;
  NoAliasClonedFunctions = 0;
  NoAliasPotentialCalls = 0;
  NoAliasClonedCalls = 0;
  NoAliasTotalCalls = 0;
}

bool AddNoalias::runOnModule(Module &M) {

  PAD = &getAnalysis<PADriver>();
  // Collect information
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      NoAliasPotentialFunctions++;

      if (F->arg_empty() || F->use_empty()) continue;

      for (Value::use_iterator UI = F->use_begin(), E = F->use_end(); UI != E; ++UI) {
        User *U = *UI;

        if (isa<BlockAddress>(U)) continue;
        if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;

        CallSite CS(cast<Instruction>(U));
        if (!CS.isCallee(UI))
          continue;

        CallSite::arg_iterator actualArgIter = CS.arg_begin();
        Function::arg_iterator formalArgIter = F->arg_begin();
        int size = F->arg_size();

        NoAliasTotalCalls++;
        for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
          Value *actualArg = *actualArgIter;
          Value *formalArg = formalArgIter;

          //store arguments if formal argument is a  pointer
          if (formalArg->getType()->isPointerTy()) {
            arguments[U].push_back(std::make_pair(formalArgIter, actualArg));
          }
        }
      }

    }
  }

  // Add noalias to arguments
  collectFn2Clone();
  bool modified = cloneFunctions();

  return modified;
}

// clone functions and replace its callers
bool AddNoalias::cloneFunctions() {
  std::map<Function*, Function*> clonedFunctions;
  for(std::map< Function*, std::vector<User*> >::iterator it = fn2Clone.begin(); it != fn2Clone.end(); ++it) {
    Function *f                = it->first;
    std::vector<User*> callers = it->second;

    Function* NF = cloneFunctionWithNoAliasArgs(f);
    clonedFunctions[f] = NF;

    substCallingInstructions(NF, callers);
    NoAliasClonedFunctions++;
    NoAliasClonedCalls += callers.size();
  }
  for(std::map<Function*, Function*>::iterator it = clonedFunctions.begin(); it != clonedFunctions.end(); ++it) {
    Function *original = it->first;
    Function *clonedFn = it->second;
    fillCloneContent(original, clonedFn);
  }
  return NoAliasClonedFunctions > 0;
}

void AddNoalias::fillCloneContent(Function* original, Function* clonedFn) {
  ValueToValueMapTy VMap;
  SmallVector<ReturnInst*, 8> Returns;

  Function::arg_iterator NI = clonedFn->arg_begin();
  for (Function::arg_iterator I = original->arg_begin();
      NI != clonedFn->arg_end(); ++I, ++NI) {
    VMap[I] = NI;
  }

  CloneAndPruneFunctionInto(clonedFn, original, VMap, false, Returns);
}

// Clone the given function adding noalias attribute to arguments
Function* AddNoalias::cloneFunctionWithNoAliasArgs(Function *Fn) {

  // Start by computing a new prototype for the function, which is the
  // same as the old function
  Function *NF = Function::Create(Fn->getFunctionType(), Fn->getLinkage());
  NF->copyAttributesFrom(Fn);

  // After the parameters have been copied, we should copy the parameter
  // names, to ease function inspection afterwards.
  Function::arg_iterator NFArg = NF->arg_begin();
  for (Function::arg_iterator Arg = Fn->arg_begin(), ArgEnd = Fn->arg_end(); Arg != ArgEnd; ++Arg, ++NFArg) {
    NFArg->setName(Arg->getName());

    // We should also add NoAlias attr to parameters that are pointers
    if (NFArg->getType()->isPointerTy()) {
      AttrBuilder noalias(Attribute::get(NFArg->getContext(), Attribute::NoAlias));
      int argNo = NFArg->getArgNo() + 1;
      NFArg->addAttr(AttributeSet::get(NFArg->getContext(), argNo, noalias));
    }
  }

  // To avoid name collision, we should select another name.
  NF->setName(Fn->getName() + ".noalias");

  // Insert the clone function before the original
  Fn->getParent()->getFunctionList().insert(Fn, NF);

  return NF;
}

// replace calling instruction
void AddNoalias::substCallingInstructions(Function* NF, std::vector<User*> callers) {
  for (std::vector<User*>::iterator it = callers.begin(); it != callers.end(); ++it) {
    User* caller = *it;
    if (isa<CallInst>(caller)) {
      CallInst *callInst = dyn_cast<CallInst>(caller);
      callInst->setCalledFunction(NF);
    } else if (isa<InvokeInst>(caller)) {
      InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
      invokeInst->setCalledFunction(NF);
    }
  }
}

// collect function to clone
void AddNoalias::collectFn2Clone() {

  for (std::map< User*, std::vector< std::pair<Argument*, Value*> > >::iterator fit = arguments.begin(); fit != arguments.end(); ++fit) {

    User* caller                                     = fit->first;
    std::vector< std::pair<Argument*, Value*> > args = fit->second;

    // map every actual argument of a User with its possible values
    std::map<Value*, std::set<int> > argsValues;
    for (std::vector< std::pair<Argument*, Value*> >::iterator it = args.begin(); it != args.end(); ++it) {
      int argumentID         = PAD->Value2Int(it->second);
      argsValues[it->second] = PAD->pointerAnalysis->pointsTo(argumentID);
    }

    // verify, for every actual argument, if its possible values collide with others arguments values
    int intersectionCount = 0;
    for (std::vector< std::pair<Argument*, Value*> >::iterator it = args.begin(); it != args.end(); ++it) {
      std::set<int> myValues = argsValues[it->second];

      for (std::vector< std::pair<Argument*, Value*> >::iterator it2 = args.begin(); it2 != args.end(); ++it2) {
        if (*it2 == *it) continue;
        if (intersectionCount != 0) break;

        std::set<int> otherValues = argsValues[it2->second];
        std::vector<int> intersection(myValues.size() + otherValues.size());
        std::vector<int>::iterator intersectionIt = std::set_intersection(myValues.begin(), myValues.end(), otherValues.begin(), otherValues.end(), intersection.begin());
        intersection.resize(intersectionIt - intersection.begin());
        intersectionCount += intersection.size();

      }
      if (intersectionCount != 0) break;
    }

    if (intersectionCount == 0 && args.size() > 1) {
      if (isa<CallInst>(caller)) {
        CallInst *callInst = dyn_cast<CallInst>(caller);
        Function* f        = callInst->getCalledFunction();
        if (!f->hasAvailableExternallyLinkage()) {
          fn2Clone[f].push_back(caller);
        }
      } else if (isa<InvokeInst>(caller)) {
        InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
        Function* f            = invokeInst->getCalledFunction();
        if (!f->hasAvailableExternallyLinkage()) {
          fn2Clone[f].push_back(caller);
        }
      }
    }
  }
  // Collect stats
  for (std::map< User*, std::vector< std::pair<Argument*, Value*> > >::iterator fit = arguments.begin(); fit != arguments.end(); ++fit) {
    User* caller = fit->first;
    Function* f;
    if (isa<CallInst>(caller)) {
      CallInst *callInst = dyn_cast<CallInst>(caller);
      f = callInst->getCalledFunction();
    } else {
      InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
      f = invokeInst->getCalledFunction();
    }
    if (fn2Clone.count(f) > 0) {
      NoAliasPotentialCalls++;
    }
  }
}

void AddNoalias::print(raw_ostream& O, const Module* M) const {
  O << "Number of functions: " << NoAliasPotentialFunctions << '\n';
  O << "Number of calls: " << NoAliasTotalCalls << '\n';
  O << "Number of cloned functions: " << NoAliasClonedFunctions << '\n';
  O << "Number of potential calls: " << NoAliasPotentialCalls << '\n';
  O << "Number of calls replaced: " << NoAliasClonedCalls << '\n';
}

// Register the pass to the LLVM framework
char AddNoalias::ID = 0;
static RegisterPass<AddNoalias> X("add-noalias", "Add noalias attribute to parameters.");
