//===-------------------- Graph.h - Points-to Graph -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Points-to Graph class.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_ANALYSIS_POINTSTO_GRAPH_H
#define LLVM_ANALYSIS_POINTSTO_GRAPH_H

#include "llvm/ADT/SmallSet.h"
#include <vector>
#include <list>
#include <algorithm>
#include <functional>

namespace PointsTo {

  /// Points-to Graph class.
  /// Instances of this class describe Points-to problems.
  class Graph {
  private:

    // ----- TYPEDEFS -----
    class NodeEntry;
    class EdgeEntry;

    // Nodes are stored in vectors to provide maximal performance
    // while iterating. These vectors contain elements which once
    // added, they are not removed, but marked as 'empty'. This
    // is due the fact points-to algorithms normally add much more
    // edges and vertexes than remove them.

    typedef std::vector<NodeEntry> NodeList;
    typedef std::vector<EdgeEntry> EdgeList;

  public:

    /// \brief The iterator jumps over removed elements. Also,
    /// it's stable against addition and removals. The vector
    /// element has to implement an empty() function.
    template<class Value>
    class JumpIterator
      : public std::iterator<std::input_iterator_tag, unsigned> {
    private:
      friend class Graph;
      typedef std::vector<Value> Vector;
      const Vector *v;
      unsigned i;

      JumpIterator(const Vector *v) : i(-1), v(v) {}
      JumpIterator(unsigned i, const Vector *v) : i(i), v(v) {
        for (; this->i < v->size(); ++this->i) {
          if (!(*v)[this->i].empty())
            break;
        }
        if (this->i == v->size())
          this->i = -1;
      }

    public:
      JumpIterator() {}
      JumpIterator(const JumpIterator &other)
        : i(other.i), v(other.v) {}
      JumpIterator &operator=(const JumpIterator &other) {
        i = other.i;
        v = other.v;
        return *this;
      }
      bool operator==(const JumpIterator &other) const {
        return i == other.i;
      }
      bool operator!=(const JumpIterator &other) const {
        return i != other.i;
      }
      const value_type &operator*() const {
        return i;
      }
      const value_type *operator->() const {
        return &i;
      }
      JumpIterator& operator++() {
        for (++i; i < v->size(); ++i) {
          if (!(*v)[i].empty())
            break;
        }
        if (i == v->size())
          i = -1;
        return *this;
      }
      JumpIterator operator++(int) {
        return operator++();
      }
    };

    typedef JumpIterator<NodeEntry> NodeItr;
    typedef const JumpIterator<NodeEntry> ConstNodeItr;

    typedef JumpIterator<EdgeEntry> EdgeItr;
    typedef const JumpIterator<EdgeEntry> ConstEdgeItr;

    typedef unsigned Node;
    typedef unsigned Edge;

  private:

    typedef std::list<Edge> AdjEdgeList;

  public:

    typedef AdjEdgeList::iterator AdjEdgeItr;
    typedef AdjEdgeList::const_iterator ConstAdjEdgeItr;

  private:

    class NodeEntry {
    private:
      friend class Graph;
      AdjEdgeList inEdges;
      AdjEdgeList outEdges;
      bool cleared;
      NodeEntry() : cleared(false) {}
    public:
      AdjEdgeItr inEdgesBegin() { return inEdges.begin(); }
      ConstAdjEdgeItr inEdgesBegin() const { return inEdges.begin(); }
      AdjEdgeItr inEdgesEnd() { return inEdges.end(); }
      ConstAdjEdgeItr inEdgesEnd() const { return inEdges.end(); }
      AdjEdgeItr outEdgesBegin() { return outEdges.begin(); }
      ConstAdjEdgeItr outEdgesBegin() const { return outEdges.begin(); }
      AdjEdgeItr outEdgesEnd() { return outEdges.end(); }
      ConstAdjEdgeItr outEdgesEnd() const { return outEdges.end(); }
      void addInEdge(Edge e) {
        inEdges.push_back(e);
      }
      void addOutEdge(Edge e) {
        outEdges.push_back(e);
      }
      void removeInEdge(Edge e) {
        AdjEdgeItr i = std::find_if(inEdges.begin(), inEdges.end(),
          std::bind1st(std::equal_to<Edge>(), e));
        if (i != inEdges.end())
          inEdges.erase(i);
      }
      void removeOutEdge(Edge e) {
        AdjEdgeItr i = std::find_if(outEdges.begin(), outEdges.end(),
          std::bind1st(std::equal_to<Edge>(), e));
        if (i != outEdges.end())
          outEdges.erase(i);
      }
      bool empty() const { return cleared; }
      void clear() { cleared = true; }
    };

