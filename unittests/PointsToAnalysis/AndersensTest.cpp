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
  void initializeAPassPass(PassRegistry&);

  namespace {
    struct APass : public FunctionPass {
      static char ID;
      virtual bool runOnFunction(Function &F) {
        AliasAnalysis *AA = &getAnalysis<AliasAnalysis>();
        Function::iterator FI = F.begin();
        BasicBlock *BB0 = FI;

        CallInst *f, *g;
        for (BasicBlock::iterator BBI = BB0->begin();
             BBI != BB0->end(); ++BBI) {
          if (isa<CallInst>(BBI)) {
            CallInst *t = dyn_cast<CallInst>(BBI);
            if (t->getCalledFunction()->getName() == "g")
              g = t;
            else if (t->getCalledFunction()->getName() == "f")
              f = t;
          }
        }

        Value *l0 = g->getArgOperand(0);
        Value *l1 = g->getArgOperand(1);
        Value *l2 = f->getArgOperand(0);
        Value *l3 = f->getArgOperand(1);

        EXPECT_EQ(AliasAnalysis::MayAlias, AA->alias(l0, l1));
        EXPECT_EQ(AliasAnalysis::MayAlias, AA->alias(l1, l0));

        EXPECT_EQ(AliasAnalysis::NoAlias, AA->alias(l2, l3));

        return false;
      }
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<AliasAnalysis>();
      }
      APass() : FunctionPass(ID) {
        initializeAPassPass(*PassRegistry::getPassRegistry());
      }
    };

    char APass::ID = 0;

    Module* makeLLVMModule() {
      const char *ModuleStrig =
        "%struct.test_struct = type { i32, %struct.test_struct* }\n"
        "\n"
        "define i32 @main() nounwind {\n"
        "  %1 = alloca i32, align 4\n"
        "  %t0 = alloca %struct.test_struct*, align 4\n"
        "  %t1 = alloca %struct.test_struct*, align 4\n"
        "  %t2 = alloca %struct.test_struct*, align 4\n"
        "  %t3 = alloca %struct.test_struct*, align 4\n"
        "  store i32 0, i32* %1\n"
        "  %2 = call noalias i8* @malloc(i32 8) nounwind\n"
        "  %3 = bitcast i8* %2 to %struct.test_struct*\n"
        "  store %struct.test_struct* %3, %struct.test_struct** %t0, align 4\n"
        "  %4 = call noalias i8* @malloc(i32 8) nounwind\n"
        "  %5 = bitcast i8* %4 to %struct.test_struct*\n"
        "  store %struct.test_struct* %5, %struct.test_struct** %t1, align 4\n"
        "  %6 = call noalias i8* @malloc(i32 8) nounwind\n"
        "  %7 = bitcast i8* %6 to %struct.test_struct*\n"
        "  store %struct.test_struct* %7, %struct.test_struct** %t2, align 4\n"
        "  %8 = load %struct.test_struct** %t2, align 4\n"
        "  store %struct.test_struct* %8, %struct.test_struct** %t3, align 4\n"
        "  %9 = load %struct.test_struct** %t1, align 4\n"
        "  %10 = load %struct.test_struct** %t0, align 4\n"
        "  %11 = getelementptr inbounds %struct.test_struct* %10, i32 0, i32 1\n"
        "  store %struct.test_struct* %9, %struct.test_struct** %11, align 4\n"
        "  %12 = load %struct.test_struct** %t0, align 4\n"
        "  %13 = load %struct.test_struct** %t1, align 4\n"
        "  %14 = getelementptr inbounds %struct.test_struct* %13, i32 0, i32 1\n"
        "  store %struct.test_struct* %12, %struct.test_struct** %14, align 4\n"
        "  %15 = load %struct.test_struct** %t0, align 4\n"
        "  %16 = load %struct.test_struct** %t3, align 4\n"
        "  %17 = call i32 @f(%struct.test_struct* %15, %struct.test_struct* %16)\n"
        "  %18 = load %struct.test_struct** %t0, align 4\n"
        "  %19 = load %struct.test_struct** %t1, align 4\n"
        "  %20 = call i32 @g(%struct.test_struct* %18, %struct.test_struct* %19)\n"
        "  ret i32 0\n"
        "}\n"
        "\n"
        "declare noalias i8* @malloc(i32) nounwind\n"
        "\n"
        "declare i32 @f(%struct.test_struct*, %struct.test_struct*)\n"
        "\n"
        "declare i32 @g(%struct.test_struct*, %struct.test_struct*)\n";
      LLVMContext &C = getGlobalContext();
      SMDiagnostic Err;
      PassRegistry &Registry = *PassRegistry::getPassRegistry();
      initializeCore(Registry);
      initializeAnalysis(Registry);
      initializePTA(Registry);
      return ParseAssemblyString(ModuleStrig, NULL, Err, C);
    }

    TEST(Andersens, Working) {
      OwningPtr<Module> M(makeLLVMModule());
      PassManager Passes;
      DebugFlag = true;
      EnableDebugBuffering = true;
      setCurrentDebugType("");
      Pass *Mem2reg = createPromoteMemoryToRegisterPass();
      Passes.add(Mem2reg);
      Pass *Andersens = createAndersensPass();
      Passes.add(Andersens);
      APass *P = new APass();
      Passes.add(P);
      Passes.run(*M);
    }
  }
}

INITIALIZE_PASS_BEGIN(APass, "apass", "apass", false, false)
INITIALIZE_PASS_DEPENDENCY(Andersens)
INITIALIZE_PASS_END(APass, "apass", "apass", false, false)
