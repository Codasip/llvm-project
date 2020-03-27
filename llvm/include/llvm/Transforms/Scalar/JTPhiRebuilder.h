//===- JTPhiRebuilder.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_JTPHIREBUILDER_H_
#define INCLUDE_CODASIP_JUMPTHREADING_JTPHIREBUILDER_H_

#include <map>
#include <set>

#include "llvm/IR/JTGraph.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {

// Forward declaration.
class Instruction;
class Value;

namespace jumpthreading {

// Forward declaration.
class JTBuilder;

/**
 *  \brief  This class computes dominating definitions to fix old PHI nodes
 *          and creates new ones when needed.
 */
class JTPhiRebuilder
{
public:
  /**
   *  \brief  Rebuilds PHI nodes and creates new ones when needed.
   *  \param[in, out] Blocks Reachable blocks, only them could be processed
   *  \param[in, out] G Graph
   *  \param[in, out] Builder JT IR Builder
   *  \param[in]  Duplicate2Original Mapping from duplicate to original instruction
   */
  static void run(
    JTGraph::JTBlocks &Blocks,
    JTGraph &G,
    JTBuilder &Builder,
    ValueToValueMapTy &Duplicate2Original);

private:
  /// Dominating def cache
  typedef std::map<JTBlock*, Value*> DDCache;
  /// Node -> Dominating def cache
  typedef std::map<Value*, DDCache*> DominatingDefs;
  /// Set to determine whether LLVM Value was visited already
  typedef std::set<Value*> VisitedValues;

  JTPhiRebuilder(JTGraph &G, JTBuilder &Builder, ValueToValueMapTy &Duplicate2Original);

  ~JTPhiRebuilder();

  void operator()(JTGraph::JTBlocks &Blocks);
  void rebuildInstruction(Instruction &I, const size_t Indent);
  static size_t getPositionFromBack(Instruction &I);
  Value *getDominatingDefinition(
    Instruction &I,
    const size_t StartPositionBack,
    JTBlock *Block,
    DDCache *DominatingDefCache,
    const size_t Indent);
  Instruction &getOriginal(Instruction &I);
  DDCache *getDDCache(Value &V);
  Value *getCachedDominatingDefinition(JTBlock &Block, DDCache &Cache);
  bool isVisitedElseMark(Value &V);

  /// Processed graph
  JTGraph &G;
  /// JT IR Builder
  JTBuilder &Builder;
  /// Initialized by createDuplicateMap
  ValueToValueMapTy &Duplicate2Original;
  /// Dominating definition caches
  DominatingDefs DDCaches;
  /// Determines whether LLVM Value is visited by PhiRebuilder
  VisitedValues Visited;
};

} // namespace llvm::jumpthreading
} // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_JTPHIREBUILDER_H_

