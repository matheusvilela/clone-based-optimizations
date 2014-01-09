//=========- llvm/Transforms/PUR.h - Prune Unused Retvals --*- C++ -*-========//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the PruneUnusedRetvals transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_PUR_H
#define LLVM_TRANSFORMS_PUR_H

namespace llvm {

//===----------------------------------------------------------------------===//
/// createCloneUnusedRetvalsPass - This pass substitutes call sites, where the
/// return value is not used, by a clone where the return value is pruned.
ModulePass *createCloneUnusedRetvalsPass();

//===----------------------------------------------------------------------===//
/// createPruneClonesPass - This pass prunes unused retvals clones based on
/// size reduction criteria.
ModulePass *createPruneClonesPass();

//===----------------------------------------------------------------------===//
/// createClonesCountPass - This pass counts the number of cloned functions and
/// classify call sites t provide an idea of the cloning range.
ModulePass *createClonesCountPass();

} // End llvm namespace

#endif
