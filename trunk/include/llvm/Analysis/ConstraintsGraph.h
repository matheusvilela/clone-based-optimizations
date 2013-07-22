//===- ConstraintsGraph.h - Build a Module's points-to constraints graph -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This interface is used to build a points-to constraints graph, which is a
// very useful tool for points-to analysis.
//
// Every value in a module is represented as a node in the graph.  Each
// directed edge carries a constraint between two values.
//
// The constraints graph contains nodes where the values are statically
// assigned as Zero or NullPointer.  Also, there's a special node to represent
// unknown assignments (UniversalSet); be carefull while handling them.
//
// There are some limitations on the graph, though:
//   1. All functions in the module without internal linkage will have the
//      return value and it's parameters short-circuited in a 'Copy'
//      constraint.
//   2. This is the same case for external functions, saving some well-known
//      functions from standard libraries.
//
// Because of these properties, the ConstraintsGraph captures a conservative
// superset of all pointer assignments of the module, which is useful for
// alias analysis passes.
//
// Note that the ConstraintsGraph class is not likely to have a root.  Further
// Alias Analysis normally iterate over the nodes and edges of the graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CONSTRAINTSGRAPH_H
#define LLVM_ANALYSIS_CONSTRAINTSGRAPH_H

#include "llvm/Pass.h"
#include "llvm/Support/IncludeFile.h"
#include "llvm/Support/ValueHandle.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/InstVisitor.h"
#include <map>

namespace llvm {

class Module;
class Function;
class TargetLibraryInfo;

//===----------------------------------------------------------------------===//
// ConstraintsGraph class definition
//
class ConstraintsGraph : private InstVisitor<ConstraintsGraph> {
private:
  friend class InstVisitor<ConstraintsGraph>;

  Module *Mod;              // The module this constraints graph represents
  const TargetLibraryInfo *TLI;

public:
  enum ConstraintType { Copy, Load, Store, AddressOf };

  /// Objects of this structure are used to represent the various constraints
  /// identified by the pass.  The constraints are 'Copy', for statements like
  /// "A = B", 'Load' for statements like "A = *B", 'Store' for statements like
  /// "*A = B", and 'AddressOf' for statements like "A = alloca".  The Offset
  /// is applied as *(A + K) = B for stores, A = *(B + K) for loads, and
  /// A = B + K for copies; it is illegal on addressof constraints (because it
  /// is statically resolvable to A = &C where C = B + K)
  class Constraint {
  private:
    friend class ConstraintsGraph;

    unsigned Target;
    unsigned Source;
    unsigned Offset;
    ConstraintType Type;

    Constraint(ConstraintType Ty, unsigned D, unsigned S, unsigned O = 0)
      : Type(Ty), Target(D), Source(S), Offset(O) {
      assert((Offset == 0 || Ty != AddressOf) &&
             "Offset is illegal on addressof constraints");
    }

  public:
    Constraint() : Type(AddressOf), Target(0), Source(0), Offset(0) {}
    ~Constraint() {}

    /// Get the target of a constraint of type Source -> Target.
    unsigned getTarget() const { return Target; }

    /// Get the source of a constraint of type Source -> Target.
    unsigned getSource() const { return Source; }

    /// Get the offset, only in case of an 'AddressOf' constraint.
    unsigned getOffset() const { return Offset; }

    /// Get the constraint type.
    ConstraintType getType() const { return Type; }
  };

  // This class is used to represent a node in the constraint graph.  Due to
  // various optimizations, it is not always the case that there is a mapping
  // from a Node to a Value.  In particular, we add artificial Node's that
  // represent the set of pointed-to variables shared for each location
  // equivalent Node.
  struct Node {
  private:
    friend class ConstraintsGraph;

    Value *Val;
    SparseBitVector<> Constraints;

    Node(Value *V) : Val(V) {}

    void setValue(Value *V) { Val = V; }
    void addConstraint(unsigned Idx) {
      Constraints.set(Idx);
    }

  public:
    typedef SparseBitVector<>::iterator constraints_iterator;

    Node() : Val(0) {}

    /// Return the LLVM value corresponding to this node. This value can be
    /// null in some situations (i.e. "*A = *B"). Therefore, you have to check
    /// the value for yourself before using it.
    Value *getValue() const { return Val; }

    /// Convenience iterator.
    constraints_iterator constraintsBegin() const {
      return Constraints.begin();
    }
    constraints_iterator constraintsEnd() const {
      return Constraints.end();
    }
  };

private:
  // This vector is populated as part of the object identification stage,
  // which populates this vector with a node for each memory object and
  // fills in the ValueNodes map.
  std::vector<Node> Nodes;

  /// This vector contains a list of all of the constraints identified by
  /// the pass.
  std::vector<Constraint> Constraints;

  // ValueNodes - This map indicates the Node that a particular Value* is
  // represented by.  This contains entries for all pointers.
  DenseMap<Value*, unsigned> ValueNodes;

  // ObjectNodes - This map contains entries for each memory object in the
  // program: globals, alloca's and mallocs.
  DenseMap<Value*, unsigned> ObjectNodes;

  // ReturnNodes - This map contains an entry for each function in the
  // program that returns a value.
  DenseMap<Function*, unsigned> ReturnNodes;

