//===-- JTGraph.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTGraph.h"
#include "llvm/Transforms/Scalar/JTAnalyzer.h"

using namespace llvm;
using namespace jumpthreading;

JTGraph::~JTGraph()
{
  // Edges for removal
  std::set<JTEdge*> Removal;

  // Store edges for removal and delete blocks
  for (JTBlock *B : getBlocks()) {
    for (JTEdge *E : B->getPredecessors())
      Removal.insert(E);
    for (JTEdge *E : B->getSuccessors())
      Removal.insert(E);
    delete B;
  }

  // Delete edges
  for (JTEdge *E : Removal)
    delete E;
}

void JTGraph::finalizeBlock(JTBlock &Block)
{
  // following code is allowed only for blocks with reference on LLVM IR set
  if (!Block.hasReference())
    return;

  BlockMapping.insert(std::make_pair(&Block.getReference(), &Block));

  if (Block.isConditional())
    gatherVariables(Block.getCondition());
}

void JTGraph::gatherVariables(Value &Node)
{
  if (PHINode *Phi = dyn_cast<PHINode>(&Node))
    Variables.insert(Phi->getName().str());
  else if (Instruction *I = dyn_cast<Instruction>(&Node))
    for (size_t i = 0; i < I->getNumOperands(); i++)
      gatherVariables(*I->getOperand(i));
}

void JTGraph::determineReducibility()
{
  // do not bother with this now
  Reducible = false;
  return;

  irreducibility::Graph IG;
  JTAnalyzer::IrreducibilityMap Mapping;
  JTAnalyzer::convertGraph(IG, Mapping, *this);
  Reducible = IG.isReducible();
}

void JTGraph::shrink(const bool ErrorOnShrinking)
{
  for (auto RIt = getBlocks().rbegin(); RIt != getBlocks().rend(); ++RIt) {
    auto RNextIt = RIt;
    ++RNextIt;

    // End shrinking
    if (RNextIt == getBlocks().rend())
      break;

    JTBlock &Current = **RIt;
    JTBlock &Next = **RNextIt;

    if (!Next.isSuccessorOf(Current)) {
      assert(!ErrorOnShrinking && "Shrinking masked the problem.");
      for (RIt = RNextIt; RIt != getBlocks().rend(); ++RIt) {
        // Delete successor edges of deleted block
        JTBlock *RemovedBlock = *RIt;
        std::set<JTEdge*> Removal;
        for (JTEdge *E : RemovedBlock->getSuccessors())
          Removal.insert(E);
        for (JTEdge *E : Removal)
          delete E;
        // Delete block itself
        delete RemovedBlock;
        *RIt = nullptr;
      }
      auto It = getBlocks().begin();
      assert(*It == nullptr && "Expected null.");
      assert(It != getBlocks().end() && "Unexpected vector end.");
      while (*It == nullptr) {
        ++It;
        assert(It != getBlocks().end() && "Unexpected vector end.");
      }
      assert(*It && "Unexpected null.");
      getBlocks().erase(getBlocks().begin(), It);
      break;
    }
  }
}

void JTGraph::prepareMergeInfo()
{
  assert(Blocks.size() >= 1 && "There should be at least one basic block.");

  // Reset
  MergeInfo.clear();
  MergePath.clear();

  for (size_t Initial = Blocks.size() - 1; Initial > 0; Initial--) {
    std::string CurrentMergePath;
    for (size_t i = Initial; i > 0; i--) {
      JTBlock &B = *Blocks[i];
      // Add delimiter if needed
      if (!CurrentMergePath.empty())
        CurrentMergePath += "*";
      CurrentMergePath += B.getName();

      // Store merge info
      auto it = MergeInfo.find(CurrentMergePath);
      if (it == MergeInfo.end()) {
        MergeInfo.insert(std::make_pair(CurrentMergePath, JTBlocks()));
        it = MergeInfo.find(CurrentMergePath);
      }
      assert(it != MergeInfo.end());
      it->second.push_back(&B);
    }

    // Merge path is created only for the whole tree
    if (Initial == Blocks.size() - 1) {
      // Store merge path
      JTBlock &B = *Blocks[0];
      // Add delimiter if needed
      if (!CurrentMergePath.empty())
        CurrentMergePath += "*";
      CurrentMergePath += B.getName();
      MergePath = CurrentMergePath;
    }
  }

  // Special case for one block, previous code is not done even once
  if (Blocks.size() == 1)
    MergePath = Blocks.front()->getName().str();

  //dumpMergeInfo();
}

