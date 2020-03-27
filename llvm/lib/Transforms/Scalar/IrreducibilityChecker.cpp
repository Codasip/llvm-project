//===-- IrreducibilityChecker.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/IrreducibilityChecker.h"

using namespace llvm;
using namespace irreducibility;

////////////////////////////////////////////////////////////////////////////////////////////////////
bool Vertex::isOnlyPredecessorOf(const Vertex &V) const
{
  bool isPredecessor = false;
  bool isOnlyPredecessor = true;

  for (Edge *P : V.getPredecessors()) {
    if (&P->getIn() != this)
      isOnlyPredecessor = false;
    else
      isPredecessor = true;
  }

  assert(isPredecessor && "Given vertex is not a predecessor of this vertex.");
  (void)isPredecessor;
  return isOnlyPredecessor;
}

void Vertex::removePredecessor(Edge &E)
{
  Edges &P = getPredecessors();
  P.erase(std::remove(P.begin(), P.end(), &E), P.end());
}

void Vertex::removeSuccessor(Edge &E)
{
  Edges &S = getSuccessors();
  S.erase(std::remove(S.begin(), S.end(), &E), S.end());
}