    class EdgeEntry {
    private:
      friend class Graph;
      Node source, target;
      bool cleared;
    public:
      EdgeEntry(Node source, Node target)
        : source(source), target(target), cleared(false) {}
      Node getSource() const { return source; }
      Node getTarget() const { return target; }
      bool empty() const { return cleared; }
      void clear() { cleared = true; }
    };

    // ----- MEMBERS -----

    NodeList nodes;
    unsigned numNodes;

    EdgeList edges;
    unsigned numEdges;

    // ----- INTERNAL METHODS -----

    NodeEntry& getNode(Node n) {
      assert(n < nodes.size() && !nodes[n].empty()
             && "Attempt to access a removed node");
      return nodes[n];
    }

    const NodeEntry& getNode(Node n) const {
      assert(n < nodes.size() && !nodes[n].empty()
             && "Attempt to access a removed node");
      return nodes[n];
    }

    EdgeEntry& getEdge(Edge e) {
      assert(e < edges.size() && !edges[e].empty()
             && "Attempt to access a removed edge");
      return edges[e];
    }

    const EdgeEntry& getEdge(Edge e) const {
      assert(e < edges.size() && !edges[e].empty()
             && "Attempt to access a removed edge");
      return edges[e];
    }

    Node addConstructedNode(const NodeEntry &n) {
      ++numNodes;
      nodes.push_back(n);
      return nodes.size()-1;
    }

    Edge addConstructedEdge(const EdgeEntry &e) {
      assert(findEdge(e.getSource(), e.getTarget()) == edgesEnd() &&
             "Attempt to add duplicate edge.");
      ++numEdges;
      edges.push_back(e);
      Edge edge = edges.size()-1;
      EdgeEntry &ne = getEdge(edge);
      NodeEntry &source = getNode(ne.getSource());
      NodeEntry &target = getNode(ne.getTarget());
      source.addOutEdge(edge);
      target.addInEdge(edge);
      return edge;
    }

    Graph(const Graph &other);
    Graph& operator=(const Graph &other);

  public:

    /// \brief Construct an empty Points-to graph.
    Graph() : numNodes(0), numEdges(0) {}

    /// \brief Add a node.
    /// @return Node identity for the added node.
    Node addNode() {
      return addConstructedNode(NodeEntry());
    }

    /// \brief Add a directed edge from source to target nodes.
    /// @param source Source node.
    /// @param target Target node.
    /// @return Edge identity for the added edge.
    Edge addEdge(Node source, Node target) {
      return addConstructedEdge(EdgeEntry(source, target));
    }

    /// \brief Get the number of nodes in the graph.
    /// @return Number of nodes in the graph.
    unsigned getNumNodes() const { return numNodes; }

    /// \brief Get the number of edges in the graph.
    /// @return Number of edges in the graph.
    unsigned getNumEdges() const { return numEdges; }

    /// \brief Begin iterator for node set.
    NodeItr nodesBegin() { return NodeItr(0, &nodes); }

    /// \brief Begin const iterator for node set.
    ConstNodeItr nodesBegin() const { return NodeItr(0, &nodes); }

    /// \brief End iterator for node set.
    NodeItr nodesEnd() { return NodeItr(&nodes); }

    /// \brief End const iterator for node set.
    ConstNodeItr nodesEnd() const { return NodeItr(&nodes); }

    /// \brief Begin iterator for edge set.
    EdgeItr edgesBegin() { return EdgeItr(0, &edges); }

    /// \brief End iterator for edge set.
    EdgeItr edgesEnd() { return EdgeItr(&edges); }

