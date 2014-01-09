//===- LCD.cpp - Lazy Cycle Detection Alias Analysis ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LCD.h"

#include <algorithm>
#include <iterator>
#include <iostream>

namespace llvm {

namespace {

#if 0
struct VertexWriter {
  ConvergingEdges &pts;
  VertexWriter(ConvergingEdges &_pts) : pts(_pts) {}
  void operator()(std::ostream& out, Vertex v) const {
    out << "[label=\"" << v << " {";
    ConvergingEdges::iterator i = pts.find(v);
    if (i != pts.end() && i->second.size() > 0) {
      VertexList l;
      std::copy(i->second.begin(), i->second.end(), std::back_inserter(l));
      std::copy(l.begin(), --l.end(), std::ostream_iterator<Vertex>(out, ","));
      out << l.back();
    }
    out << "}\"]";
  }
};
#endif

// Convenience functions to increase BitVector's size dynamically
bool isset(BitVector &V, unsigned Idx) {
  if (Idx >= V.size())
    return false;
  return V[Idx];
}

void set(BitVector &V, unsigned Idx) {
  if (V.size() <= Idx)
    V.resize(Idx+1);
  V.set(Idx);
}

void unset(BitVector &V, unsigned Idx) {
  if (Idx >= V.size())
    return;
  V.reset(Idx);
}

} // empty namespace

void LCD::addConstraint(const Constraint &c) {
  Vertex a = ensureVertex(c.getSource());
  Vertex b = ensureVertex(c.getTarget());
  if (c.getType() == Constraint::AddressOf) {
    // Keep refs for later
    set(pts[a], b);
  }
  else if (c.getType() == Constraint::Copy) {
    // Initially, the points-to graph has an edge for each
    // constraint "v1 contains v2" in the constraint system
    G.addEdge(b, a);
  }
  else if (c.getType() == Constraint::Store) {
    // Keep stores for later
    set(stores[a], b);
  }
  else if (c.getType() == Constraint::Load) {
    // Keep loads for later
    set(loads[b], a);
  }
}

void LCD::solve() {
  Vertex n;
  EdgeSet R;
  BitVector W;
  W.resize(G.getNumNodes());
  W.set();

  while ((n = W.find_first()) != -1) {
    W.flip(n);
    for (Vertex v = pts[n].find_first(); v != -1; v = pts[n].find_next(v)) {
      for (Vertex a = loads[n].find_first(); a != -1; a = loads[n].find_next(a)) {
        if (G.findEdge(v, a) == G.edgesEnd()) {
          G.addEdge(v, a);
          W.set(v);
        }
      }
      for (Vertex b = stores[n].find_first(); b != -1; b = stores[n].find_next(b)) {
        if (G.findEdge(b, v) == G.edgesEnd()) {
          G.addEdge(b, v);
          W.set(b);
        }
      }
    }
    BitVector l;
    for (out_edge_iterator i = G.outEdgesBegin(n), ie = G.outEdgesEnd(n);
         i != ie; ++i) {
      set(l, G.getEdgeTarget(*i));
    }
    for (Vertex z = l.find_first(); z != -1; z = l.find_next(z)) {
      if (pts[n] == pts[z]) {
        if (R.find(std::make_pair(n, z)) == R.end()) {
          // Detect and Collapse Cycles
          VertexCycles cycles = findCycles(z); // Find all cycles from z
          for (VertexCycles::iterator path = cycles.begin(), path_end = cycles.end();
               path != path_end; ++path) {
            collapse(*path); // Collapse found paths in z
            // Remove collapsed vertexes from W
            VertexList::iterator v = path->begin(), ve = path->end();
            for (++v; v != ve; ++v) {
              unset(W, *v);
              unset(l, *v);
            }
          }
          R.insert(std::make_pair(n,z));
        }
      }
      else {
        pts[z] |= pts[n];
        W.set(z);
      }
    }
  }
}

bool LCD::alias(Value *A, Value *B) {
  using PointsTo::df_iterator;
  right_const_iterator ai = rightVars.find(A);
  right_const_iterator bi = rightVars.find(B);
  if (ai == rightVars.end() || bi == rightVars.end())
    return false;
  if (A == B)
    return true;
  Vertex a = ai->second;
  Vertex b = bi->second;
  for (df_iterator i = df_iterator::begin(G, b), end = df_iterator::end(G);
       i != end; ++i) {
    if (*i == a)
      return true;
  }
  return false;
}

void LCD::printDot(std::ostream& out) {
  G.printDot(std::cout);
}

LCD::Vertex LCD::ensureVertex(Value *X) {
  Vertex x;
  right_const_iterator kr = rightVars.find(X);
  if (kr == rightVars.end()) {
    x = G.addNode();
    rightVars[X] = x;
    leftVars[x].insert(X);
  }
  else
    x = kr->second;
  return x;
}

LCD::VertexCycles LCD::findCycles(Vertex s) {
  using PointsTo::df_iterator;
  VertexCycles result;
  for (df_iterator i = df_iterator::begin(G, s), end = df_iterator::end(G);
       i != end; ++i) {
    if (i.peekNext() != G.outEdgesEnd(*i)
        && G.getEdgeTarget(*i.peekNext()) == s) {
      result.push_back(i.getPath());
    }
  }
  return result;
}

void LCD::collapse(VertexList &path) {
  VertexList::iterator i = path.begin(), ie = path.end();
  Vertex origin = *i++;
  for (;i != ie; ++i) {
    Vertex u = *i;
    for (in_edge_iterator j = G.inEdgesBegin(u), je = G.inEdgesEnd(u);
         j != je; ++j) {
      Vertex v = G.getEdgeSource(*j);
      if (u == v)
        continue;
      if (v != origin && G.findEdge(v, origin) == G.edgesEnd())
        G.addEdge(v, origin);
      if (isset(pts[u], v))
        unset(pts[u], v), set(pts[origin], v);
      if (isset(stores[u], v))
        unset(stores[u], v), set(stores[origin], v);
      if (isset(loads[v], u))
        unset(loads[v], u), set(loads[v], origin);
    }
    for (out_edge_iterator k = G.outEdgesBegin(u), ke = G.outEdgesEnd(u);
         k != ke; ++k) {
      Vertex v = G.getEdgeTarget(*k);
      if (u == v)
        continue;
      if (origin != v && G.findEdge(origin, v) == G.edgesEnd())
        G.addEdge(origin, v);
    }
    for (std::set<Value *>::iterator i = leftVars[u].begin(), ie = leftVars[u].end();
         i != ie; ++i) {
      rightVars[*i] = origin;
      leftVars[origin].insert(*i);
    }
    leftVars.erase(u);
    G.removeNode(u);
  }
}

}
