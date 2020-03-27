//===-- JTEdge.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTBlock.h"
#include "llvm/IR/JTEdge.h"

using namespace llvm;
using namespace jumpthreading;

JTEdge &JTEdge::CreateUnconditional(JTBlock &InBlock, JTBlock &OutBlock)
{
  JTEdge *Edge = new JTEdge(UNCONDITIONAL, InBlock, OutBlock);
  assert(Edge && "Failed allocation of unconditional jump edge.");
  return *Edge;
}

JTEdge &JTEdge::CreateConditional(JTBlock &InBlock, JTBlock &OutBlock, const bool Condition)
{
  JTEdge *Edge = new JTEdge(CONDITIONAL, InBlock, OutBlock);
  assert(Edge && "Failed allocation of conditional jump edge.");
  Edge->setCondition(Condition);
  return *Edge;
}

JTEdge &JTEdge::CreateSwitch(
  JTBlock &InBlock, JTBlock &OutBlock, const bool IsDefault, ConstantInt *SwitchValue)
{
  if (IsDefault)
    assert(SwitchValue == nullptr && "Unexpected switch case value on default case.");
  else
    assert(SwitchValue && "Expecting switch case value on non-default case.");

  JTEdge *Edge = new JTEdge(SWITCH, InBlock, OutBlock);
  assert(Edge && "Failed allocation of switch case jump edge.");
  Edge->setCondition(!IsDefault);
  Edge->setSwitchValue(SwitchValue);
  return *Edge;
}

JTEdge &JTEdge::CreateIndirect(JTBlock &InBlock, JTBlock &OutBlock, const unsigned int Address)
{
  JTEdge *Edge = new JTEdge(INDIRECT, InBlock, OutBlock);
  assert(Edge && "Failed allocation of indirect address jump edge.");
  Edge->setAddress(Address);
  return *Edge;
}

JTEdge &JTEdge::CreateCopy(
  const JTEdge &Edge, JTBlock &InBlock, JTBlock &OutBlock, const bool UpdateOutBlock)
{
  JTEdge *Copy = new JTEdge(Edge.getType(), InBlock, OutBlock, UpdateOutBlock);
  assert(Copy && "Failed allocation of edge copy.");
  Copy->setCondition(Edge.getCondition());
  Copy->setSwitchValue(Edge.getSwitchValue());
  Copy->setAddress(Edge.getAddress());
  return *Copy;
}

void JTEdge::deleteEdge()
{
  getInBlock().removeSuccessor(*this);
  getOutBlock().removePredecessor(*this);
  delete this;
}

void JTEdge::dump() const
{
  if (isUnconditional())
    LLVM_DEBUG(dbgs() << "jump " << getInBlock().getName() << "=>" << getOutBlock().getName());
  else if (isConditional())
    LLVM_DEBUG(dbgs() << "cond_jump " << getInBlock().getName() << " => " << getOutBlock().getName() << " [" << std::to_string(getCondition()) << "]");
  else if (isSwitch()) {
    LLVM_DEBUG(dbgs() << "switch " << getInBlock().getName() << " => " << getOutBlock().getName() << " [" << std::to_string(getCondition()) << "][");
    if (getCondition())
      LLVM_DEBUG(dbgs() << *getSwitchValue());
    else
      LLVM_DEBUG(dbgs() << "-");
    LLVM_DEBUG(dbgs() << "]");
  } else if (isIndirect()) {
    LLVM_DEBUG(dbgs() << "indirect_jump " << getInBlock().getName() << " => " << getOutBlock().getName() << " [" << std::to_string(getAddress()) << "][]");
  } else
    assert(false);
}

JTEdge::JTEdge(
  const EdgeType Type,
  JTBlock &InBlock,
  JTBlock &OutBlock,
  const bool UpdateOutBlock)
: Type(Type),
  In(&InBlock),
  Out(&OutBlock),
  Condition(false),
  SwitchValue(nullptr),
  Address(0)
{
  InBlock.addSuccessor(*this);
  if (UpdateOutBlock)
    OutBlock.addPredecessor(*this);
}