  // VarargNodes - This map contains the entry used to represent all pointers
  // passed through the varargs portion of a function call for a particular
  // function.  An entry is not present in this map for functions that do not
  // take variable arguments.
  DenseMap<Function*, unsigned> VarargNodes;

private:
  Node &getNode(unsigned Idx) {
    assert(Idx < Nodes.size() && "Invalid node index");
    return Nodes[Idx];
  }

protected:
  //===---------------------------------------------------------------------
  // Modifiers for implementers of the interface.
  //

  /// Preallocate a bunch of nodes.
  void allocateNodes(unsigned numNodes) {
    Nodes.resize(numNodes);
  }

  /// Add a node to the graph, iteratively.
  unsigned addNode(Value *V=0) {
    unsigned Idx = Nodes.size();
    Nodes.push_back(Node(V));
    return Idx;
  }

  /// Add a constraint to the graph and return it's index.
  unsigned addConstraint(ConstraintType Type, unsigned Target, unsigned Source,
                         unsigned Offset = 0);

  /// Set a node value.
  void setNodeValue(unsigned Idx, Value *V) {
    getNode(Idx).setValue(V);
  }

  /// Return the node corresponding to the specified pointer scalar.
  unsigned getNode(Value *V);

  /// Return the node corresponding to the memory object for the
  /// specified global or allocation instruction.
  unsigned getObject(Value *V) const;

  /// Return the node representing the return value for the
  /// specified function.
  unsigned getReturnNode(Function *F) const;

  /// Return the node representing the variable arguments
  /// formal for the specified function.
  unsigned getVarargNode(Function *F) const;

  /// Get the node for the specified LLVM value and set the
  /// value for it to be the specified value.
  unsigned getNodeValue(Value &V);

public:
  //===---------------------------------------------------------------------
  // Accessors.
  //
  typedef std::vector<Node>::const_iterator node_iterator;
  typedef Node::constraints_iterator constraints_iterator;

  /// This enum defines the GraphNodes indices that correspond to important
  /// fixed sets.
  enum {
    UniversalSet = 0,
    NullPtr      = 1,
    NullObject   = 2,
    NumberSpecialNodes
  };

  /// Get the total number of mapped nodes.
  unsigned getNumNodes() const {
    return Nodes.size();
  }

  /// Get the total number of mapped constraints.
  unsigned getNumConstraints() const {
    return Constraints.size();
  }

  /// Iterate over the mapped nodes.
  node_iterator nodesBegin() const {
    return Nodes.begin();
  }
  node_iterator nodesEnd() const {
    return Nodes.end();
  }

  /// Get the mapped node.
  const Node &getNode(unsigned Idx) const {
    assert(Idx < Nodes.size() && "Invalid node index");
    return Nodes[Idx];
  }

  /// Get the mapped constraint.
  const Constraint &getConstraint(unsigned Idx) const {
    assert(Idx < Constraints.size() && "Invalid constraint index");
    return Constraints[Idx];
  }

  //===---------------------------------------------------------------------
  // Pass infrastructure interface glue code.
  //
public:
  ConstraintsGraph() : Mod(0) {}
  virtual ~ConstraintsGraph() { destroy(); }

  /// Call this method before calling other methods, re/initializes the state
  /// of the ConstraintsGraph.
  void Initialize(Module &M, Pass *P);

  /// Return the module the constraints graph corresponds to.
  Module &getModule() const { return *Mod; }

  /// Print out this constraints graph.
  void dump() const;
  void print(raw_ostream &OS) const;

  // Release memory for the call graph.
  void destroy();

private:
  //===------------------------------------------------------------------===//
  // Instruction visitation methods for adding constraints
  //
  void visitReturnInst(ReturnInst &RI);
  void visitCallInst(CallInst &CI);
  void visitCallSite(CallSite CS);
  void visitAllocaInst(AllocaInst &I);
  void visitLoadInst(LoadInst &LI);
  void visitStoreInst(StoreInst &SI);
  void visitGetElementPtrInst(GetElementPtrInst &GEP);
  void visitPHINode(PHINode &PN);
  void visitCastInst(CastInst &CI);
  void visitSelectInst(SelectInst &SI);
  void visitVAArg(VAArgInst &I);
  void visitInstruction(Instruction &I);

  // Extension to handle memory allocations
  void visitAlloc(Instruction &I);

  void addConstraintsForCall(CallSite CS, Function *F);
  bool addConstraintsForExternalCall(CallSite CS, Function *F);

  void identifyObjects(Module &M);
  void collectConstraints(Module &M);

  /// Return the node corresponding to the constant pointer itself.
  unsigned getNodeForConstantPointer(Constant *C);

  /// Add inclusion constraints for the memory object N, which contains
  /// values indicated by C.
  void addGlobalInitializerConstraints(unsigned NodeIndex, Constant *C);

  /// If this function does not have internal linkage, realize that we
  /// can't trust anything passed into or returned by this function.
  void addConstraintsForNonInternalLinkage(Function *F);

  /// Look at all of the users of the specified function. If this is used
  /// by anything complex (i.e., the address escapes), return true.
  bool analyzeUsesOfFunction(Value *V);
};

} // End llvm namespace

// Make sure that any clients of this file link in CallGraph.cpp
FORCE_DEFINING_FILE_TO_BE_LINKED(ConstraintsGraph)

#endif