    /// \brief Get begin iterator for incoming edges set.
    /// @param n Node identity.
    /// @return Begin iterator for the set of edges connected to the given node.
    AdjEdgeItr inEdgesBegin(Node n) {
      return getNode(n).inEdgesBegin();
    }
    ConstAdjEdgeItr inEdgesBegin(Node n) const {
      return getNode(n).inEdgesBegin();
    }

    /// \brief Get end iterator for incoming edges set.
    /// @param n Node identity.
    /// @return End iterator for the set of edges connected to the given node.
    AdjEdgeItr inEdgesEnd(Node n) {
      return getNode(n).inEdgesEnd();
    }
    ConstAdjEdgeItr inEdgesEnd(Node n) const {
      return getNode(n).inEdgesEnd();
    }

    /// \brief Get begin iterator for outgoing edges set.
    /// @param n Node identity.
    /// @return Begin iterator for the set of edges connected to the given node.
    AdjEdgeItr outEdgesBegin(Node n) {
      return getNode(n).outEdgesBegin();
    }
    ConstAdjEdgeItr outEdgesBegin(Node n) const {
      return getNode(n).outEdgesBegin();
    }

    /// \brief Get end iterator for outgoing edges set.
    /// @param n Node identity.
    /// @return End iterator for the set of edges connected to the given node.
    AdjEdgeItr outEdgesEnd(Node n) {
      return getNode(n).outEdgesEnd();
    }
    ConstAdjEdgeItr outEdgesEnd(Node n) const {
      return getNode(n).outEdgesEnd();
    }

    /// \brief Get the source node connected to this edge.
    /// @param e Edge identity.
    /// @return The source node connected to the given edge.
    Node getEdgeSource(Edge e) const {
      return getEdge(e).getSource();
    }

    /// \brief Get the target node connected to this edge.
    /// @param e Edge identity.
    /// @return The target node connected to the given edge.
    Node getEdgeTarget(Edge e) const {
      return getEdge(e).getTarget();
    }

    /// \brief Get the edge connecting two nodes.
    /// @param source Source node identity.
    /// @param target Target node identity.
    /// @return An iterator for edge (source, target) if such an edge exists,
    ///         otherwise returns edgesEnd().
    EdgeItr findEdge(Node source, Node target) {
      for (AdjEdgeItr aeItr = outEdgesBegin(source), aeEnd = outEdgesEnd(source);
           aeItr != aeEnd; ++aeItr) {
        if (getEdgeTarget(*aeItr) == target)
          return EdgeItr(*aeItr, &edges);
      }
      return edgesEnd();
    }

    /// \brief Remove a node from the graph.
    /// @param n Node identity.
    void removeNode(Node n) {
      NodeEntry &N = getNode(n);
      for (AdjEdgeItr itr = N.inEdgesBegin(), end = N.inEdgesEnd(); itr != end;) {
        Edge e = *itr;
        ++itr;
        removeEdge(e);
      }
      for (AdjEdgeItr itr = N.outEdgesBegin(), end = N.outEdgesEnd(); itr != end;) {
        Edge e = *itr;
        ++itr;
        removeEdge(e);
      }
      nodes[n].clear();
      --numNodes;
    }

    /// \brief Remove an edge from the graph.
    /// @param e Edge identity.
    void removeEdge(Edge e) {
      EdgeEntry &E = getEdge(e);
      NodeEntry &source = getNode(E.getSource());
      NodeEntry &target = getNode(E.getTarget());
      source.removeOutEdge(e);
      target.removeInEdge(e);
      edges[e].clear();
      --numEdges;
    }

    /// \brief Remove all nodes and edges from the graph.
    void clear() {
      nodes.clear();
      edges.clear();
      numNodes = numEdges = 0;
    }

