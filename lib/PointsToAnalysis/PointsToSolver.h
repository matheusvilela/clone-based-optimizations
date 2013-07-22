//===- PointsToSolver.h - Points-to Solver Interface  ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic PointsToSolver interface, which is used as the
// common interface used by LCD, HCD and hybrid algorithms to solve points-to
// constraints graphs.
//
// Implementations of this interface must implement the various virtual methods,
// which automatically provides functionality for the entire suite of client
// APIs.
//
//===----------------------------------------------------------------------===//

#ifndef POINTSTOSOLVER_H
#define POINTSTOSOLVER_H

#include "llvm/IR/Value.h"

namespace llvm {

/// Constraint - The constraints are 'copy', for statements like
/// "Target = Source", 'load' for statements like "Target = *Source",
/// 'store' for statements like "*Target = Source", and AddressOf
/// for statements like "Target = &Source" or "Target = alloca".
class Constraint {
 public:
  enum ConstraintType {
    Copy, Load, Store, AddressOf
  };
  explicit Constraint(ConstraintType Ty, Value *Source, Value *Target)
    : Type(Ty), Source(Source), Target(Target) {}
  ConstraintType getType() const { return Type; }
  Value *getSource() const { return Source; };
  Value *getTarget() const { return Target; }

 private:
  ConstraintType Type;
  Value *Source;
  Value *Target;
};

/// \brief This class template is an interface for Points-to solvers.
class PointsToSolver {
 public:
  /// Convenience function
  inline void addConstraint(Constraint::ConstraintType Ty,
                            Value *Source, Value *Target) {
    addConstraint(Constraint(Ty, Source, Target));
  }

  /// Add a constraint to the graph
  virtual void addConstraint(const Constraint &c)=0;

  /// Perform the iterative solver over the provided data
  virtual void solve()=0;

  /// Check whether or not two values alias
  virtual bool alias(Value *A, Value *B)=0;
};

} // end of namespace llvm.

#endif // POINTSTOSOLVER_H
