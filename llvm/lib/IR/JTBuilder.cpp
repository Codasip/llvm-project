//===-- JTBuilder.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTBuilder.h"
#include "llvm/IR/JTGraph.h"
#include "llvm/IR/JTThreadableNode.h"

using namespace llvm;
using namespace jumpthreading;

JTBuilder::JTBuilder()
  : ThreadableNodeCount(0),
    BlockCount(0)
{}

JTThreadableNode &JTBuilder::CreateThreadableNode(const Value &V)
{
  const std::string Id = "Node_" + std::to_string(ThreadableNodeCount++);
  JTThreadableNode *Node = new JTThreadableNode(Id, V);
  assert(Node && "Allocation of JTThreadableNode failed.");
  return *Node;
}

JTBlock &JTBuilder::CreateBlock(BasicBlock &Reference)
{
  const std::string Id = "Block_" + std::to_string(BlockCount++);
  JTBlock *Block = new JTBlock(Id);
  assert(Block && "Allocation of JTBlock failed.");
  Block->setReference(Reference);
  return *Block;
}

JTBlock &JTBuilder::CreateBlock(JTBlock &Original)
{
  const std::string Id = "Block_" + std::to_string(BlockCount++);
  JTBlock *Block = new JTBlock(Id);
  assert(Block && "Allocation of JTBlock failed.");
  Block->setOriginal(Original);
  return *Block;
}

JTGraph &JTBuilder::CreateGraph()
{
  JTGraph *Graph = new JTGraph();
  assert(Graph && "Allocation of JTGraph failed.");
  return *Graph;
}

void JTBuilder::updateDuplicateName(JTBlock &Block)
{
  const std::string Name = Block.getName().str();
  auto it = BlockIndexes.find(Name);
  // create if does not exist
  if (it == BlockIndexes.end()) {
    BlockIndexes.insert(std::make_pair(Name, 0));
    it = BlockIndexes.find(Name);
  }
  assert(it != BlockIndexes.end());
  const std::string NewName = Name + ".jt" + std::to_string(it->second++);
  Block.getReference().setName(NewName);
}

void JTBuilder::updateDuplicateName(Value &I)
{
  const std::string Name = I.getName().str();
  auto it = InstructionIndexes.find(Name);
  // create if does not exist
  if (it == InstructionIndexes.end()) {
      InstructionIndexes.insert(std::make_pair(Name, 0));
    it = InstructionIndexes.find(Name);
  }
  assert(it != InstructionIndexes.end());
  const std::string NewName = Name + ".jt" + std::to_string(it->second++);
  I.setName(NewName);
}
