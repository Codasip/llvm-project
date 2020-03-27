//===-- JTAnalyzer.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/JTBuilder.h"
#include "llvm/IR/JTGraph.h"
#include "llvm/IR/JTThreadableNode.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar/JTAnalyzer.h"
#include "llvm/Transforms/Scalar/JTDebug.h"
#include "llvm/Transforms/Utils/Local.h"

// FIXME do not work
STATISTIC(NumFolds,   "Number of terminators folded");

using namespace llvm;
using namespace jumpthreading;

static cl::opt<unsigned>
JTCostThreshold("codasip-jump-threading-cost-threshold",
  cl::desc("Max instructions to duplicate for jump threading"),
  cl::init(30), cl::Hidden);

void JTAnalyzer::run(
  ThreadableGraphs &Graphs,
  bool &Changed,
  JTGraph &G,
  JTBuilder &Builder,
  const LoopInfo &LI,
  const TargetLibraryInfo &TLI)
{
  JTAnalyzer Analyzer(Graphs, Changed, G, Builder, LI, TLI);
  Analyzer.findThreadableGraphs();
}

void JTAnalyzer::shrinkAndFilterDuplicates(ThreadableGraphs &Graphs, const bool ErrorOnShrinking)
{
  LLVM_DEBUG(dbgs() << "CodasipJumpThreading::shrinkAndFilterDuplicates(" << Graphs.size() << ")\n");

  ThreadableGraphs Input;
  Input.swap(Graphs);

  std::set<std::string> MergePaths;
  for (JTGraph *G : Input) {
    G->shrink(ErrorOnShrinking);
    G->prepareMergeInfo();
    if (MergePaths.find(G->getMergePath()) == MergePaths.end()) {
      Graphs.push_back(G);
      MergePaths.insert(G->getMergePath());
    } else
      delete G;
  }
}

JTAnalyzer::JTAnalyzer(
  ThreadableGraphs &Graphs,
  bool &Changed,
  JTGraph &G,
  JTBuilder &Builder,
  const LoopInfo &LI,
  const TargetLibraryInfo &TLI)
: Graphs(Graphs),
  Changed(Changed),
  G(G),
  Builder(Builder),
  LI(LI),
  TLI(TLI),
  InitTreeCount(0),
  ThreadTreeCount(0)
{}

/// \brief  Main method to find threadable graphs.
void JTAnalyzer::findThreadableGraphs()
{
  LLVM_DEBUG(dbgs() << "findThreadableGraphs\n");

  for (JTBlock *Block : G.getBlocks())
    if (Block->isConditional())
      findThreadableGraphsConditional(*Block);
    else if (Block->isSwitch())
      findThreadableGraphsSwitch(*Block);

  for (JTGraph *Graph : Graphs)
    JTDebug::printDot(*Graph, "thread_tree_" + std::to_string(ThreadTreeCount++) + ".dot");

  shrinkAndFilterDuplicates(Graphs, true);

  // Do not solve reducibility at all
  return;

  // Remove threadable graphs that lead to irreducible CFG
  dbgs() << "With irreducible graphs " << Graphs.size() << "\n";

  // Reducible analysis and filtering is valid only for reducible graphs
  if (!G.isReducible())
    return;

  ThreadableGraphs Input;
  Input.swap(Graphs);
  ThreadableGraphs Removal;
  for (JTGraph *Attachment : Input) {
    // Is graph irreducible
    /*bool IsIrreducible = false;
    for (JTBlock *B : Attachment->getBlocks())
      if (B != &Attachment->getEntryBlock() && LI.isLoopHeader(&B->getOriginal()->getReference())) {
        IsIrreducible = true;
        break;
      }*/

    if (isReducible(G, *Attachment))
      Graphs.push_back(Attachment);
    else
      Removal.push_back(Attachment);
  }

  for (JTGraph *G : Removal)
    delete G;

  dbgs() << "Without irreducible graphs " << Graphs.size() << "\n";
}

