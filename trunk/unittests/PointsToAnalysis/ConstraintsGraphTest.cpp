//===----------- AndersensTest.cpp - Andersen's AA unit tests -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Analysis/PTA.h"
#include "llvm/Analysis/ConstraintsGraph.h"
#include "llvm/Assembly/Parser.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Scalar.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace llvm {
  void initializeGPassPass(PassRegistry&);

  namespace {
    struct GPass : public ModulePass {
      static char ID;
      ConstraintsGraph G;

      virtual bool runOnModule(Module &M) {
        G.Initialize(M, this);
        G.dump();
        return false;
      }
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.setPreservesAll();
      }
      GPass() : ModulePass(ID) {
        initializeGPassPass(*PassRegistry::getPassRegistry());
      }
    };

    char GPass::ID = 0;

    Module* makeLLVMModule() {
      const char *ModuleStrig =
        "%struct.node = type { %struct.node* }\n"
        "\n"
        "define i32 @main() nounwind {\n"
        "  %t2 = alloca %struct.node, align 4\n"
        "  %1 = call noalias i8* @malloc(i32 4) nounwind\n"
        "  %2 = bitcast i8* %1 to %struct.node*\n"
        "  %3 = call noalias i8* @calloc(i32 1, i32 4) nounwind\n"
        "  %4 = bitcast i8* %3 to %struct.node*\n"
        "  %5 = bitcast %struct.node* %t2 to i8*\n"
        "  call void @llvm.memset.p0i8.i32(i8* %5, i8 0, i32 4, i32 4, i1 false)\n"
        "  %6 = getelementptr inbounds %struct.node* %t2, i32 0, i32 0\n"
        "  store %struct.node* null, %struct.node** %6, align 4\n"
        "  %7 = getelementptr inbounds %struct.node* %2, i32 0, i32 0\n"
        "  store %struct.node* %4, %struct.node** %7, align 4\n"
        "  %8 = getelementptr inbounds %struct.node* %4, i32 0, i32 0\n"
        "  store %struct.node* %2, %struct.node** %8, align 4\n"
        "  %9 = call i32 @f(%struct.node* %2, %struct.node* %4, %struct.node* %t2)\n"
        "  ret i32 0\n"
        "}\n"
        "\n"
        "declare noalias i8* @malloc(i32) nounwind\n"
        "declare noalias i8* @calloc(i32, i32) nounwind\n"
        "declare void @llvm.memset.p0i8.i32(i8* nocapture, i8, i32, i32, i1) nounwind\n"
        "declare i32 @f(%struct.node*, %struct.node*, %struct.node*)\n";
      LLVMContext &C = getGlobalContext();
      SMDiagnostic Err;
      return ParseAssemblyString(ModuleStrig, NULL, Err, C);
    }

    TEST(Andersens, Working) {
      OwningPtr<Module> M(makeLLVMModule());
      PassManager Passes;
      DebugFlag = true;
      EnableDebugBuffering = true;
      setCurrentDebugType("");
      GPass *P = new GPass();
      Passes.add(P);
      Passes.run(*M);
    }
  }
}

INITIALIZE_PASS(GPass, "GPass", "GPass", false, false)
