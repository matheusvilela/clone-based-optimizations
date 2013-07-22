//===- PointsToAnalysisTest.cpp - ScalarEvolution unit tests -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LCD.h"
#include "llvm/IR/Argument.h"
#include "llvm/ADT/OwningPtr.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "gtest/gtest.h"

using namespace llvm;

TEST(TestCases, Program1) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  // Use a simple constant to work as an allocated var
  Constant *Zero = Constant::getNullValue(Int32Ptr);
  Constant *alloc_0 = ConstantInt::get(C, APInt(32, 1492));

  OwningPtr<Value> p0(new Argument(Int32Ptr));
  OwningPtr<Value> v0(new Argument(Int32Ptr));
  OwningPtr<Value> v1(new Argument(Int32Ptr));
  OwningPtr<Value> v2(new Argument(Int32Ptr));

  g->addConstraint(Constraint::AddressOf, p0.get(), alloc_0);  // p0 = alloc_0;
  g->addConstraint(Constraint::Copy, v0.get(), Zero);          // v0 = 0;
  g->addConstraint(Constraint::Copy, v1.get(), p0.get());      // v1 = (int)p0;
  g->addConstraint(Constraint::Copy, v2.get(), v1.get());      // v2 = v1 + v0;
  g->addConstraint(Constraint::Copy, v2.get(), v0.get());

  g->solve();

  EXPECT_TRUE(g->alias(v2.get(), p0.get()));
  EXPECT_TRUE(g->alias(v1.get(), p0.get()));
  EXPECT_TRUE(g->alias(v2.get(), v0.get()));

  EXPECT_FALSE(g->alias(p0.get(), v2.get()));
  EXPECT_FALSE(g->alias(v0.get(), v1.get()));
  EXPECT_FALSE(g->alias(v1.get(), v0.get()));
  EXPECT_FALSE(g->alias(v0.get(), alloc_0));
}

TEST(TestCases, Program2) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  Constant *Zero = Constant::getNullValue(Int32Ptr);
  OwningPtr<Value> v0(new Argument(Int32Ptr));
  OwningPtr<Value> v1(new Argument(Int32Ptr));
  OwningPtr<Value> v2(new Argument(Int32Ptr));

  g->addConstraint(Constraint::Copy, v0.get(), Zero);           // v0 = 0;
  g->addConstraint(Constraint::AddressOf, v1.get(), v0.get());  // v1 = &v0;
  g->addConstraint(Constraint::Copy, v2.get(), v0.get());       // v2 = v0 + v1;
  g->addConstraint(Constraint::Copy, v2.get(), v1.get());

  EXPECT_TRUE(g->alias(v2.get(), v0.get()));
  EXPECT_TRUE(g->alias(v2.get(), v1.get()));
  
  EXPECT_TRUE(g->alias(v2.get(), v2.get())); // self alias
  
  EXPECT_FALSE(g->alias(v1.get(), v0.get()));
  EXPECT_FALSE(g->alias(v1.get(), v2.get()));
  EXPECT_FALSE(g->alias(v0.get(), v2.get()));
  EXPECT_FALSE(g->alias(v0.get(), v1.get()));
}

TEST(TestCases, Program3) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  Constant *Zero = Constant::getNullValue(Int32Ptr);
  OwningPtr<Value> a(new Argument(Int32Ptr));
  OwningPtr<Value> b(new Argument(Int32Ptr));
  OwningPtr<Value> c(new Argument(Int32Ptr));
  OwningPtr<Value> d(new Argument(Int32Ptr));

  g->addConstraint(Constraint::AddressOf, b.get(), a.get());  // b = &a
  g->addConstraint(Constraint::AddressOf, a.get(), c.get());  // a = &c
  g->addConstraint(Constraint::Copy, d.get(), a.get());       // d = a
  g->addConstraint(Constraint::Store, d.get(), b.get());      // *d = b
  g->addConstraint(Constraint::Load, a.get(), d.get());       // a = *d

  g->solve();

  EXPECT_TRUE(g->alias(d.get(), a.get()));
  EXPECT_TRUE(g->alias(c.get(), b.get()));
  EXPECT_TRUE(g->alias(a.get(), c.get()));
  EXPECT_TRUE(g->alias(a.get(), b.get()));
  
  EXPECT_FALSE(g->alias(b.get(), a.get()));
  EXPECT_FALSE(g->alias(b.get(), c.get()));
  EXPECT_FALSE(g->alias(a.get(), d.get()));
  EXPECT_FALSE(g->alias(c.get(), d.get()));
}