/**
 *  \brief  Find threadable graphs in block with conditional jump.
 *  \param[in]  Block
 */
void JTAnalyzer::findThreadableGraphsConditional(JTBlock &Block)
{
  ThreadablePaths EvaluablePaths;
  Value *Condition = &Block.getCondition();
  JTThreadableNode *Tree = findEvaluablePaths(EvaluablePaths, Condition, Block);
  if (!Tree)
    return;

  // Evaluate paths and creates threadable path just from blocks
  for (ThreadablePath &Path : EvaluablePaths) {
    JTGraph &Graph = Builder.CreateGraph();

    JTBlock *LastCopy = nullptr;
    for (JTThreadableNode *Node : Path)
      for (JTBlock *B : Node->getBlocks()) {
        LLVM_DEBUG(dbgs() << "Copying: " << B->getName() << "\n");
        // Copy block
        JTBlock &Copy = Builder.CreateBlock(*B);
        // Copy edges
        for (JTEdge *E : B->getSuccessors()) {
          const bool UseCopy = (LastCopy && LastCopy->getOriginal() == &E->getOutBlock());
          JTBlock &OutBlock = UseCopy ? *LastCopy : E->getOutBlock();
          JTEdge::CreateCopy(*E, Copy, OutBlock, UseCopy);
        }
        LastCopy = &Copy;
        Graph.addBlock(Copy);
      }

    // Reconnect last block unconditionally, depends on evaluation of condition
    const bool Result = evaluatePathAsBoolean(Path);
    JTBlock *ConditionBlock = Graph.getBlocks().front();
    assert(ConditionBlock->getSuccessors().size() == 2 && "Expected 2 successors.");
    JTEdge &First = ConditionBlock->getSuccessor(0);
    JTEdge &Second = ConditionBlock->getSuccessor(1);
    assert((First.getCondition() ^ Second.getCondition()) &&
      "Just one of the successors should be true.");

    if (Result) {
      if (First.getCondition()) {
        First.setType(JTEdge::UNCONDITIONAL);
        Second.deleteEdge();
      } else {
        Second.setType(JTEdge::UNCONDITIONAL);
        First.deleteEdge();
      }
    }
    else {
      if (!First.getCondition()) {
        First.setType(JTEdge::UNCONDITIONAL);
        Second.deleteEdge();
      } else {
        Second.setType(JTEdge::UNCONDITIONAL);
        First.deleteEdge();
      }
    }

    Graphs.push_back(&Graph);
  }

  // Free initial tree because it is no longer needed
  delete Tree;
}

/**
 *  \brief  Find threadable graphs in block with switch.
 *  \param[in]  Block
 */
void JTAnalyzer::findThreadableGraphsSwitch(JTBlock &Block)
{
  ThreadablePaths EvaluablePaths;
  Value *Condition = Block.getSwitch().getCondition();
  JTThreadableNode *Tree = findEvaluablePaths(EvaluablePaths, Condition, Block);
  if (!Tree)
    return;

  // Evaluate paths and creates threadable path just from blocks
  for (ThreadablePath &Path : EvaluablePaths) {
    JTGraph &Graph = Builder.CreateGraph();

    JTBlock *LastCopy = nullptr;
    for (JTThreadableNode *Node : Path)
      for (JTBlock *B : Node->getBlocks()) {
        LLVM_DEBUG(dbgs() << "Copying: " << B->getName() << "\n");
        // Copy block
        JTBlock &Copy = Builder.CreateBlock(*B);
        // Copy edges
        for (JTEdge *E : B->getSuccessors()) {
          const bool UseCopy = (LastCopy && LastCopy->getOriginal() == &E->getOutBlock());
          JTBlock &OutBlock = UseCopy ? *LastCopy : E->getOutBlock();
          JTEdge::CreateCopy(*E, Copy, OutBlock, UseCopy);
        }
        LastCopy = &Copy;
        Graph.addBlock(Copy);
      }

    // Reconnect last block unconditionally, depends on evaluation of condition
    const APInt Result = evaluatePath(Path);
    JTBlock &ConditionBlock = *Graph.getBlocks().front();

    JTEdge *ChosenEdge = nullptr;
    for (JTEdge *Edge : ConditionBlock.getSuccessors()) {
      assert(Edge->isSwitch() && "Expected edge of switch type.");
      // Default case
      if (Edge->isDefault()) {
        // Set at least default to have something in the end, but continue in search for case
        if (ChosenEdge == nullptr)
          ChosenEdge = Edge;
      } else {
        // Case
        if (Edge->getSwitchValue()->getValue() == Result) {
          ChosenEdge = Edge;
          break;
        }
      }
    }
    assert(ChosenEdge && "Switch type edge not found.");

    JTBlock::Edges Successors = ConditionBlock.getSuccessors();
    for (JTEdge *Edge : Successors)
      if (Edge == ChosenEdge)
        Edge->setType(JTEdge::UNCONDITIONAL);
      else
        Edge->deleteEdge();

    Graphs.push_back(&Graph);
  }

  // Free initial tree because it is no longer needed
  delete Tree;
}

