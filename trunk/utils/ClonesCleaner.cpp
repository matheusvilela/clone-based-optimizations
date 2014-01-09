#include <sstream>
#include <unistd.h>
#include <ios>
#include <fstream>
#include <string>
#include <iostream>

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/Statistic.h"

#undef DEBUG_TYPE
#define DEBUG_TYPE "clones-cleaner"

using namespace llvm;

STATISTIC(OrphansDropped, "Number of ophan functions removed");
class ClonesCleaner : public ModulePass {

  std::map<std::string, std::vector<Function*> > functions;

  public:

  static char ID;

  ClonesCleaner() : ModulePass(ID) {
     OrphansDropped = 0;
  }

  // +++++ METHODS +++++ //

  bool runOnModule(Module &M);
  void collectFunctions(Module &M);
  bool removeFunctionFusionGarbage(Module &M);
  bool removeOrphanFunctions();
};

bool ClonesCleaner::runOnModule(Module &M) {

  bool modified = false;
  // Collect information
  collectFunctions(M);
  modified = modified | removeOrphanFunctions();
  modified = modified | removeFunctionFusionGarbage(M);

  return false;
}

void ClonesCleaner::collectFunctions(Module &M) {
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (!F->isDeclaration()) {
      std::string fnName = F->getName();

      Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+)|(\\.noret))+");
      Regex fusedEnding("\\.fused_[0-9]+$");
      bool isFused  = fusedEnding.match(fnName);
      bool isCloned = ending.match(fnName);

      std::string originalName = fnName;
      if (isCloned) {
        Regex noaliasend("\\.noalias");
        Regex constargsend("\\.constargs[0-9]+");
        Regex noretend("\\.noret");

        if (noaliasend.match(fnName)) {
          originalName = noaliasend.sub("", originalName);
        }
        if (constargsend.match(fnName)) {
          originalName = constargsend.sub("", originalName);
        }
        if (noretend.match(fnName)) {
          originalName = noretend.sub("", originalName);
        }
        functions[originalName].push_back(F);
      } else if (isFused) {

        SmallVector<StringRef, 10> functionsNames;
        StringRef(fnName).split(functionsNames, StringRef(".fused_"));
        for( SmallVector<StringRef, 10>::iterator it = functionsNames.begin();
            it != functionsNames.end(); ++it) {
          originalName = it->str();
          functions[originalName].push_back(F);
        }

      } else {
        functions[originalName].push_back(F);
      }
    }
  }
}

bool ClonesCleaner::removeOrphanFunctions() {
  Regex ending(".*((\\.noalias)|(\\.constargs[0-9]+)|(\\.noret))+");
  Regex fusedEnding("\\.fused_[0-9]+$");
  bool modified = false;

  std::vector<Function*> toBeRemoved;
  for(std::map<std::string, std::vector<Function*> >::iterator it = functions.begin();
      it != functions.end(); ++it) {
    std::vector<Function*> vec = it->second;
    bool hasClones = (vec.size() > 1);
    if (!hasClones) continue;
    for(std::vector<Function*>::iterator it2 = vec.begin(); it2 != vec.end(); ++it2) {
      Function* F = *it2;
      std::string fnName = F->getName();
      bool isFused  = fusedEnding.match(fnName);
      bool isCloned = ending.match(fnName);
      if (!isFused && !isCloned && F->use_empty()) {
        toBeRemoved.push_back(F);
      }
    }
  }
  for (std::vector<Function*>::iterator it = toBeRemoved.begin(); it != toBeRemoved.end(); ++it) {
    Function *F = *it;
    F->eraseFromParent();
    OrphansDropped++;
    modified = true;
  }
  return modified;
}

bool ClonesCleaner::removeFunctionFusionGarbage(Module &M) {
  bool modified = false;
  std::vector<Function*> toBeRemoved;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (F && !F->isDeclaration() && F->getLinkage() == GlobalValue::InternalLinkage && F->use_empty()) {
      toBeRemoved.push_back(F);
    }
  }
  for (std::vector<Function*>::iterator it = toBeRemoved.begin(); it != toBeRemoved.end(); ++it) {
    Function *F = *it;
    F->eraseFromParent();
    modified = true;
  }
  return modified;
}



// Register the pass to the LLVM framework
char ClonesCleaner::ID = 0;

static RegisterPass<ClonesCleaner> X("clones-cleaner",
                "Clean useless code.", false, true);
