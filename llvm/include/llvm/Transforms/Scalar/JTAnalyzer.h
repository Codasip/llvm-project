//===- JTAnalyzer.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_JTANALYZER_H_
#define INCLUDE_CODASIP_JUMPTHREADING_JTANALYZER_H_

#include <cstddef>
#include <map>
#include <vector>

#include "llvm/ADT/APInt.h"
#include "llvm/IR/JTFwd.h"
#include "IrreducibilityChecker.h"

namespace llvm {

// Forward declaration.
class BasicBlock;
class BinaryOperator;
class CastInst;
class CmpInst;
class Instruction;
class LoopInfo;
class TargetLibraryInfo;
class Value;

namespace jumpthreading {

/**
 *  \brief  This class is used to find threadable graphs of given graph.
 */
class JTAnalyzer
{
public:
  /// Threadable graphs
  typedef std::vector<JTGraph*> ThreadableGraphs;

  /**
   *  \brief  Finds all threadable graphs that could be joined into the original graph.
   *  \param[out] Graphs Resulting graphs
   *  \param[in, out] Changed LLVM code is changed (during condition constant folding)
   *  \param[in]  G Original graph
   *  \param[in]  Builder JT IR Builder
   *  \param[in]  TLI Target library info
   */
  static void run(
    ThreadableGraphs &Graphs,
    bool &Changed,
    JTGraph &G,
    JTBuilder &Builder,
    const LoopInfo &LI,
    const TargetLibraryInfo &TLI);
  /**
   *  \brief  Try to shrink given graphs and filter out duplicates.
   *  \param[in, out] Graphs
   *  \param[in]  ErrorOnShrinking Ends with an assert when this is true
   */
  static void shrinkAndFilterDuplicates(
    JTAnalyzer::ThreadableGraphs &Graphs, const bool ErrorOnShrinking);

//private:
  /// Threadable flow
  typedef std::vector<JTThreadableNode*> ThreadablePath;
  /// Threadable flows
  typedef std::vector<ThreadablePath> ThreadablePaths;
  /// Map used during conversion between JTGraphs and irreducibility::Graph
  typedef std::map<const JTBlock*, irreducibility::Vertex*> IrreducibilityMap;

  JTAnalyzer(
    ThreadableGraphs &Graphs,
    bool &Changed,
    JTGraph &G,
    JTBuilder &Builder,
    const LoopInfo &LI,
    const TargetLibraryInfo &TLI);

  void findThreadableGraphs();
  void findThreadableGraphsConditional(JTBlock &Block);
  void findThreadableGraphsSwitch(JTBlock &Block);
  JTThreadableNode *findEvaluablePaths(
    ThreadablePaths &EvaluablePaths, Value *&Condition, JTBlock &Block);
  void findTree(JTThreadableNode *&Node, Value &V, JTBlock &Last, const size_t Cost);
  void fillBlocks(JTThreadableNode &NewNode, size_t &Cost, JTBlock &Block, JTBlock &Last);
  void findTreeBinary(JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost);
  void findTreeTernary(JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost);
  void findTreeUnary(JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost);
  void updateTemporaries(JTBlock *&Last, JTBlock *&Block, size_t &Cost);
  void gatherPaths(ThreadablePaths &Paths, JTThreadableNode &Node);
  void filterUnknownPaths(ThreadablePaths &Output, ThreadablePaths &Input);
  void foldCondition(Value *&Condition, BasicBlock &Block);
  static APInt evaluatePath(const ThreadablePath &Path);
  static bool evaluatePathAsBoolean(const ThreadablePath &Path);
  static void evaluateValue(APInt &Res, const Value &V);
  static APInt evaluateBinary(const BinaryOperator &I, const APInt &A, const APInt &B);
  static APInt evaluateCast(const CastInst &I, const APInt &A);
  static APInt evaluateCompare(const CmpInst &I, const APInt &A, const APInt &B);
  static bool isReducible(const JTGraph &G, const JTGraph &Attachment);
  static void convertGraph(irreducibility::Graph &IG, IrreducibilityMap &Mapping, const JTGraph &G);
  static irreducibility::Vertex &convertBlock(
    irreducibility::Graph &IG, IrreducibilityMap &Mapping, const JTBlock &B);

  /// Found threadable graphs
  ThreadableGraphs &Graphs;
  /// Is function changed?
  bool &Changed;
  /// Graph
  JTGraph &G;
  /// JT IR Builder
  JTBuilder &Builder;
  /// Loop Info
  const LoopInfo &LI;
  /// Target Library Info
  const TargetLibraryInfo &TLI;

  /// Initial tree count
  size_t InitTreeCount;
  /// Thread tree count
  size_t ThreadTreeCount;
};

} // namespace llvm::jumpthreading
} // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_JTANALYZER_H_