/**
 *  \brief  Find evaluable paths of given condition of given block.
 *  \param[out] EvaluablePaths Resulting evaluable graphs
 *  \param[in, out] Condition Condition value, could be optimized before analysis
 *  \param[in]  Block
 */
JTThreadableNode *JTAnalyzer::findEvaluablePaths(
  ThreadablePaths &EvaluablePaths, Value *&Condition, JTBlock &Block)
{
  EvaluablePaths.clear();

  // Run constant folding to see if we can reduce the condition to a simple constant.
  foldCondition(Condition, Block.getReference());
  // Find blindly all paths
  JTThreadableNode *Tree = nullptr;
  findTree(Tree, *Condition, Block, Block.getCost());
  // TODO urgent check that condition has same block as jump if has block
  // Set first block
  // Unsupported expression in condition
  if (!Tree)
    return nullptr;
  Tree->addBlock(Block, false);
  JTDebug::printDot(*Tree, "init_tree_" + std::to_string(InitTreeCount++) + ".dot");

  // Gather all paths
  ThreadablePaths TreePaths;
  gatherPaths(TreePaths, *Tree);
  assert(!TreePaths.empty());

  // Filter out paths that does not end with ConstantInt
  filterUnknownPaths(EvaluablePaths, TreePaths);
  // No evaluable paths found
  if (EvaluablePaths.empty()) {
    // Free initial tree because it is no longer needed
    delete Tree;
    return nullptr;
  }

  /*dbgs() << "Having paths\n";
  for (const ThreadablePath &Path : EvaluablePaths) {
    bool first = true;
    for (const ThreadableNode *Node : Path) {
      if (first)
        first = false;
      else
        dbgs() << " => ";
      dbgs() << Node->getNodeName();
    }
    dbgs() << "\n";
  }*/

  return Tree;
}

/**
 *  \brief  Find tree of given Value.
 *  \param[in, out] Node Resulting threadable node tree
 *  \param[in]  V Examined Value
 *  \param[in]  Last Lastly attached block
 *  \param[in]  Cost Cost of all attached blocks till now
 */
