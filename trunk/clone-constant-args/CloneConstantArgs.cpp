#include "CloneConstantArgs.h"

using namespace llvm;

CloneConstantArgs::CloneConstantArgs() : ModulePass(ID) {
  FunctionsCount    = 0;
  FunctionsCloned   = 0;
  ClonesCount       = 0;
  CallsCount        = 0;
  PromissorCalls    = 0;
  CallsReplaced     = 0;
}

bool CloneConstantArgs::runOnModule(Module &M) {

  findConstantArgs(M);
  collectFn2Clone();
  bool modified = cloneFunctions();

  return modified;
}

void CloneConstantArgs::findConstantArgs(Module &M) {
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      FunctionsCount++;

      if (F->use_empty()) continue;
      CallsCount += F->getNumUses();

      for (Value::use_iterator UI = F->use_begin(), E = F->use_end(); UI != E; ++UI) {
        User *U = *UI;

        if (isa<BlockAddress>(U)) continue;
        if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;

        CallSite CS(cast<Instruction>(U));
        if (!CS.isCallee(UI)) continue;

        if(F->arg_empty()) break;

        Function::arg_iterator formalArgIter = F->arg_begin();
        CallSite::arg_iterator actualArgIter = CS.arg_begin();
        int size = F->arg_size();

        for (int i = 0; i < size; ++i, ++actualArgIter, ++formalArgIter) {
          Value *actualArg = *actualArgIter;

          if (isa<Constant>(actualArg)) {
            arguments[U].push_back(std::make_pair(formalArgIter, actualArg));
          }
        }
 
      }
    }
  }
}

void CloneConstantArgs::collectFn2Clone() {

  for(std::map< User*, std::vector< std::pair<Argument*, Value*> > >::iterator it = arguments.begin();
      it != arguments.end(); ++it) {
    User* caller = it->first;

      if (isa<CallInst>(caller)) {
        CallInst *callInst = dyn_cast<CallInst>(caller);
        Function* f        = callInst->getCalledFunction();
        if (!f->hasAvailableExternallyLinkage()) {
          if (!fn2Clone.count(f)) PromissorCalls += f->getNumUses();
          fn2Clone[f].push_back(caller);
        }
      } else if (isa<InvokeInst>(caller)) {
        InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
        Function* f            = invokeInst->getCalledFunction();
        if (!f->hasAvailableExternallyLinkage()) {
          if (!fn2Clone.count(f)) PromissorCalls += f->getNumUses();
          fn2Clone[f].push_back(caller);
        }
      }
  }

}

// clone functions and replace its callers
bool CloneConstantArgs::cloneFunctions() {
  bool modified = false;
  for(std::map< Function*, std::vector <User*> >::iterator it = fn2Clone.begin();
      it != fn2Clone.end(); ++it) {
  
    FunctionsCloned++;
    Function *F = it->first;

    std::map< std::vector< std::pair<Argument*, Value*> >, Function*> clonedFns;
    for(unsigned long i = 0; i < it->second.size(); i++) {
      User* caller = it->second.at(i);
      std::vector< std::pair<Argument*, Value*> > userArgs = arguments[caller];

      if (!clonedFns.count(userArgs)) {
        // Clone function if a proper clone doesnt already exist
        std::stringstream suffix;
        suffix << ".constargs" << i;
        Function* NF = cloneFunctionWithConstArgs(F, caller, suffix.str());
        replaceCallingInst(caller, NF);
        clonedFns[userArgs] = NF;
        ClonesCount++;
      } else {
        // Use existing clone
        Function* NF = clonedFns.at(userArgs);
        replaceCallingInst(caller, NF);
      }
      CallsReplaced++;

      modified = true;
    }
  }
  return modified;
}

void CloneConstantArgs::replaceCallingInst(User* caller, Function* fn) {
  if (isa<CallInst>(caller)) {
    CallInst *callInst = dyn_cast<CallInst>(caller);
    callInst->setCalledFunction(fn);
  } else if (isa<InvokeInst>(caller)) {
    InvokeInst *invokeInst = dyn_cast<InvokeInst>(caller);
    invokeInst->setCalledFunction(fn);
  }
}

// Clone the given function adding noalias attribute to arguments
Function* CloneConstantArgs::cloneFunctionWithConstArgs(Function *Fn, User* caller, std::string suffix) {

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

  // Replace uses from constant args
  std::map<Argument*, Value*> argsMap;
  for(std::vector< std::pair<Argument*, Value*> >::iterator it = arguments[caller].begin();
      it != arguments[caller].end(); ++it) {
    std::pair<Argument*, Value*> argPair = *it;
    argsMap[argPair.first] = argPair.second;
  }

  Function::arg_iterator NFArgIter = NF->arg_begin();
  for (Function::arg_iterator FnArgIter = Fn->arg_begin();
      FnArgIter != Fn->arg_end(); ++FnArgIter, ++NFArgIter) {
    Value *formalArg = NFArgIter;
    if (argsMap.count(FnArgIter)) {
      Value *actualArg = argsMap[FnArgIter];
      formalArg->replaceAllUsesWith(actualArg);
    }
  }

  // Insert the clone function before the original
  Fn->getParent()->getFunctionList().insert(Fn, NF);

  return NF;
}

void CloneConstantArgs::print(raw_ostream& O, const Module* M) const {
  O << "# functions; # cloned functions; # clones; # calls; # promissor calls; # replaced calls\n";
  O << FunctionsCount << ";" << FunctionsCloned << ";" << ClonesCount << ";" << CallsCount << ";" << PromissorCalls << ";" << CallsReplaced << "\n";
}

// Register the pass to the LLVM framework
char CloneConstantArgs::ID = 0;
static RegisterPass<CloneConstantArgs> X("clone-constant-args", "Clone functions with constant args.", false, false);
