//===-- JTThreadableNode.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTThreadableNode.h"
#include "llvm/Support/FileSystem.h"

using namespace llvm;
using namespace jumpthreading;

////////////////////////////////////////////////////////////////////////////////////////////////////
JTThreadableNode::~JTThreadableNode()
{
  for (JTThreadableNode *Child : getChildren())
    delete Child;
}

void JTThreadableNode::printDot(const std::string &Path) const
{
  std::error_code EC;
  raw_fd_ostream Out(Path, EC, sys::fs::OpenFlags::F_None);
  assert(!EC);
  Out << "digraph G {\n";

  printDot(Out);

  Out << "}\n";
  Out.close();
}

JTThreadableNode::JTThreadableNode(const std::string &Identifier, const Value &Node)
  : Id(Identifier),
    V(Node),
    Parent(nullptr),
    Cost(0)
{}

/**
 *  \brief  Print *.dot information of this threadable node.
 *  \param[out] Out
 */
void JTThreadableNode::printDot(raw_fd_ostream &Out) const
{
  // Node
  Out << "\"" << getId() << "\" [label=\"" << getNodeName() << "\\n";

  if (getBlocks().empty())
    Out << "-";
  bool first = true;
  for (JTBlock *Block : getBlocks()) {
    if (first)
      first = false;
    else
      Out << "\\n";
    Out << Block->getName();
  }
  Out << "\\nCost: " << getCost() << "\"]\n";

  // Parent
  if (getParent())
    Out << "\"" << getId() << "\" -> \"" << getParent()->getId() << "\" [color=grey]\n";

  // Children
  for (const JTThreadableNode *Child : getChildren()) {
    Out << "\"" << getId() << "\" -> \"" << Child->getId() << "\"\n";
    Child->printDot(Out);
  }
}