void JTAnalyzer::findTree(JTThreadableNode *&Node, Value &V, JTBlock &Last, const size_t Cost)
{
  LLVM_DEBUG(
    V.dump();
    dbgs() << "Cost '" << Cost << "'\n");

  // End search when cost is too high
  if (Cost > JTCostThreshold)
    return;

  JTThreadableNode *NewNode = nullptr;

  if (isa<ConstantInt>(&V)) {
    NewNode = &Builder.CreateThreadableNode(V);
    NewNode->setCost(Cost);
  /*} else if (isa<UndefValue>(&V)) {
    NewNode = &Builder.CreateThreadableNode(V); TODO?
    NewNode->setCost(Cost);*/
  } else if (PHINode *Phi = dyn_cast<PHINode>(&V)) {
    assert(Phi->getParent());
    JTBlock *NewLast = &Last;
    JTBlock *Block = &G.getBlock(*Phi->getParent());
    size_t NewCost = Cost;
    updateTemporaries(NewLast, Block, NewCost);
    NewNode = &Builder.CreateThreadableNode(V);
    assert(NewNode && "Failed allocation of threadable node.");
    if (Block) {
      LLVM_DEBUG(dbgs() << "Fill blocks in PHI\n");
      fillBlocks(*NewNode, NewCost, *Block, Last);
      NewNode->addBlock(*Block);
    }
    NewNode->setCost(NewCost);
    for (unsigned i = 0; i < Phi->getNumIncomingValues(); i++) {
      Value *PhiV = Phi->getIncomingValue(i);
      BasicBlock *BB = Phi->getIncomingBlock(i);
      assert(PhiV);
      assert(BB);
      JTBlock &Block = G.getBlock(*BB);
      size_t NewCostChild = NewCost;
      if (!isa<ConstantInt>(PhiV))
        NewCostChild += Block.getCost();
      assert(Block.isPredecessorOf(*NewLast));
      // Previous block is reseted when walk through PHI node
      const size_t OldChildCount = NewNode->getChildren().size();
      findTree(NewNode, *PhiV, Block, NewCostChild);
      // Repair child blocks if able
      if (NewNode->getChildren().size() != OldChildCount) {
        JTThreadableNode *Child = NewNode->getChildren().back();
        if (Child->getBlocks().empty() || Child->getBlocks().front() != &Block) {
          Child->addBlock(Block, false);
        }
      }
    }
  } else if (BinaryOperator *I = dyn_cast<BinaryOperator>(&V))
    findTreeBinary(NewNode, *I, Last, Cost);
  else if (CastInst *I = dyn_cast<CastInst>(&V)) {
    // Only basic conversions are supported now
    if (I->getOpcode() == CastInst::Trunc ||
      I->getOpcode() == CastInst::ZExt ||
      I->getOpcode() == CastInst::SExt)
      findTreeUnary(NewNode, *I, Last, Cost);
  } else if (CmpInst *I = dyn_cast<CmpInst>(&V))
    findTreeBinary(NewNode, *I, Last, Cost);
  else if (SelectInst *I = dyn_cast<SelectInst>(&V))
    findTreeTernary(NewNode, *I, Last, Cost);
  /*else if (isa<Argument>(&V) || isa<CallInst>(&V) || isa<LoadInst>(&V) || isa<StoreInst>(&V)) {
    // Nothing to do for these instructions
  } else {
    V.dump();
    assert(false && "Missing support for instruction.");
  }*/

  if (NewNode) {
    if (Node)
      Node->addChild(NewNode); // child
    else
      Node = NewNode; // root
  }
}

/**
 *  \brief  Fill blocks to NewNode that are visited between Block and Last blocks.
 *  \param[in, out] NewNode Resulting threadable node tree
 *  \param[in, out]  Cost Cost of all attached blocks till now
 *  \param[in]  Block Currently attached block
 *  \param[in]  Last Lastly attached block
 */
void JTAnalyzer::fillBlocks(JTThreadableNode &NewNode, size_t &Cost, JTBlock &Block, JTBlock &Last)
{
  LLVM_DEBUG(dbgs() << "fillBlocks(" << NewNode.getValue().getName() << ", " << Cost
    << ", " << Block.getName() << ", " << Last.getName() << ")\n");

  // There is nothing to do.
  if (Block.isPredecessorOf(Last))
    return;

  // Unreachable block, analysis could not continue, set cost over max to stop this path.
  if (Last.getPredecessors().empty()) {
    Cost = JTCostThreshold + 1;
    return;
  }

  // TODO could not be handled by current implementation, set cost over max to stop this path.
  if (Last.getPredecessors().size() > 1) {
    Cost = JTCostThreshold + 1;
    return;
  }

  assert(Last.getPredecessors().size() == 1 && "Expected one predecessor.");
  JTEdge &Edge = Last.getPredecessor(0);
  JTBlock &Predecessor = Edge.getInBlock();
  LLVM_DEBUG(dbgs() << "Filling " << Predecessor.getName() << "\n");
  NewNode.addBlock(Predecessor);
  Cost += Predecessor.getCost();
  fillBlocks(NewNode, Cost, Block, Predecessor);
}