TEST(TestCases, Program4) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  Constant *Zero = Constant::getNullValue(Int32Ptr);
  OwningPtr<Value> a(new Argument(Int32Ptr));
  OwningPtr<Value> b(new Argument(Int32Ptr));
  OwningPtr<Value> c(new Argument(Int32Ptr));
  OwningPtr<Value> d(new Argument(Int32Ptr));

  g->addConstraint(Constraint::Copy, a.get(), b.get());  // a = b
  g->addConstraint(Constraint::Copy, c.get(), a.get());  // c = a
  g->addConstraint(Constraint::Copy, b.get(), c.get());  // b = c
  g->addConstraint(Constraint::Copy, c.get(), d.get());  // c = d

  g->solve();

  EXPECT_TRUE(g->alias(a.get(), d.get()));
  EXPECT_TRUE(g->alias(b.get(), d.get()));
  EXPECT_TRUE(g->alias(c.get(), d.get()));
  
  EXPECT_FALSE(g->alias(d.get(), a.get()));
  EXPECT_FALSE(g->alias(d.get(), b.get()));
  EXPECT_FALSE(g->alias(d.get(), c.get()));
}

TEST(TestCases, Program5) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  Constant *Zero = Constant::getNullValue(Int32Ptr);
  OwningPtr<Value> a(new Argument(Int32Ptr));
  OwningPtr<Value> b(new Argument(Int32Ptr));
  OwningPtr<Value> c(new Argument(Int32Ptr));
  OwningPtr<Value> d(new Argument(Int32Ptr));

  g->addConstraint(Constraint::AddressOf, a.get(), b.get());  // a = &b
  g->addConstraint(Constraint::AddressOf, c.get(), d.get());  // c = &d
  g->addConstraint(Constraint::Store, a.get(), d.get());      // *a = d
  g->addConstraint(Constraint::Store, c.get(), b.get());      // *c = b

  g->solve();

  EXPECT_TRUE(g->alias(b.get(), d.get()));
  EXPECT_TRUE(g->alias(d.get(), b.get()));
  
  EXPECT_FALSE(g->alias(d.get(), a.get()));
  EXPECT_FALSE(g->alias(d.get(), c.get()));
  EXPECT_FALSE(g->alias(b.get(), a.get()));
  EXPECT_FALSE(g->alias(b.get(), c.get()));
}

TEST(TestCases, Program6) {
  LLVMContext &C(getGlobalContext());
  OwningPtr<PointsToSolver> g(new LCD);

  Type *Int32Ptr = Type::getInt32PtrTy(C);

  Constant *Zero = Constant::getNullValue(Int32Ptr);
  OwningPtr<Value> a(new Argument(Int32Ptr));
  OwningPtr<Value> b(new Argument(Int32Ptr));
  OwningPtr<Value> c(new Argument(Int32Ptr));
  OwningPtr<Value> d(new Argument(Int32Ptr));

  g->addConstraint(Constraint::AddressOf, a.get(), b.get());  // a = &b
  g->addConstraint(Constraint::AddressOf, c.get(), d.get());  // c = &d
  g->addConstraint(Constraint::Store, a.get(), d.get());      // *a = d
  g->addConstraint(Constraint::Store, c.get(), b.get());      // *c = b
  g->addConstraint(Constraint::Copy, a.get(), d.get());       // a = (void*)d
  g->addConstraint(Constraint::Copy, c.get(), b.get());       // c = (void*)b

  g->solve();

  EXPECT_TRUE(g->alias(b.get(), d.get()));
  EXPECT_TRUE(g->alias(d.get(), b.get()));
  EXPECT_TRUE(g->alias(c.get(), b.get()));
  EXPECT_TRUE(g->alias(c.get(), d.get()));
  EXPECT_TRUE(g->alias(a.get(), d.get()));
  EXPECT_TRUE(g->alias(a.get(), b.get()));

  EXPECT_FALSE(g->alias(a.get(), c.get()));
  EXPECT_FALSE(g->alias(c.get(), a.get()));
  EXPECT_FALSE(g->alias(b.get(), a.get()));
  EXPECT_FALSE(g->alias(d.get(), c.get()));
}

/* Modeline for vim: set tw=79 et ts=4: */

