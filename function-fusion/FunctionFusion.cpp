#include "FunctionFusion.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"

using namespace llvm;

FunctionFusion::FunctionFusion() : ModulePass(ID) {
  FunctionsCount    = 0;
  CallsCount        = 0;
  FunctionsCloned   = 0;
  CallsReplaced     = 0;
}

bool FunctionFusion::isExternalFunctionCall(CallInst* CS) {
  Function *F = CS->getCalledFunction();
  if (!F || F->isDeclaration()) return true;
  return false;
}

bool FunctionFusion::areNeighborInsts(Instruction* first, Instruction* second) {
   BasicBlock* BB = first->getParent();
   for (BasicBlock::iterator I = BB->begin(); I != BB->end();) {
     Instruction *inst = I;
     if (inst == first) {
      Instruction *nextInst = ++I;
      if (nextInst == second) return true;
      else return false;
     } else {
       ++I;
     }
   }
   return false;
}

bool FunctionFusion::hasPointerParam(Function* F) {
  FunctionType* FT = F->getFunctionType();
  for (unsigned i = 0; i < FT->getNumParams(); ++i) {
    if (FT->getParamType(i)->isPointerTy()) return true;
  }
  return false;
}

void FunctionFusion::visitCallSite(CallSite CS) {
  // Pega definição do tipo %v = call i32 @foo()
  Instruction *Call = CS.getInstruction();
  if (isa<CallInst>(Call) && Call->hasNUses(1)) {

    // Pega único uso da definição, do tipo
    // %u = call i32 @bar(..., %v, ...)
    User *u = *(Call->use_begin());
    if (isa<CallInst>(u)) {

      CallSite iCS(cast<Instruction>(u));

      CallInst *CI = dyn_cast<CallInst>(Call);
      CallInst *iCI = dyn_cast<CallInst>(iCS.getInstruction());
      if(isExternalFunctionCall(CI) || isExternalFunctionCall(iCI)
          || toBeModified.count(CI) || toBeModified.count(iCI)
          //|| hasPointerParam(CS.getCalledFunction())
          || CS.getCalledFunction()->isVarArg()
          || iCS.getCalledFunction()->isVarArg()
          || !areNeighborInsts(CI, iCI)
          //|| CS.getCalledFunction()->getReturnType()->isPointerTy()
          //|| hasPointerParam(iCS.getCalledFunction())
          //|| iCS.getCalledFunction()->getReturnType()->isPointerTy()
         ) {
        return;
      } else {
        toBeModified.insert(CI);
        toBeModified.insert(iCI);
        selectToClone(iCS, CS);
      }
    }
  }
}

void FunctionFusion::selectToClone(CallSite& use, CallSite& definition) {

  CallInst *definitionCall = dyn_cast<CallInst>(definition.getInstruction());

  unsigned n = 0;
  // Itera nos argumentos do uso para encontrar o
  // índice onde a definição foi usada
  for (CallSite::arg_iterator actualArgIter = use.arg_begin();
      actualArgIter != use.arg_end(); ++actualArgIter, ++n) {
    Value *actualArg = *actualArgIter;

    if(actualArg == definitionCall) {
      //Achou a definição na lista de argumentos
      Function *F = use.getCalledFunction();
      Function *G = definition.getCalledFunction();
      CallInst *useCall = dyn_cast<CallInst>(use.getInstruction());
      functions2fuse[std::make_pair(std::make_pair(F, G), n)].push_back(std::make_pair(useCall, definitionCall));
      functions2fuseHistogram[std::make_pair(std::make_pair(F, G), n)]++;

      DEBUG(errs() << "Tripla (fn, fn, n) = " << F->getName() << ", " << G->getName() << ", " << n << "\n");
    }
  }
}


bool FunctionFusion::runOnModule(Module &M) {
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      FunctionsCount++;
       if (F->use_empty()) continue;
       for (Value::use_iterator UI = F->use_begin(), E = F->use_end(); UI != E; ++UI) {
         User *U = *UI;

         if (isa<BlockAddress>(U)) continue;
         if (!isa<CallInst>(U) && !isa<InvokeInst>(U)) continue;

         CallSite CS(cast<Instruction>(U));
         if (!CS.isCallee(UI)) continue;
         CallsCount++;
       }
    }
  }

  bool modifiedModule = false;
  bool modified       = false;
  do {
    toBeModified.clear();
    functions2fuse.clear();
    visit(M);
    modified  = cloneFunctions();
    modifiedModule = modifiedModule | modified;

    DEBUG(errs() << "one round! " << modified << "\n");
  } while (modified);

  return modifiedModule;
}

// clone functions and replace its callers
bool FunctionFusion::cloneFunctions() {
  bool modified = false;
  for (std::map < std::pair < std::pair < Function*, Function* >, unsigned >, std::vector< std::pair<CallInst*, CallInst*> > >::iterator it = functions2fuse.begin();
      it != functions2fuse.end(); ++it) {
    std::pair < std::pair < Function*, Function* >, unsigned > triple = it->first;

    modified = true;

    //Fuse two functions
    Function *clone;
    if (!clonedFunctions.count(triple)) {
      Function* use        = triple.first.first;
      Function* definition = triple.first.second;
      unsigned argPosition = triple.second; 

      clone = fuseFunctions(use, definition, argPosition);
    } else {
      clone = clonedFunctions[triple];
    }

    //Replace uses
    std::vector< std::pair<CallInst* ,CallInst*> > callInsts = it->second;
    for(std::vector< std::pair<CallInst* ,CallInst*> >::iterator CSit = callInsts.begin();
        CSit != callInsts.end(); ++CSit) {
      std::pair<CallInst* ,CallInst*> cspair = *CSit;
      CallInst* use        = cspair.first;
      CallInst* definition = cspair.second;
      unsigned argPosition = triple.second; 
      ReplaceCallInstsWithFusion(clone, use, definition, argPosition);
    }

  }
    
  return modified;
}