void JTAnalyzer::findTreeBinary(
  JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost)
{
  LLVM_DEBUG(dbgs() << "findTreeBinary\n");

  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  const bool IsFirstConstant = isa<ConstantInt>(Op0);
  const bool IsSecondConstant = isa<ConstantInt>(Op1);
  if (IsFirstConstant || IsSecondConstant) {
    // It is an issue when they are both constants
    assert(!IsFirstConstant || !IsSecondConstant);
    assert(I.getParent());
    JTBlock *NewLast = &Last;
    JTBlock *Block = &G.getBlock(*I.getParent());
    size_t NewCost = Cost;
    updateTemporaries(NewLast, Block, NewCost);
    NewNode = &Builder.CreateThreadableNode(I);
    if (Block) {
      LLVM_DEBUG(dbgs() << "Fill blocks in binary\n");
      fillBlocks(*NewNode, NewCost, *Block, Last);
      NewNode->addBlock(*Block);
    }
    NewNode->setCost(NewCost);
    if (IsSecondConstant)
      findTree(NewNode, *Op0, *NewLast, NewCost);
    else if (IsFirstConstant)
      findTree(NewNode, *Op1, *NewLast, NewCost);
  }
}

void JTAnalyzer::findTreeTernary(
  JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost)
{
  LLVM_DEBUG(dbgs() << "findTreeTernary\n");

  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *Op2 = I.getOperand(2);
  const bool IsFirstConstant = isa<ConstantInt>(Op0);
  const bool IsSecondConstant = isa<ConstantInt>(Op1);
  const bool IsThirdConstant = isa<ConstantInt>(Op2);
  const size_t ConstantCount = IsFirstConstant + IsSecondConstant + IsThirdConstant;
  if (ConstantCount >= 2) {
    // It is an issue when they are all constants
    assert(!IsFirstConstant || !IsSecondConstant || !IsThirdConstant);
    assert(I.getParent());
    JTBlock *NewLast = &Last;
    JTBlock *Block = &G.getBlock(*I.getParent());
    size_t NewCost = Cost;
    updateTemporaries(NewLast, Block, NewCost);
    NewNode = &Builder.CreateThreadableNode(I);
    if (Block) {
      LLVM_DEBUG(dbgs() << "Fill blocks in ternary\n");
      fillBlocks(*NewNode, NewCost, *Block, Last);
      NewNode->addBlock(*Block);
    }
    NewNode->setCost(NewCost);
    if (IsSecondConstant || IsThirdConstant)
      findTree(NewNode, *Op0, *NewLast, NewCost);
    else if (IsFirstConstant || IsThirdConstant)
      findTree(NewNode, *Op1, *NewLast, NewCost);
    else if (IsFirstConstant || IsSecondConstant)
      findTree(NewNode, *Op2, *NewLast, NewCost);
  }
}

void JTAnalyzer::findTreeUnary(
  JTThreadableNode *&NewNode, Instruction &I, JTBlock &Last, const size_t Cost)
{
  LLVM_DEBUG(dbgs() << "findTreeUnary\n");

  Value *Op0 = I.getOperand(0);
  assert(I.getParent());
  JTBlock *NewLast = &Last;
  JTBlock *Block = &G.getBlock(*I.getParent());
  size_t NewCost = Cost;
  updateTemporaries(NewLast, Block, NewCost);
  NewNode = &Builder.CreateThreadableNode(I);
  if (Block) {
    LLVM_DEBUG(dbgs() << "Fill blocks in unary\n");
    fillBlocks(*NewNode, NewCost, *Block, Last);
    NewNode->addBlock(*Block);
  }
  NewNode->setCost(NewCost);
  findTree(NewNode, *Op0, *NewLast, NewCost);
}

