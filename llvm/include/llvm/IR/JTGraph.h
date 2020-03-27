//===- JTGraph.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTGRAPH_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTGRAPH_H_

#include "llvm/Support/FileSystem.h"

#include "JTBlock.h"

namespace llvm {
namespace jumpthreading {

/**
 *  \brief  Represents Jump Threading graph.
 */
class JTGraph
{
public:
  /// JT basic blocks
  typedef std::vector<JTBlock*> JTBlocks;
  /// LLVM basic blocks => JT basic blocks
  typedef std::map<BasicBlock*, JTBlock*> BlockMap;
  /// Variables present in conditions
  typedef std::set<std::string> VariableSet;

  ~JTGraph();

  JTBlocks &getBlocks();
  const JTBlocks &getBlocks() const;
  BlockMap &getBlockMapping();
  bool isReducible() const;
  JTBlock &getBlock(BasicBlock &BB);
  JTBlock &getEntryBlock();
  JTBlock &getFirstConditionBlock();
  JTBlocks &getConditionBlocks(const std::string &MergePath);
  JTBlock &getTargetBlock();
  const std::string &getMergePath() const;
  void addBlock(JTBlock &Block, const bool InsertAtFront = false);
  void finalizeBlock(JTBlock &Block);
  void gatherVariables(Value &Node);
  void determineReducibility();
  /// \brief  Shrink threadable graph by removal of unreachable blocks.
  void shrink(const bool ErrorOnShrinking);
  void prepareMergeInfo();
  void getReachableBlocks(JTBlocks &Blocks);

  void dumpBlocks() const;
  void dumpMergeInfo() const;
  void printDot(const std::string &Path) const;
  void printEdge(
    raw_fd_ostream &Out,
    const JTEdge &Edge,
    const bool IsSuccessor,
    const std::string &VariablesStr = "") const;
  static const std::string toString(const Value &Condition);

private:
  /// Merge info map consists of string which represents tree path and index of destination blocks.
  typedef std::map<std::string, JTBlocks> MergeInfoMap;

  void getReachableBlocks(JTBlocks &Blocks, JTBlock &Block);

  /// Basic blocks of the graph.
  JTBlocks Blocks;
  /// LLVM BB => JT BB.
  BlockMap BlockMapping;
  /// Variables present in conditions
  VariableSet Variables;
  /// Reducible graph, it is valid only for source graph before attaching
  bool Reducible;

  // Merge information is used only by threadable graphs
  /// Merge info
  MergeInfoMap MergeInfo;
  /// Merge path, for graph 'A->B->C->D' merge path is 'A->B->C'
  /// and it means when going through merge path 'A->B->C', then go unconditionally through 'C->D'
  std::string MergePath;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline JTGraph::JTBlocks &JTGraph::getBlocks()
{
  return Blocks;
}

inline const JTGraph::JTBlocks &JTGraph::getBlocks() const
{
  return Blocks;
}

inline JTGraph::BlockMap &JTGraph::getBlockMapping()
{
  return BlockMapping;
}

inline bool JTGraph::isReducible() const
{
  return Reducible;
}

inline JTBlock &JTGraph::getBlock(BasicBlock &BB)
{
  //dbgs() << "JTGraph::getBlock " << BB.getName() << "\n";
  auto it = BlockMapping.find(&BB);
  assert(it != BlockMapping.end());
  return *it->second;
}

inline const std::string &JTGraph::getMergePath() const
{
  return MergePath;
}

inline JTBlock &JTGraph::getEntryBlock()
{
  assert(!getBlocks().empty() && "Missing entry block.");
  return *getBlocks().back();
}

inline JTBlock &JTGraph::getFirstConditionBlock()
{
  assert(getBlocks().size() >= 2 && "Missing first condition block.");
  return *getBlocks()[getBlocks().size() - 2];
}

inline JTGraph::JTBlocks &JTGraph::getConditionBlocks(const std::string &MergePath)
{
  // Empty condition blocks
  static JTBlocks EmptyMergeInfo;
  auto it = MergeInfo.find(MergePath);
  return it != MergeInfo.end() ? it->second : EmptyMergeInfo;
}

inline JTBlock &JTGraph::getTargetBlock()
{
  assert(!getBlocks().empty() && "Missing target block.");
  JTBlock &ConditionBlock = *getBlocks().front();
  assert(ConditionBlock.getSuccessors().size() == 1 && "Expected one target block.");
  return ConditionBlock.getSuccessor(0).getOutBlock();
}

inline void JTGraph::addBlock(JTBlock &Block, const bool InsertAtFront)
{
  if (InsertAtFront)
    Blocks.insert(Blocks.begin(), &Block);
  else
    Blocks.push_back(&Block);
  finalizeBlock(Block);
}

}   // namespace llvm::jumpthreading
}   // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTGRAPH_H_
