#include "FunctionFusion.h"
#include "llvm/InitializePasses.h"
#include "llvm/CBO/CBO.h"

using namespace llvm;

FunctionFusion::FunctionFusion() : ModulePass(ID) {
  FunctionsCloned   = 0;
  CallsReplaced     = 0;
  FunctionsCount    = 0;
  CallsCount        = 0;
  ClonesCount       = 0;
  FunctionsFused     = 0;
}

bool FunctionFusion::isExternalFunctionCall(CallSite& CS) {
  Function *F = CS.getCalledFunction();
  if (!F || F->isDeclaration()) return true;
  return false;
}

void FunctionFusion::visitCallSite(CallSite CS) {
  // Pega definição do tipo %v = call i32 @foo()
  Instruction *Call = CS.getInstruction();
  if (Call->hasNUses(1)) {

    // Pega único uso da definição, do tipo
    // %u = call i32 @bar(..., %v, ...)
    User *u = *(Call->use_begin());
    if (isa<CallInst>(u) || isa<InvokeInst>(u)) {

      CallSite iCS(cast<Instruction>(u));
      if(isExternalFunctionCall(CS) || isExternalFunctionCall(iCS)
          || toBeModified.count(&CS) || toBeModified.count(&iCS)) {
        return;
      } else {
        toBeModified.insert(&CS);
        toBeModified.insert(&iCS);
        selectToClone(iCS, CS);
      }
    }
  }
}

void FunctionFusion::selectToClone(CallSite& use, CallSite& definition) {

  Instruction *definitionCall = definition.getInstruction();

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
      errs() << "Inserting " << *use.getInstruction() << "\n";
      errs() << "Inserting " << *definition.getInstruction() << "\n";
      functions2fuse[std::make_pair(std::make_pair(F, G), n)].push_back(std::make_pair(use.getInstruction(), definition.getInstruction()));
      functions2fuseHistogram[std::make_pair(std::make_pair(F, G), n)]++;

      FunctionsFused++;
      errs() << "Tripla (fn, fn, n) = " << F->getName() << ", " << G->getName() << ", " << n << "\n";
    }
  }
}


bool FunctionFusion::runOnModule(Module &M) {

  visit(M);
  bool modified = cloneFunctions();

  return modified;
}

// clone functions and replace its callers
bool FunctionFusion::cloneFunctions() {
  bool modified = false;
  for (std::map < std::pair < std::pair < Function*, Function* >, unsigned >, std::vector< std::pair<Instruction*, Instruction*> > >::iterator it = functions2fuse.begin();
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
    std::vector< std::pair<Instruction* ,Instruction*> > callSites = it->second;
    for(std::vector< std::pair<Instruction* ,Instruction*> >::iterator CSit = callSites.begin();
        CSit != callSites.end(); ++CSit) {
      std::pair<Instruction* ,Instruction*> cspair = *CSit;
      Instruction* use        = cspair.first;
      Instruction* definition = cspair.second;
      unsigned argPosition = triple.second; 
      ReplaceCallSitesWithFusion(clone, use, definition, argPosition);
    }

  }
    
  return modified;
}

Function* FunctionFusion::getAlwaysInlineFunction(Function *F) {
  Function *NF;
  if (!alwaysInlineFns.count(F)) {
    NF = cloneFunctionWithAlwaysInlineAttr(F);
    alwaysInlineFns[F] = NF;
  } else {
    NF = alwaysInlineFns[F];
  }
  return NF;
}