/**
 *  \brief  Update Last, Block and Cost, update depends on equality of Last and Block blocks.
 *  \param[in, out] Last
 *  \param[in, out] Block
 *  \param[in, out] Cost
 */
void JTAnalyzer::updateTemporaries(JTBlock *&Last, JTBlock *&Block, size_t &Cost)
{
  assert(Block);

  LLVM_DEBUG(dbgs() << "Update " << Block->getName() << " into ");

  // No change in threadable path from blocks view
  if (Last && Block == Last) {
    Block = nullptr;
    LLVM_DEBUG(dbgs() << "nullptr\n");
    return;
  }

  Last = Block;
  Cost += Block->getCost();

  LLVM_DEBUG(dbgs() << Block->getName() << "\n");
}

/**
 *  \brief  Enumarate all threadable paths of thredable node tree.
 *  \param[in, out] Paths Enumerated threadable paths
 *  \param[in]  Node threadable node tree
 */
void JTAnalyzer::gatherPaths(ThreadablePaths &Paths, JTThreadableNode &Node)
{
  // list node
  if (Node.getChildren().empty()) {
    Paths.push_back(ThreadablePath());
    Paths.back().push_back(&Node);
    return;
  }

  for (JTThreadableNode *Child : Node.getChildren()) {
    ThreadablePaths ChildPaths;
    gatherPaths(ChildPaths, *Child);
    for (ThreadablePath &ChildPath : ChildPaths) {
      Paths.push_back(ThreadablePath());
      Paths.back().push_back(&Node);
      Paths.back().insert(Paths.back().end(), ChildPath.begin(), ChildPath.end());
    }
  }
}

/**
 *  \brief  Transfers paths that ends with constant integer from Input to Output.
 *  \param[out] Output
 *  \param[in]  Input
 */
void JTAnalyzer::filterUnknownPaths(ThreadablePaths &Output, ThreadablePaths &Input)
{
  for (ThreadablePath &Path : Input)
    if (isa<ConstantInt>(Path.back()->getValue()))
      Output.push_back(Path);
}

/**
 *  \brief  Performs constant folding on given Condition.
 *  \param[in ,out] Condition Examined condition
 *  \param[in]  Block
 */
void JTAnalyzer::foldCondition(Value *&Condition, BasicBlock &Block)
{
  LLVM_DEBUG(dbgs() << "foldCondition\n");

  if (Instruction *I = dyn_cast<Instruction>(Condition)) {
    Value *SimpleVal = ConstantFoldInstruction(I, Block.getModule()->getDataLayout(), &TLI);
    if (SimpleVal) {
      I->replaceAllUsesWith(SimpleVal);
      if (isInstructionTriviallyDead(I, &TLI))
        I->eraseFromParent();
      Condition = SimpleVal;
      Changed = true;
      // Update statistics
      NumFolds++;
    }
  }
}

/**
 *  \brief  Evaluates given path and returns boolean result.
 *  \param[in]  Path
 */
APInt JTAnalyzer::evaluatePath(const ThreadablePath &Path)
{
  APInt Res;
  for (auto rit = Path.rbegin(); rit != Path.rend(); ++rit)
    evaluateValue(Res, (*rit)->getValue());
  return Res;
}

/**
 *  \brief  Evaluates given path and returns boolean result.
 *  \param[in]  Path
 */
bool JTAnalyzer::evaluatePathAsBoolean(const ThreadablePath &Path)
{
  return evaluatePath(Path).getBoolValue();
}

/**
 *  \brief  Evaluates given value. Res is used as input first, in the end it is rewritten.
 *  \param[in, out] Res
 *  \param[in]  V
 */
