//===- IrreducabilityChecker.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IRREDUCIBILITYCHECKER_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IRREDUCIBILITYCHECKER_H_

#include <set>
#include <sstream>

#include "llvm/Support/FileSystem.h"

namespace llvm {
namespace irreducibility {

// Forward declarations.
class Edge;

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief  Represents a vertex of the graph.
class Vertex
{
public:
  /// Identifier of the vertex, mainly for debugging purposes
  typedef std::set<unsigned int> Identifier;
  /// Vector of edges
  typedef std::vector<Edge*> Edges;

  Vertex();

  /**
   *  \{
   *  \brief  Getter
   */
  Identifier &getId();
  const Identifier &getId() const;
  const std::string getIdStr() const;
  unsigned int getIndex() const;
  Edges &getPredecessors();
  const Edges &getPredecessors() const;
  Edges &getSuccessors();
  const Edges &getSuccessors() const;
  /**
   *  \}
   *  \{
   *  \brief  Setter
   */
  void addId(unsigned int Value);
  void setIndex(unsigned int Value);
  void addPredecessor(Edge &E);
  void addSuccessor(Edge &E);
  /// \}

  /**
   *  \brief  Returns true when this vertex is the only predecessor of Vertex V, false otherwise.
   *  \param[in]  V Vertex
   */
  bool isOnlyPredecessorOf(const Vertex &V) const;
  /**
   *  \brief Remove edge E from predecessors of this Vertex.
   *  \param[in]  E Removed edge
   */
  void removePredecessor(Edge &E);
  /**
   *  \brief Remove edge E from successors of this Vertex.
   *  \param[in]  E Removed edge
   */
  void removeSuccessor(Edge &E);

private:
  /// Identifier
  Identifier Id;
  /// Index, it is managed by Graphs addVertex and removeVertex methods
  unsigned int Index;
  /// Predecessors
  Edges Predecessors;
  /// Successors
  Edges Successors;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline Vertex::Vertex()
: Index(0)
{}

inline Vertex::Identifier &Vertex::getId()
{
  return Id;
}

inline const Vertex::Identifier &Vertex::getId() const
{
  return Id;
}

inline const std::string Vertex::getIdStr() const
{
  std::stringstream ss;
  for (const unsigned int Value : getId()) {
    if (!ss.str().empty())
      ss << ", ";
    ss << Value;
  }
  return ss.str();
}

inline unsigned int Vertex::getIndex() const
{
  return Index;
}

inline Vertex::Edges &Vertex::getPredecessors()
{
  return Predecessors;
}

inline const Vertex::Edges &Vertex::getPredecessors() const
{
  return Predecessors;
}

inline Vertex::Edges &Vertex::getSuccessors()
{
  return Successors;
}

inline const Vertex::Edges &Vertex::getSuccessors() const
{
  return Successors;
}

inline void Vertex::addId(unsigned int Value)
{
  Id.insert(Value);
}

inline void Vertex::setIndex(unsigned int Value)
{
  Index = Value;
}

inline void Vertex::addPredecessor(Edge &E)
{
  Predecessors.push_back(&E);
}

inline void Vertex::addSuccessor(Edge &E)
{
  Successors.push_back(&E);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief  Represents an edge of the graph.
class Edge
{
public:
  Edge(Vertex &Incoming, Vertex &Outgoing);

  /**
   *  \{
   *  \brief  Getter
   */
  Vertex &getIn();
  const Vertex &getIn() const;
  Vertex &getOut();
  const Vertex &getOut() const;
  /** \}
   *  \{
   *  \brief  Setter
   */
  void setIn(Vertex &V);
  void setOut(Vertex &V);
  /// \}

  /// \brief  Removes edge from successors of Vertex In, predecessors of Vertex Out and deletes it.
  void deleteEdge();
  /// \brief  Determines reflexivity of the edge (Vertex In == Vertex Out).
  bool isReflexive() const;

private:
  /// Incoming vertex
  Vertex *In;
  /// Outgoing vertex
  Vertex *Out;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline Edge::Edge(Vertex &Incoming, Vertex &Outgoing)
: In(&Incoming),
  Out(&Outgoing)
{
  In->addSuccessor(*this);
  Out->addPredecessor(*this);
}

inline Vertex &Edge::getIn()
{
  return *In;
}

inline const Vertex &Edge::getIn() const
{
  return *In;
}

inline Vertex &Edge::getOut()
{
  return *Out;
}

inline const Vertex &Edge::getOut() const
{
  return *Out;
}

inline void Edge::setIn(Vertex &V)
{
  In = &V;
}

inline void Edge::setOut(Vertex &V)
{
  Out = &V;
}

inline void Edge::deleteEdge()
{
  In->removeSuccessor(*this);
  Out->removePredecessor(*this);
  delete this;
}

inline bool Edge::isReflexive() const
{
  return In == Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
/// \brief  Represents graph obviously.
class Graph
{
public:
  /// \brief  Deletes graph with all the contained vertices.
  ~Graph();

  /**
   *  \{
   *  \brief  Setter
   */
  void addVertex(Vertex &V);
  void removeVertex(Vertex &V);
  /// \}

  ///  \brief  Reduces graph towards trivial graph. Returns whether resulting graph is trivial.
  bool isReducible();
  /// \brief  Prints dot of this graph
  void printDot(const std::string &Path) const;

private:
  /// List of vertices
  typedef std::vector<Vertex*> VerticesList;

  void reduce();

  /// Vertices
  VerticesList Vertices;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline Graph::~Graph()
{
  for (Vertex *V : Vertices) {
    for (Edge *E : V->getSuccessors())
      delete E;
    delete V;
  }
}

inline void Graph::addVertex(Vertex &V)
{
  V.setIndex(Vertices.size());
  Vertices.push_back(&V);
}

inline void Graph::removeVertex(Vertex &V)
{
  unsigned int Erased = V.getIndex();
  Vertices.erase(Vertices.begin() + Erased);
  // Recompute indexes of all vertices after erased vertex
  for (size_t i = Erased; i < Vertices.size(); i++)
    Vertices[i]->setIndex(i);
}

inline bool Graph::isReducible()
{
  assert(!Vertices.empty() && "Unexpected empty graph.");
  reduce();
  return Vertices.size() == 1;
}

inline void Graph::printDot(const std::string &Path) const
{
  std::error_code EC;
  raw_fd_ostream Out(Path, EC, sys::fs::OpenFlags::F_None);
  assert(!EC);
  Out << "digraph G {\n";

  for (Vertex *V : Vertices) {
    Out << "\"" << V->getIdStr() << "\"\n";
    for (const Edge *P : V->getPredecessors())
      Out << "\"" << P->getOut().getIdStr() << "\" -> \"" << P->getIn().getIdStr() << "\" [style=dashed, color=\"gray\"]\n";
    for (const Edge *S : V->getSuccessors())
      Out << "\"" << S->getIn().getIdStr() << "\" -> \"" << S->getOut().getIdStr() << "\"\n";
  }

  Out << "}\n";
  Out.close();
}

/**
 *  \brief  Apply transformations on graph to reduce him.
 *          Inspired at https://parasol.tamu.edu/~rwerger/Courses/605/L7.pdf.
 */
inline void Graph::reduce()
{
  bool Changed = false;
  do {
    Changed = false;

    for (size_t i = 0; i < Vertices.size(); i++) {
      Vertex &V = *Vertices[i];

      // Apply T1 on all nodes successors
      auto It = V.getSuccessors().begin();
      while (It != V.getSuccessors().end()) {
        Edge &E = **It;
        if (E.isReflexive()) {
          E.deleteEdge();
          It = V.getSuccessors().begin();
          Changed = true;
        } else
          ++It;
      }

      // Apply T2 on first valid successor, if has one
      bool IsT2Applied = false;
      for (size_t j = 0; j < V.getSuccessors().size(); j++) {
        Vertex &S = V.getSuccessors()[j]->getOut();
        if (V.isOnlyPredecessorOf(S)) {

          // Create new vertex N, which stands for vertexes V and S
          Vertex *N = new Vertex();
          assert(N && "Allocation of Vertex failed.");

          // Copy both identifiers from V and S into N
          N->getId().insert(V.getId().begin(), V.getId().end());
          N->getId().insert(S.getId().begin(), S.getId().end());

          // Add predecessors of V into N
          Vertex::Edges &NPs = N->getPredecessors();
          Vertex::Edges &VPs = V.getPredecessors();
          NPs.insert(NPs.end(), VPs.begin(), VPs.end());
          VPs.clear();

          // Add successors of both V and S into N
          Vertex::Edges Removal;
          for (Edge *E : V.getSuccessors())
            if (&E->getOut() == &S)
              Removal.push_back(E);
            else
              N->addSuccessor(*E);
          for (Edge *E : S.getSuccessors())
            if (&E->getOut() == &S)
              Removal.push_back(E);
            else
              N->addSuccessor(*E);

          for (Edge *E : Removal)
            E->deleteEdge();

          V.getSuccessors().clear();
          S.getSuccessors().clear();

          // Reconnect incoming and outgoing vertices of old edges
          for (Edge *NP : N->getPredecessors())
            NP->setOut(*N);
          for (Edge *SP : N->getSuccessors())
            SP->setIn(*N);

          // Remove old vertexes V and S
          removeVertex(V);
          removeVertex(S);

          // Add new vertex
          addVertex(*N);
          Changed = true;
          IsT2Applied = true;
          break;
        }
      }

      // Reset is needed after T2 application
      if (IsT2Applied)
        break;
    }
  } while (Changed);
}

} // namespace llvm::irreducibility
} // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IRREDUCIBILITYCHECKER_H_