void JTGraph::getReachableBlocks(JTBlocks &Blocks)
{
  assert(!getBlocks().empty() && "Blocks are empty.");
  getReachableBlocks(Blocks, *getBlocks().front());
}

void JTGraph::dumpBlocks() const
{
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  for (auto &pair : BlockMapping)
    pair.first->dump();
#endif
}

void JTGraph::dumpMergeInfo() const
{
  LLVM_DEBUG(dbgs() << "Merge info\n");
  for (auto &Pair : MergeInfo) {
    LLVM_DEBUG(dbgs() << "'" << Pair.first << "' => { ");
    for (auto it = Pair.second.begin(); it != Pair.second.end(); ++it) {
      if (it != Pair.second.begin())
        LLVM_DEBUG(dbgs() << ", ");
      LLVM_DEBUG(dbgs() << (*it)->getName());
    }
    LLVM_DEBUG(dbgs() << " }\n");
  }
  LLVM_DEBUG(dbgs() << "MergePath '" << MergePath << "'\n");
}

void JTGraph::printDot(const std::string &Path) const
{
  //dbgs() << "printDot start " << Path << "\n";

  std::error_code EC;
  raw_fd_ostream Out(Path, EC, sys::fs::OpenFlags::F_None);
  assert(!EC);
  Out << "digraph G {\n";

  std::set<JTBlock*> BlocksToPrint;

  // Print control flow edges
  for (JTBlock *Block : Blocks) {
    BlocksToPrint.insert(Block);
    for (JTEdge *Edge : Block->getSuccessors()) {
      // Remember blocks for later printing
      BlocksToPrint.insert(&Edge->getOutBlock());

      // Prepare label for edge
      std::string VariablesStr;
      if (!Block->isDuplicate() && // TODO try to fix for duplicates
        Edge->getInBlock().hasReference() && Edge->getOutBlock().hasReference())
        for (Instruction &I : Edge->getOutBlock().getReference())
          if (PHINode *Phi = dyn_cast<PHINode>(&I))
            if (Variables.count(Phi->getName().str())) {
              Value *V = Phi->getIncomingValueForBlock(&Edge->getInBlock().getReference());
              if (ConstantInt *C = dyn_cast<ConstantInt>(V)) {
                if (!VariablesStr.empty())
                  VariablesStr += "\n";
                VariablesStr += (std::string)Phi->getName() + " = " + C->getValue().toString(16, true);
              }
            }

      printEdge(Out, *Edge, true, VariablesStr);
    }

    for (JTEdge *Edge : Block->getPredecessors()) {
      printEdge(Out, *Edge, false);
    }
  }

  // Print stored blocks
  for (JTBlock *Block : BlocksToPrint) {
    // Prepare block color
    std::string Color = "";
    if (Block->isDuplicate())
      Color = ", color=\"green\"";

    // Print block itself
    Out << "\"" << Block->getId() << "\" [label=\"" << Block->getName() << "\\nId: " << Block->getId() << "\"" << Color << "]\n";
  }

  Out << "}\n";
  Out.close();

  //dbgs() << "printDot end\n";
}

void JTGraph::printEdge(
  raw_fd_ostream &Out,
  const JTEdge &Edge,
  const bool IsSuccessor,
  const std::string &VariablesStr) const
{
  /*dbgs() << "Printing " << (IsSuccessor ? "successor" : "predecessor") << " edge ";
  Edge.dump();
  dbgs() << "\n";*/

  const std::string &FromId = IsSuccessor ? Edge.getInBlock().getId() : Edge.getOutBlock().getId();
  const std::string &ToId = IsSuccessor ? Edge.getOutBlock().getId() : Edge.getInBlock().getId();
  const std::string Style = IsSuccessor ? "" : " style=dashed, color=gray";

  // Print edge itself
  if (Edge.isConditional()) {
    Value &Condition = Edge.getInBlock().getCondition();
    std::string ConditionStr = toString(Condition);
    if (!Edge.getCondition())
      ConditionStr = "!(" + ConditionStr + ")";
    Out << "\"" << FromId << "\" -> \"" << ToId
        << "\" [label=\"[" << VariablesStr << ConditionStr << "]\"" << Style << "]\n";
  } else if (Edge.isUnconditional())
    Out << "\"" << FromId << "\" -> \"" << ToId
      << "\" [label=\"" + VariablesStr + "\"" << Style << "]\n";
  else if (Edge.isSwitch()) {
    const std::string ConditionStr = Edge.getCondition() ? "" : "!";
    Out << "\"" << FromId << "\" -> \"" << ToId << "\" [label=\"[" << ConditionStr << "][";
    if (Edge.getCondition())
      Out << *Edge.getSwitchValue();
    else
      Out << "-";
    Out << "]\"" << Style << "]\n";
  } else if (Edge.isIndirect()) {
    const Value &Address = Edge.getInBlock().getAddressValue();
    const std::string AddressStr = toString(Address);
    Out << "\"" << FromId << "\" -> \"" << ToId
        << "\" [label=\"[" << VariablesStr << AddressStr << "][" << Edge.getAddress() << "]\"" << Style << "]\n";
  } else
    assert(false && "Unsupported edge type.");
}

