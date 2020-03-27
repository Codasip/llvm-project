//===-- JTBlock.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTBlock.h"

using namespace llvm;
using namespace jumpthreading;

////////////////////////////////////////////////////////////////////////////////////////////////////
bool JTBlock::isPredecessor(BasicBlock &BB)
{
  for (JTEdge *Edge : getPredecessors())
    if (&Edge->getInBlock().getReference() == &BB)
      return true;
  return false;
}

bool JTBlock::isPredecessorOf(JTBlock &Block)
{
  for (JTEdge *Edge : Block.getPredecessors())
    if (&Edge->getInBlock() == this)
      return true;
  return false;
}

bool JTBlock::isSuccessorOf(JTBlock &Block)
{
  for (JTEdge *Edge : Block.getSuccessors())
    if (&Edge->getOutBlock() == this)
      return true;
  return false;
}

BasicBlock *JTBlock::getDefaultCase()
{
  Edges DefaultCases;
  for (JTEdge *Edge : getSuccessors()) {
    assert(Edge->getType() == JTEdge::SWITCH);
    if (!Edge->getCondition())
      DefaultCases.push_back(Edge);
  }
  assert(!DefaultCases.empty() && "Missing default case."); // what to do when switch default case is missing?
  assert(DefaultCases.size() == 1 && "Multiple default cases."); // and here?
  return &DefaultCases.front()->getOutBlock().getReference();
}

JTBlock::JTBlock(const std::string &Identifier)
: Id(Identifier),
  Original(nullptr),
  Reference(nullptr),
  Duplicated(false),
  Reachable(false)
{}

/**
 *  \brief  Determines if Predecessors contains given edge.
 *  \param[in]  Edge
 */
bool JTBlock::containsPredecessorEdge(JTEdge *Edge)
{
  for (JTEdge *E : getPredecessors())
    if (E == Edge)
      return true;
  return false;
}
