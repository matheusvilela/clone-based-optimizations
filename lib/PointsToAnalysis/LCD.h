//===- LCD.h - Lazy Cycle Detection Alias Analysis ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an implementation of Andersen's interprocedural alias
// analysis
//
// In pointer analysis terms, this is a subset-based, flow-insensitive,
// field-sensitive, and context-insensitive algorithm pointer algorithm.
//
// This algorithm is implemented as three stages:
//   1. Inclusion constraint identification.
//   2. Offline constraint graph optimization
//   3. Inclusion constraint solving.
//
// The inclusion constraint identification stage is delegated to the external
// class that uses the PointsToSolver interface.
//===----------------------------------------------------------------------===//

#ifndef LCD_H
#define LCD_H

#include "PointsToSolver.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Analysis/PointsTo/Graph.h"

#include <map>
#include <set>
#include <vector>

namespace llvm {

/// This class implements the Lazy Cycle Detection Alias Analysis.
/// The algorithm is also known as Andersen's Interprocedural Alias Analysis.
/// More info: Andersen, L. "Program Analysis and Specialization for the C
/// Programming Language", PhD Thesis, University of Copenhagen, 1994). To
/// remove cycles, it uses Lazy Cycle Detection.
class LCD : public PointsToSolver {
 public:
  typedef PointsTo::Graph Digraph;

  typedef Digraph::Node Vertex;
  typedef Digraph::Edge Edge;
  typedef Digraph::AdjEdgeItr in_edge_iterator;
  typedef Digraph::AdjEdgeItr out_edge_iterator;
  typedef Digraph::NodeItr vertex_iterator;

  typedef std::map<Vertex, std::set<Value *> > AdjLeftVars;
  typedef std::map<Value *, Vertex> AdjRightVars;
  typedef AdjLeftVars::const_iterator left_const_iterator;
  typedef AdjRightVars::const_iterator right_const_iterator;

  typedef std::set<Vertex> VertexSet;
  typedef std::pair<Vertex, Vertex> VertexPair;
  typedef std::set<VertexPair> EdgeSet;
  typedef std::map<Vertex, BitVector> ConvergingEdges;
  typedef std::vector<Vertex> VertexList;
  typedef std::vector<VertexList> VertexCycles;

 private:
  Digraph G;
  AdjLeftVars leftVars;
  AdjRightVars rightVars;

  // Constraints
  ConvergingEdges pts;
  ConvergingEdges stores;
  ConvergingEdges loads;

 public:
  virtual void addConstraint(const Constraint &c);
  virtual void solve();
  bool alias(Value *A, Value *B);

  /// For debugging
  void printDot(std::ostream& out);

 private:
  Vertex ensureVertex(Value *X);
  VertexCycles findCycles(Vertex s);
  void collapse(VertexList &path);
};

} // end of namespace llvm.

#endif // LCD_H