const std::string JTGraph::toString(const Value &Condition)
{
  if (const ConstantInt *V = dyn_cast<ConstantInt>(&Condition)) {
    return V->getValue().toString(16, true);
  } else if (const PHINode *Phi = dyn_cast<PHINode>(&Condition)) {
    return Phi->getName().str();
  } else if (const CmpInst *Compare = dyn_cast<CmpInst>(&Condition)) {
    std::string OpcodeStr;
    switch (Compare->getPredicate()) {
      case CmpInst::ICMP_EQ: OpcodeStr = "=="; break;
      case CmpInst::ICMP_NE: OpcodeStr = "!="; break;
      case CmpInst::ICMP_UGT: OpcodeStr = ">"; break;
      case CmpInst::ICMP_UGE: OpcodeStr = ">="; break;
      case CmpInst::ICMP_ULT: OpcodeStr = "<"; break;
      case CmpInst::ICMP_ULE: OpcodeStr = "<="; break;
      case CmpInst::ICMP_SGT: OpcodeStr = ">"; break;
      case CmpInst::ICMP_SGE: OpcodeStr = ">="; break;
      case CmpInst::ICMP_SLT: OpcodeStr = "<"; break;
      case CmpInst::ICMP_SLE: OpcodeStr = "<="; break;
      default: OpcodeStr = "???"; break;
    }
    assert(Compare->getOperand(0));
    assert(Compare->getOperand(1));
    return toString(*Compare->getOperand(0)) + " " + OpcodeStr + " " +
        toString(*Compare->getOperand(1));
  } else if (const BinaryOperator *I = dyn_cast<BinaryOperator>(&Condition)) {
    std::string OpcodeStr;
    switch (I->getOpcode()) {
      case Instruction::Add: OpcodeStr = "+"; break;
      case Instruction::Sub: OpcodeStr = "-"; break;
      case Instruction::Mul: OpcodeStr = "*"; break;
      case Instruction::UDiv: OpcodeStr = "/(udiv)"; break;
      case Instruction::SDiv: OpcodeStr = "/(sdiv)"; break;
      case Instruction::URem: OpcodeStr = "/(urem)"; break;
      case Instruction::SRem: OpcodeStr = "/(srem)"; break;
      // Logical operators (integer operands)
      case Instruction::Shl: OpcodeStr = "<<"; break;
      case Instruction::LShr: OpcodeStr = ">>(logical)"; break;
      case Instruction::AShr: OpcodeStr = ">>(arithmetic)"; break;
      case Instruction::And: OpcodeStr = "&"; break;
      case Instruction::Or: OpcodeStr = "|"; break;
      case Instruction::Xor: OpcodeStr = "^"; break;
      default: OpcodeStr = "???"; break;
    }
    assert(I->getOperand(0));
    assert(I->getOperand(1));
    return toString(*I->getOperand(0)) + " " + OpcodeStr + " " + toString(*I->getOperand(1));
  }
  return "???";
}

void JTGraph::getReachableBlocks(JTBlocks &Blocks, JTBlock &Block)
{
  // Visited.
  if (Block.IsReachable())
    return;

  Blocks.push_back(&Block);
  Block.setReachable(true);
  for (JTEdge *Edge : Block.getSuccessors())
    getReachableBlocks(Blocks, Edge->getOutBlock());
}