void JTAnalyzer::evaluateValue(APInt &Res, const Value &V)
{
  LLVM_DEBUG(V.dump());
  if (const ConstantInt *C = dyn_cast<ConstantInt>(&V))
    Res = C->getValue();
  /*else if (const UndefValue *U = dyn_cast<UndefValue>(&V))
    Res = U->getIntegerValue()*/
  else if (const BinaryOperator *I = dyn_cast<BinaryOperator>(&V)) {
    if (const ConstantInt *Op0 = dyn_cast<ConstantInt>(I->getOperand(0)))
      Res = evaluateBinary(*I, Op0->getValue(), Res);
    else if (const ConstantInt *Op1 = dyn_cast<ConstantInt>(I->getOperand(1)))
      Res = evaluateBinary(*I, Res, Op1->getValue());
    else
      assert(false && "Unsupported evaluation of binary instruction.");
  } else if (const CastInst *I = dyn_cast<CastInst>(&V))
    Res = evaluateCast(*I, Res);
  else if (const CmpInst *I = dyn_cast<CmpInst>(&V)) {
    if (const ConstantInt *Op0 = dyn_cast<ConstantInt>(I->getOperand(0)))
      Res = evaluateCompare(*I, Op0->getValue(), Res);
    else if (const ConstantInt *Op1 = dyn_cast<ConstantInt>(I->getOperand(1)))
      Res = evaluateCompare(*I, Res, Op1->getValue());
    else
      assert(false && "Unsupported evaluation of compare instruction.");
  } else if (isa<PHINode>(&V)) {
    // do nothing
  } else if (const SelectInst *I = dyn_cast<SelectInst>(&V)) {
    const ConstantInt *Op0 = dyn_cast<ConstantInt>(I->getOperand(0));
    const ConstantInt *Op1 = dyn_cast<ConstantInt>(I->getOperand(1));
    const ConstantInt *Op2 = dyn_cast<ConstantInt>(I->getOperand(2));
    if (Op1 && Op2)
      Res = Res.getBoolValue() ? Op1->getValue() : Op2->getValue();
    else if (Op0 && Op2)
      Res = Op0->getValue().getBoolValue() ? Res : Op2->getValue();
    else if (Op1 && Op2)
      Res = Op0->getValue().getBoolValue() ? Op1->getValue() : Res;
    else
      assert(false && "Unsupported evaluation of select instruction.");
  } else
    assert(false && "Unsupported evaluation of value.");
}

/**
 *  \brief  Evaluates expression A I.opcode B and returns result.
 *  \param[in]  I binary instruction
 *  \param[in]  A first operand
 *  \param[in]  B second operand
 */
APInt JTAnalyzer::evaluateBinary(const BinaryOperator &I, const APInt &A, const APInt &B)
{
  switch (I.getOpcode()) {
    case Instruction::Add: return A + B;
    case Instruction::Sub: return A - B;
    case Instruction::Mul: return A * B;
    case Instruction::UDiv: return A.udiv(B);
    case Instruction::SDiv: return A.sdiv(B);
    case Instruction::URem: return A.urem(B);
    case Instruction::SRem: return A.srem(B);
    // Logical operators (integer operands)
    case Instruction::Shl: return A.shl(B);
    case Instruction::LShr: return A.lshr(B);
    case Instruction::AShr: return A.ashr(B);
    case Instruction::And: return A & B;
    case Instruction::Or:  return A | B;
    case Instruction::Xor: return A ^ B;
    default:
      assert(false && "Unsupported evaluation of binary opcode.");
      return APInt();
  }
}

/**
 *  \brief  Evaluates expression I.opcode A and returns result.
 *  \param[in]  I cast instruction
 *  \param[in]  A first operand
 */