    /// \brief Dump a graph to an output stream.
    template <typename OStream>
    void dump(OStream &os) {
      os << "nodes: ";
      for (NodeItr nodeItr = nodesBegin(), nodeEnd = nodesEnd();
           nodeItr != nodeEnd; ++nodeItr) {
        if (nodeItr != nodesBegin())
          os << ", ";
        os << *nodeItr;
      }
      os << "\n";

      os << "edges: ";
      for (EdgeItr edgeItr = edgesBegin(), edgeEnd = edgesEnd();
           edgeItr != edgeEnd; ++edgeItr) {
        if (edgeItr != edgesBegin())
          os << ", ";
        os << "("
           << getEdgeSource(*edgeItr)
           << ", "
           << getEdgeTarget(*edgeItr)
           << ")";
      }
      os << "\n";
    }

    /// \brief Print a representation of this graph in DOT format.
    /// @param os Output stream to print on.
    template <typename OStream>
    void printDot(OStream &os) {

      os << "graph {\n";

      for (NodeItr nodeItr = nodesBegin(), nodeEnd = nodesEnd();
           nodeItr != nodeEnd; ++nodeItr) {
        os << *nodeItr << "\n";
      }

      for (EdgeItr edgeItr = edgesBegin(), edgeEnd = edgesEnd();
           edgeItr != edgeEnd; ++edgeItr) {

        os << getEdgeSource(*edgeItr)
           << " -> "
           << getEdgeTarget(*edgeItr)
           << "\n";
      }

      os << "}\n";
    }

  };

  /// A depth first iterator for the Graph.
  class df_iterator : public std::iterator<std::forward_iterator_tag,
                                           int, ptrdiff_t> {

    struct StackTuple {
      StackTuple(Graph::Node Node, const Graph *G)
        : Node(Node), OutEdgeItr(G->outEdgesBegin(Node)) {}
      Graph::Node Node;
      Graph::ConstAdjEdgeItr OutEdgeItr;
      bool operator==(const StackTuple &other) const {
        return Node == other.Node
               && OutEdgeItr == other.OutEdgeItr;
      }
    };

    std::vector<StackTuple> VisitStack;
    llvm::SmallSet<Graph::Node, 8> Visited;
    const Graph *G;

  private:
    df_iterator(const Graph &G, Graph::Node Node) : G(&G) {
      Visited.insert(Node);
      VisitStack.push_back(StackTuple(Node, this->G));
    }

    df_iterator() {
      // End is when stack is empty
    }

    void toNext() {
      do {
        StackTuple &Top = VisitStack.back();
        while (Top.OutEdgeItr != G->outEdgesEnd(Top.Node)) {
          Graph::Node Next = G->getEdgeTarget(*Top.OutEdgeItr++);
          // Has our next sibling been visited?
          if (!Visited.count(Next)) {
            // No, do it now.
            Visited.insert(Next);
            VisitStack.push_back(StackTuple(Next, G));
            return;
          }
        }
        // Oops, ran out of successors... go up a level on the stack.
        VisitStack.pop_back();
      } while (!VisitStack.empty());
    }

  public:
    static inline df_iterator begin(const Graph& G, Graph::Node start) {
      return df_iterator(G, start);
    }
 
    static inline df_iterator end(const Graph& G) { return df_iterator(); }
 
    bool operator==(const df_iterator& x) const {
      return VisitStack == x.VisitStack;
    }
 
    bool operator!=(const df_iterator& x) const { return !operator==(x); }
 
    const Graph::Node &operator*() const {
      return VisitStack.back().Node;
    }
 
    const Graph::Node *operator->() const {
      return &operator*();
    }
 
    inline df_iterator& operator++() {
      toNext();
      return *this;
    }
 
    inline df_iterator operator++(int) {
      df_iterator tmp = *this;
      ++*this;
      return tmp;
    }

    Graph::ConstAdjEdgeItr peekNext() const {
      return VisitStack.back().OutEdgeItr;
    }
 
    /// getPath - Return the nodes path up to this point, including
    /// the start node
    std::vector<Graph::Node> getPath() const {
      std::vector<Graph::Node> path;
      for (unsigned i = 0; i < VisitStack.size(); ++i)
        path.push_back(VisitStack[i].Node);
      return path;
    }
  };

} // end of PointsTo namespace

#endif // LLVM_ANALYSIS_POINTSTO_GRAPH_H