Function* FunctionFusion::fuseFunctions(Function* use, Function* definition, unsigned argPosition) {

  // Copy the parameters from the functions to fuse
  FunctionType* useFT = use->getFunctionType();
  FunctionType* defFT = definition->getFunctionType();
  std::vector<Type*> params;
  for (unsigned i = 0; i < defFT->getNumParams(); ++i) {
    params.push_back(defFT->getParamType(i));
  }
  for (unsigned i = 0; i < useFT->getNumParams(); ++i) {
    if (i!=argPosition) {
      params.push_back(useFT->getParamType(i));
    }
  }

  // Create the new fused function
  FunctionType *newFT = FunctionType::get(use->getReturnType(), params, use->isVarArg());
  Function *NF = Function::Create(newFT, use->getLinkage());
  NF->setCallingConv(use->getCallingConv());
  // NF->copyAttributesFrom(use);

  // After the parameters have been copied, we should copy the parameter
  // names, to ease function inspection afterwards.
  Function::arg_iterator NFArg = NF->arg_begin();
  for (Function::arg_iterator Arg = definition->arg_begin(), ArgEnd = definition->arg_end();
      Arg != ArgEnd; ++Arg, ++NFArg) {
    NFArg->setName(definition->getName() + Arg->getName());
  }
  Function::arg_iterator Arg = use->arg_begin();
  for (unsigned i = 0; i < useFT->getNumParams(); ++i) {
    if (i != argPosition) {
      NFArg->setName(use->getName() + Arg->getName());
      ++NFArg;
    }
    ++Arg;
  }

  // Set function fused name
  std::ostringstream convert;
  convert << argPosition;

  std::string fnName = definition->getName();
  Regex fusedEnding("\\.fused_[0-9]+$");
  std::string number;
  if(fusedEnding.match(fnName)) {
    SmallVector<StringRef, 1> matches;
    Regex fusedNumber("[0-9]+$");
    if(fusedNumber.match(fnName, &matches)) {
      number = matches.begin()->str();
    } else {
      number = "";
    }
    fnName = fusedEnding.sub("", fnName);
  } else {
    number = "";
  }

  std::string newName = fnName + ".fused_" + use->getName().str() + ".fused_" + number + convert.str();
  NF->setName(newName);
  DEBUG(errs() << "creating function " << NF->getName() << "\n");

  // Insert the fusion function before the original
  use->getParent()->getFunctionList().insert(use, NF);

  // Create the calls inside the new function
  BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", NF);
  std::vector<Value*> defParams;
  Function::arg_iterator NFArgIter = NF->arg_begin();
  for (unsigned i = 0; i < defFT->getNumParams(); ++i, ++NFArgIter) {
    Value *arg = NFArgIter;
    defParams.push_back(arg);
  }

  CallInst* newDefCI = CallInst::Create(definition, defParams, "", BB);
  std::vector<Value*> useParams;
  for (unsigned i = 0; i < useFT->getNumParams(); ++i) {
    if (i == argPosition) {
      useParams.push_back(newDefCI);
    } else {
      Value *arg = NFArgIter;
      useParams.push_back(arg);
      ++NFArgIter;
    }
  }

  CallInst* newUseCI = CallInst::Create(use, useParams, "", BB);

  // Create return inst
  if (use->getReturnType()->isVoidTy()) {
    ReturnInst::Create(NF->getContext(), 0, BB);
  } else {
    ReturnInst::Create(NF->getContext(), newUseCI, BB);
  }

  // Inline new call insts
  InlineFunctionInfo unused;
  InlineFunction(newDefCI, unused);
  InlineFunction(newUseCI, unused);

  FunctionsCloned++;
  return NF;
}

void FunctionFusion::ReplaceCallInstsWithFusion(Function* fn, CallInst* use, CallInst* definition, unsigned argPosition) {

  // Collect params
  std::vector<Value*> params;

  for (unsigned i = 0; i < definition->getNumArgOperands(); ++i) {
    params.push_back(definition->getArgOperand(i));
  }

  for (unsigned i = 0; i < use->getNumArgOperands(); ++i) {
    if (i != argPosition) params.push_back(use->getArgOperand(i));
  }

  // Insert new call inst
  CallInst* newCI = CallInst::Create(fn, params, "", use);
  newCI->setCallingConv(use->getCallingConv());
  // newCI->setAttributes(use->getAttributes());

  // Replace uses of 'use' values
  use->replaceAllUsesWith(newCI);

  // Remove previous callInsts
  use->eraseFromParent();
  definition->eraseFromParent();
  CallsReplaced += 2;
}

void FunctionFusion::print(raw_ostream& O, const Module* M) const {
  O << "# functions; # cloned functions; # calls; # replaced calls\n";
  O << FunctionsCount << ";" << FunctionsCloned << ";" << CallsCount << ";" << CallsReplaced << "\n";
}

// Register the pass to the LLVM framework
char FunctionFusion::ID = 0;
INITIALIZE_PASS(FunctionFusion, "function-fusion", "Clone functions with constant args.", false, false)

ModulePass *llvm::createFunctionFusionPass() {
  return new FunctionFusion;
}