APInt JTAnalyzer::evaluateCast(const CastInst &I, const APInt &A)
{
  assert(I.getDestTy());

  switch (I.getOpcode()) {
    case CastInst::Trunc: return A.trunc(I.getDestTy()->getScalarSizeInBits());
    case CastInst::ZExt: return A.zext(I.getDestTy()->getScalarSizeInBits());
    case CastInst::SExt: return A.zext(I.getDestTy()->getScalarSizeInBits());
    default:
      assert(false && "Unsupported evaluation of cast opcode.");
      return APInt();
  }
}

/**
 *  \brief  Evaluates expression A I.opcode B and returns result.
 *  \param[in]  I compare instruction
 *  \param[in]  A first operand
 *  \param[in]  B second operand
 */
APInt JTAnalyzer::evaluateCompare(const CmpInst &I, const APInt &A, const APInt &B)
{
  switch (I.getPredicate()) {
    case CmpInst::ICMP_EQ:  return APInt(1, A.eq(B));
    case CmpInst::ICMP_NE:  return APInt(1, A.ne(B));
    case CmpInst::ICMP_UGT: return APInt(1, A.ugt(B));
    case CmpInst::ICMP_UGE: return APInt(1, A.uge(B));
    case CmpInst::ICMP_ULT: return APInt(1, A.ult(B));
    case CmpInst::ICMP_ULE: return APInt(1, A.ule(B));
    case CmpInst::ICMP_SGT: return APInt(1, A.sgt(B));
    case CmpInst::ICMP_SGE: return APInt(1, A.sge(B));
    case CmpInst::ICMP_SLT: return APInt(1, A.slt(B));
    case CmpInst::ICMP_SLE: return APInt(1, A.sle(B));
    default:
      assert(false && "Unsupported evaluation of compare predicate.");
      return APInt();
  }
}

/**
 *  \brief  Determines if graph G with Attachment is reducible.
 *  \param[in]  G
 *  \param[in]  Attachment
 */
bool JTAnalyzer::isReducible(const JTGraph &G, const JTGraph &Attachment)
{
  irreducibility::Graph IG;
  IrreducibilityMap Mapping;
  convertGraph(IG, Mapping, G);
  convertGraph(IG, Mapping, Attachment);
  return IG.isReducible();
}

/**
 *  \brief  Converts JTGraph into irreducibility::Graph with usage of given Mapping.
 *  \param[in, out] IG Graph that is used for testing irreducibility of original JTGraph
 *  \param[in, out] Mapping maps JTBlocks into Vertices
 *  \param[in]  G original JTGraph
 */
void JTAnalyzer::convertGraph(
  irreducibility::Graph &IG, IrreducibilityMap &Mapping, const JTGraph &G)
{
  // TODO Entry block should be handled specially, but according to libfirm example leaving it as is
  for (JTBlock *B : G.getBlocks())
    convertBlock(IG, Mapping, *B);
}

/**
 *  \brief  Converts JTBlock into irreducibility::Graph with usage of given Mapping.
 *  \param[in, out] IG Graph that is used for testing irreducibility of original JTGraph
 *  \param[in, out] Mapping maps JTBlocks into Vertices
 *  \param[in]  B original JTBlock
 *  \return Returns created Vertex
 */
irreducibility::Vertex &JTAnalyzer::convertBlock(
  irreducibility::Graph &IG, IrreducibilityMap &Mapping, const JTBlock &B)
{
  auto it = Mapping.find(&B);

  // Block was already processed
  if (it != Mapping.end())
    return *it->second;

  // Create block itself
  irreducibility::Vertex *V = new irreducibility::Vertex();
  assert(V && "Allocation of Vertex failed.");
  V->addId(Mapping.size());
  IG.addVertex(*V);
  Mapping.insert(std::make_pair(&B, V));

  // Process block successors
  for (const JTEdge *Edge : B.getSuccessors()) {
    irreducibility::Vertex &S = convertBlock(IG, Mapping, Edge->getOutBlock());
    irreducibility::Edge *E = new irreducibility::Edge(*V, S);
    assert(E && "Allocation of Edge failed.");
  }

  return *V;
}