Function* FunctionFusion::cloneFunctionWithAlwaysInlineAttr(Function* Fn) {
  Function *NF = Function::Create(Fn->getFunctionType(), GlobalValue::InternalLinkage);
  NF->copyAttributesFrom(Fn);

  // Add alwaysinline attribute
  NF->addFnAttr(Attribute::AlwaysInline);

  // Copy parameter names
  Function::arg_iterator NFArg = NF->arg_begin();
  for (Function::arg_iterator Arg = Fn->arg_begin(), ArgEnd = Fn->arg_end(); Arg != ArgEnd; ++Arg, ++NFArg) {
    NFArg->setName(Arg->getName());
  }

  // To avoid name collision, we should select another name.
  NF->setName(Fn->getName() + ".alwaysinline");

  // Insert the clone function before the original
  Fn->getParent()->getFunctionList().insert(Fn, NF);

  // Fill clone content
  ValueToValueMapTy VMap;
  SmallVector<ReturnInst*, 8> Returns;

  Function::arg_iterator NI = NF->arg_begin();
  for (Function::arg_iterator I = Fn->arg_begin();
      NI != NF->arg_end(); ++I, ++NI) {
    VMap[I] = NI;
  }

  CloneAndPruneFunctionInto(NF, Fn, VMap, false, Returns);

  return NF;
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
  Function *NF = Function::Create(newFT, GlobalValue::InternalLinkage);

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
  NF->setName(use->getName() + definition->getName() + convert.str() + ".fused");

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

  CallInst* newDefCI = CallInst::Create(getAlwaysInlineFunction(definition), defParams, "", BB);
  errs() << "inserting use callinst " << *newDefCI << "\n";
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
  CallInst* newUseCI = CallInst::Create(getAlwaysInlineFunction(use), useParams, "", BB);
  errs() << "inserting use callinst " << *newUseCI << "\n";

  ReturnInst::Create(NF->getContext(), newUseCI, BB);

  return NF;
}

void FunctionFusion::ReplaceCallSitesWithFusion(Function* fn, Instruction* use, Instruction* definition, unsigned argPosition) {

  // Collect params
  std::vector<Value*> params;

  if (InvokeInst *II = dyn_cast<InvokeInst>(definition)) {
    errs() << "will get params from " << *II << "\n";
    for (unsigned i = 0; i < II->getNumArgOperands(); ++i) {
      params.push_back(II->getArgOperand(i));
    }
  } else {
    CallInst *CI = dyn_cast<CallInst>(definition);
    errs() << "will get params from " << *CI << "\n";
    for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
      params.push_back(CI->getArgOperand(i));
    }
  }
  errs() << "got def params" << params.size() << "\n";

  if (InvokeInst *II = dyn_cast<InvokeInst>(use)) {
    errs() << "will get params from " << *II << "\n";
    for (unsigned i = 0; i < II->getNumArgOperands(); ++i) {
      if (i != argPosition) params.push_back(II->getArgOperand(i));
    }
  } else {
    CallInst *CI = dyn_cast<CallInst>(use);
    errs() << "will get params from " << *CI << "\n";
    for (unsigned i = 0; i < CI->getNumArgOperands(); ++i) {
      if (i != argPosition) params.push_back(CI->getArgOperand(i));
    }
  }
  errs() << "got params " << params.size();

  // Insert new call inst
  CallInst* newCI = CallInst::Create(fn, params, "", use);

  // Replace uses of 'use' values
  use->replaceAllUsesWith(newCI);

  // Remove previous callSites
  use->eraseFromParent();
  definition->eraseFromParent();
}

void FunctionFusion::print(raw_ostream& O, const Module* M) const {
  O << "# functions; # cloned functions; # clones; # calls; # promissor calls; # replaced calls\n";
  O << FunctionsCount << ";" << FunctionsCloned << ";" << ClonesCount << ";" << CallsCount << ";" << PromissorCalls << ";" << CallsReplaced << "\n";
  O << "Functions Fused: "<< FunctionsFused << "\n";
}

// Register the pass to the LLVM framework
char FunctionFusion::ID = 0;
INITIALIZE_PASS(FunctionFusion, "function-fusion", "Clone functions with constant args.", false, false)

ModulePass *llvm::createFunctionFusionPass() {
  return new FunctionFusion;
}
