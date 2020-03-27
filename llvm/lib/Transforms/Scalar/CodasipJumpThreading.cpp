//===-- CodasipJumpThreading.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/InitializePasses.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/JTBuilder.h"
#include "llvm/IR/JTGraph.h"
#include "llvm/IR/JTThreadableNode.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/CodasipJumpThreading.h"
#include "llvm/Transforms/Scalar/JTAnalyzer.h"
#include "llvm/Transforms/Scalar/JTDebug.h"
#include "llvm/Transforms/Scalar/JTPhiRebuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;
using namespace jumpthreading;

STATISTIC(NumThreads, "Number of jumps threaded");
STATISTIC(NumDupes,   "Number of branch blocks duplicated to eliminate phi");

namespace {

/// \brief  Copied from LLVM.
static bool eliminateUnreachableBlock(Function &F) {
  df_iterator_default_set<BasicBlock*> Reachable;

  // Mark all reachable blocks.
  for (BasicBlock *BB : depth_first_ext(&F, Reachable))
    (void)BB/* Mark all reachable blocks */;

  // Loop over all dead blocks, remembering them and deleting all instructions
  // in them.
  std::vector<BasicBlock*> DeadBlocks;
  for (Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
    if (!Reachable.count(&*I)) {
      BasicBlock *BB = &*I;
      DeadBlocks.push_back(BB);
      while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
        PN->replaceAllUsesWith(Constant::getNullValue(PN->getType()));
        BB->getInstList().pop_front();
      }
      for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI) {
        // Codasip patch, remove predecessor of each PHI if it is present there
        std::vector<PHINode*> Phis;
        for (Instruction &I : **SI)
          if (PHINode *Phi = dyn_cast<PHINode>(&I))
            Phis.push_back(Phi);
        for (PHINode *Phi : Phis) {
          const int Index = Phi->getBasicBlockIndex(BB); 
          if (Index != -1)
            Phi->removeIncomingValue(Index, true);
        }

        // Old code
        //(*SI)->removePredecessor(BB, true);
      }
      BB->dropAllReferences();
    }

  // Actually remove the blocks now.
  for (unsigned i = 0, e = DeadBlocks.size(); i != e; ++i) {
    DeadBlocks[i]->eraseFromParent();
  }

  return !DeadBlocks.empty();
}

class CodasipJumpThreading : public FunctionPass {
  CodasipJumpThreadingPass Impl;

public:
  static char ID; // Pass identification

  CodasipJumpThreading(int T = -1) : FunctionPass(ID), Impl() {
    initializeCodasipJumpThreadingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    //AU.addRequired<LazyValueInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }

private:
  void createJTGraph(JTGraph &G, Function &F, JTBuilder &Builder);
  // Merging functions
  bool mergeThreadableGraphs(JTAnalyzer::ThreadableGraphs &Graphs, const unsigned int Iteration);
  static bool merge(JTGraph &Dst, JTGraph &Src);
  static void mergeSameEntryEdgeGraphs(JTAnalyzer::ThreadableGraphs &Graphs);
  static bool mergeSameEntryEdgeGraphs(JTGraph &Dst, JTGraph &Src);
  // Attaching functions
  void attachGraphs(JTGraph &Dst, JTAnalyzer::ThreadableGraphs &Src, bool &Changed);
  //static bool hasIntersection(ThreadableGraphs &Src);
  void attachGraph(JTGraph &Dst, JTGraph &Src, bool &Changed);
  // LLVM final processing functions
  static void cloneBlocks(JTGraph::JTBlocks &Blocks, JTGraph &G, JTBuilder &Builder);
  void fixTerminators(JTGraph::JTBlocks &Blocks, Function &F);
  void fixConditionalBranch(Instruction *Terminator, JTBlock &Block);
  void fixSwitch(Instruction *Terminator, JTBlock &Block);
  void placeDuplicatedBlocksIntoFunction(Function &F, JTGraph::JTBlocks &Blocks);

  // Utility functions
  void createDuplicateMap(ValueToValueMapTy &Duplicate2Original, JTBlock &Duplicate);
};

} // end anonymous namespace

char CodasipJumpThreading::ID = 0;

INITIALIZE_PASS_BEGIN(CodasipJumpThreading, "codasip-jump-threading",
                "Codasip Jump Threading", false, false)
//INITIALIZE_PASS_DEPENDENCY(LazyValueInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(CodasipJumpThreading, "codasip-jump-threading",
                "Codasip Jump Threading", false, false)

// Public interface to the Jump Threading pass
FunctionPass *llvm::createCodasipJumpThreadingPass(int Threshold) {
  return new CodasipJumpThreading(Threshold);
}

CodasipJumpThreadingPass::CodasipJumpThreadingPass()
  : TLI(nullptr),
    LVI(nullptr)
{}

/// runOnFunction - Toplevel algorithm.
bool CodasipJumpThreading::runOnFunction(Function &F)
{
  bool Changed = false;
  if (skipFunction(F))
    return Changed;

  LLVM_DEBUG(dbgs()
    << "Running my pass(" << JTDebug::getPassCount() << ") 'CodasipJumpThreading' on function '"
    << F.getName() << "' ("
    << F.getBasicBlockList().size() << ").\n"
    << F);

  // Initialize JT builder
  JTBuilder Builder;

  // Create overlay graph, it stores additional information for JT
  JTGraph G;
  createJTGraph(G, F, Builder);
  G.determineReducibility();
  // Investigated that LLVM sometimes produces irreducible graphs itself
  //assert(IG.isReducible() && "Source graph is not reducible.");

  // Find threadable graphs.
  JTAnalyzer::ThreadableGraphs Graphs;
  const LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  const TargetLibraryInfo &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  JTAnalyzer::run(Graphs, Changed, G, Builder, LI, TLI);
  unsigned int Iteration = 0;
  while (mergeThreadableGraphs(Graphs, Iteration))
    Iteration++;

  // Return when there are no threadable graphs
  if (Graphs.empty()) {
    LLVM_DEBUG(dbgs() << "No threadable graphs found.\n");
    JTDebug::incrementPassCount();
    return Changed;
  }

  mergeSameEntryEdgeGraphs(Graphs);

  attachGraphs(G, Graphs, Changed);

  JTDebug::printDot(G, "attached.dot");

  // Free graphs
  for (JTGraph *G : Graphs)
    delete G;

  // Following code is done only on reachable blocks
  JTGraph::JTBlocks Blocks;
  G.getReachableBlocks(Blocks);

  // Remove unreachable blocks from predecessors, it breaks getPredDominatingDefs phase otherwise
  for (JTBlock *Block : G.getBlocks()) {
    JTBlock::Edges Predecessors = Block->getPredecessors();
    for (JTEdge *Edge : Predecessors)
      if (!Edge->getInBlock().IsReachable())
        Edge->deleteEdge();
  }

  // Generate duplicated LLVM blocks
  cloneBlocks(Blocks, G, Builder);

  // Initialized by createDuplicateMap
  ValueToValueMapTy Duplicate2Original;

  // Repair block with given information
  // Loop over all of the instructions in this funclet, fixing up operand
  // references as we go.  This uses VMap to do all the hard work.
  for (JTBlock *Block : Blocks) {
    // Loop over all instructions, fixing each one as we find it...
    for (Instruction &I : Block->getReference())
      RemapInstruction(&I, Block->getOriginal2Duplicate(),
        RF_IgnoreMissingLocals | RF_NoModuleLevelChanges);

    // Create global duplicate to original mapping
    if (Block->isDuplicate())
      createDuplicateMap(Duplicate2Original, *Block);
  }

  fixTerminators(Blocks, F);

  LLVM_DEBUG(G.dumpBlocks());

  JTPhiRebuilder::run(Blocks, G, Builder, Duplicate2Original);

  LLVM_DEBUG(dbgs() << "Dominating definitions\n");
  LLVM_DEBUG(G.dumpBlocks());

  // Place duplicated basic blocks into function
  placeDuplicatedBlocksIntoFunction(F, Blocks);

  LLVM_DEBUG(dbgs() << "Duplicated blocks in function\n");
  JTDebug::printDot(G, "finish.dot");
  LLVM_DEBUG(dbgs() << F);

  // Delete dead PHI instructions
  // Fails when duplicated blocks are not placed into function
  for (BasicBlock &Block : F)
    Changed |= DeleteDeadPHIs(&Block);

  // Cleanup blocks, should run after delete of dead PHIs because it breaks it somehow
  LLVM_DEBUG(dbgs() << "Delete unreachable blocks.\n");
  Changed |= eliminateUnreachableBlock(F);
  LLVM_DEBUG(dbgs() << F);

  // Remove unreachable PHIs,
  // all PHI instructions that are not referenced by another instruction should be removed
  // Should be called second time
  for (BasicBlock &Block : F)
    Changed |= DeleteDeadPHIs(&Block);

  LLVM_DEBUG(dbgs() << "Finish\n"
    << F);

  JTDebug::incrementPassCount();
  return Changed;
}

void CodasipJumpThreading::createJTGraph(JTGraph &G, Function &F, JTBuilder &Builder)
{
  LLVM_DEBUG(dbgs() << "createJTGraph\n");

  // create basic blocks
  for (BasicBlock &BB : F) {
    JTBlock &Block = Builder.CreateBlock(BB);
    G.addBlock(Block);
  }

  // create predecessors
  for (JTBlock *Block : G.getBlocks()) {
    Instruction *Terminator = Block->getReference().getTerminator();
    if (BranchInst *Branch = dyn_cast<BranchInst>(Terminator)) {
      if (Branch->isConditional()) {
        // if branch
        JTBlock &IfSuccessor = G.getBlock(*Terminator->getSuccessor(0));
        JTEdge::CreateConditional(*Block, IfSuccessor, true);
        // else branch
        JTBlock &ElseSuccessor = G.getBlock(*Terminator->getSuccessor(1));
        JTEdge::CreateConditional(*Block, ElseSuccessor, false);
      } else {
        // unconditional branch
        JTBlock &Successor = G.getBlock(*Terminator->getSuccessor(0));
        JTEdge::CreateUnconditional(*Block, Successor);
      }
    } else if (SwitchInst *Switch = dyn_cast<SwitchInst>(Terminator)) {
      for (SwitchInst::CaseHandle &Case : Switch->cases()) {
        // case branch
        JTBlock &Successor = G.getBlock(*Case.getCaseSuccessor());
        JTEdge::CreateSwitch(*Block, Successor, false, Case.getCaseValue());
      }
      // default branch
      JTBlock &DefaultSuccessor = G.getBlock(*Switch->getDefaultDest());
      JTEdge::CreateSwitch(*Block, DefaultSuccessor, true);
    } else if (IndirectBrInst *IndirectJump = dyn_cast<IndirectBrInst>(Terminator)) {
      for (size_t i = 0; i < IndirectJump->getNumDestinations(); i++) {
        JTBlock &Successor = G.getBlock(*IndirectJump->getDestination(i));
        JTEdge::CreateIndirect(*Block, Successor, i);
      }
    } else if (isa<ReturnInst>(Terminator)) {
      // nothing to do for return instruction
    } else if (isa<UnreachableInst>(Terminator)) {
      // nothing to do for unreachable instruction
    } else
      assert(false && "Unsupported terminator.");
  }

  JTDebug::printDot(G, "graph.dot");
}

/**
 *  \brief  Merge provided graphs together.
 *  \param[in, out] Graphs
 *  \param[in]  Iteration
 */
bool CodasipJumpThreading::mergeThreadableGraphs(
  JTAnalyzer::ThreadableGraphs &Graphs, const unsigned int Iteration)
{
  LLVM_DEBUG(dbgs() << "mergeThreadableGraphs(" << Graphs.size() << ", " << Iteration << ")\n");

  JTAnalyzer::ThreadableGraphs Input;
  Input.swap(Graphs);

  // Flags about removal of particular input
  std::vector<bool> Removed(Input.size(), false);

  for (size_t i = 0; i < Input.size(); i++) {
    // Skip removed graphs
    if (Removed[i])
      continue;

    JTGraph *GI = Input[i];

    // Merge graph GI into all other graphs
    bool IsMerged = false;
    for (size_t j = 0; j < Input.size(); j++) {
      // Skip removed graphs
      if (Removed[j])
        continue;

      // Skip same graph
      if (i == j)
        continue;

      JTGraph *GJ = Input[j];

      // Merge graph GI into graph GJ
      if (merge(*GJ, *GI))
        IsMerged = true;
    }

    // Mark as removed when it is merged into any other graph
    if (IsMerged) {
      delete Input[i];
      Removed[i] = true;
    }
  }

  // Copy remaining graphs back
  bool Merged = false;
  for (size_t i = 0; i < Input.size(); i++) {
    if (!Removed[i])
      Graphs.push_back(Input[i]);
    else
      Merged = true;
  }

  size_t TreeCount = 0;
  for (JTGraph *Graph : Graphs)
    JTDebug::printDot(*Graph,
      "merged_tree_" + std::to_string(Iteration) + "_"+ std::to_string(TreeCount++) + ".dot");

  JTAnalyzer::shrinkAndFilterDuplicates(Graphs, false);

  TreeCount = 0;
  for (JTGraph *Graph : Graphs)
    JTDebug::printDot(*Graph,
      "merged_tree_filtered_" + std::to_string(Iteration) + "_" + std::to_string(TreeCount++) + ".dot");

  return Merged;
}

/**
 *  \brief  Merge graph Src to graph Dst.
 *  \param[in, out] Dst
 *  \param[in]  Src
 */
bool CodasipJumpThreading::merge(JTGraph &Dst, JTGraph &Src)
{
  /*LLVM_DEBUG(dbgs()
    << "merging " << Src.getMergePath() << "\n"
    << " inside " << Dst.getMergePath() << "\n");*/

  // TODO urgent refactor with find*switch function
  JTGraph::JTBlocks &ConditionBlocks = Dst.getConditionBlocks(Src.getMergePath());
  // Now it is at least part of another graph
  for (JTBlock *ConditionBlock : ConditionBlocks) {
    // Needs merge
    if (ConditionBlock->getSuccessors().size() > 1) {
      JTEdge *ChosenEdge = nullptr;
      for (JTEdge *Edge : ConditionBlock->getSuccessors()) {
        if (Edge->getOutBlock().getName() == Src.getTargetBlock().getName()) {
          ChosenEdge = Edge;
          break;
        }
      }
      assert(ChosenEdge && "Failed to determine edge to keep after merge.");

      JTBlock::Edges Successors = ConditionBlock->getSuccessors();
      for (JTEdge *Edge : Successors)
        if (Edge == ChosenEdge)
          Edge->setType(JTEdge::UNCONDITIONAL);
        else
          Edge->deleteEdge();
    }
  }

  return !ConditionBlocks.empty();
}

void CodasipJumpThreading::mergeSameEntryEdgeGraphs(JTAnalyzer::ThreadableGraphs &Graphs)
{
  for (size_t i = 0; i < Graphs.size(); i++)
    for (size_t j = i + 1; j < Graphs.size(); j++)
      if (mergeSameEntryEdgeGraphs(*Graphs[i], *Graphs[j])) {
        LLVM_DEBUG(dbgs() << "Merged " << j << " into " << i << "\n");
        // Remove graph j with all his remains
        delete Graphs[j];
        Graphs.erase(Graphs.begin() + j);
        j--;
      }

  size_t TreeCount = 0;
  for (JTGraph *G : Graphs)
    JTDebug::printDot(*G, "merged_tree_final_" + std::to_string(TreeCount++) + ".dot");
}

bool CodasipJumpThreading::mergeSameEntryEdgeGraphs(JTGraph &Dst, JTGraph &Src)
{
  // Graphs needs to have at least two blocks to be mergeable this way
  // TODO Increase power to suffice with one?
  if (Dst.getBlocks().size() < 2 || Src.getBlocks().size() < 2)
    return false;

  // Pair of entry and first condition nodes has to be the same in both graphs
  // TODO Share the same edge?
  JTBlock &DstEntry = Dst.getEntryBlock();
  JTBlock &DstNext = Dst.getFirstConditionBlock();
  JTBlock &SrcEntry = Src.getEntryBlock();
  JTBlock &SrcNext = Src.getFirstConditionBlock();
  if (DstEntry.getOriginal() != SrcEntry.getOriginal() ||
    DstNext.getOriginal() != SrcNext.getOriginal())
    return false;

  // Find blocks, where both graphs start to differ
  bool Found = false;
  size_t DstIndex = Dst.getBlocks().size() - 1;
  size_t SrcIndex = Src.getBlocks().size() - 1;
  while (!Found) {
    JTBlock &DstBlock = *Dst.getBlocks()[DstIndex];
    JTBlock &SrcBlock = *Src.getBlocks()[SrcIndex];
    // Found the desired blocks
    if (DstBlock.getOriginal() != SrcBlock.getOriginal()) {
      Found = true;
      break;
    }
    // Stop the searching now
    if (DstIndex == 0 || SrcIndex == 0)
      break;
    // Go to next pair of blocks
    DstIndex--;
    SrcIndex--;
  }
  assert(Found && "Failed merging of same entry edge graphs.");

  const size_t DstLastSharedIndex = DstIndex + 1;
  const size_t SrcLastSharedIndex = SrcIndex + 1;
  assert(DstLastSharedIndex < Dst.getBlocks().size());
  assert(SrcLastSharedIndex < Src.getBlocks().size());

  // Find the edge in Dst last shared block that leads to Src first different block
  JTBlock &DstLastShared = *Dst.getBlocks()[DstLastSharedIndex];
  JTBlock &DstFirstDifferent = *Dst.getBlocks()[DstIndex];
  JTBlock &SrcLastShared = *Src.getBlocks()[SrcLastSharedIndex];
  JTBlock &SrcFirstDifferent = *Src.getBlocks()[SrcIndex];
  LLVM_DEBUG(dbgs() << "LS DST: " << DstLastShared.getName() << " SRC: " << SrcLastShared.getName()
    << "\nFD DST: " << DstFirstDifferent.getName() << " SRC: " << SrcFirstDifferent.getName() << "\n");
  (void)DstFirstDifferent;
  //JTEdge *FoundEdge = nullptr;
  for (JTEdge *E : DstLastShared.getSuccessors())
    if (E->getOutBlock().getOriginal(true) == SrcFirstDifferent.getOriginal()) {
      DstLastShared.reroute(SrcFirstDifferent, E->getOutBlock());
      // There should be only one edge found
      //assert(FoundEdge == nullptr && "Found multiple edges for merge of same entry edge graphs.");
      //FoundEdge = E;
    }
  //assert(FoundEdge && "Missing edge for merge of same entry edge graphs.");

  //DstLastShared.reroute(SrcFirstDifferent, FoundEdge->getOutBlock());

  bool DeletionPhase = true;
  for (auto RIt = Src.getBlocks().rbegin(); RIt != Src.getBlocks().rend(); ++RIt) {
    JTBlock *Block = *RIt;
    if (DeletionPhase) {
      // Src last shared is already processed by reroute function
      if (Block == &SrcLastShared) {
        DeletionPhase = false;
      }
      JTBlock::Edges Removal = Block->getPredecessors();
      for (JTEdge *E : Removal)
        E->deleteEdge();
      Removal = Block->getSuccessors();
      for (JTEdge *E : Removal)
        E->deleteEdge();
      delete Block;
    } else
      // Needs to be inserted at the front because it could otherwise break merging phase
      // Entry is expected to be the last block in graph etc...
      // TODO what if merging by some bad luck goes to merged blocks?
      Dst.addBlock(*Block, true);
  }

  // Remove all blocks from graph, they are already deleted or moved to Dst
  Src.getBlocks().clear();

  return true;
}

void CodasipJumpThreading::attachGraphs(
  JTGraph &Dst, JTAnalyzer::ThreadableGraphs &Src, bool &Changed)
{
  // Expected max one graph or graphs without intersection now
  // TODO is this safe? if (Src.size() <= 1 || !hasIntersection(Src))
  // Latest forbids only graphs that have same entry and first condition node
  std::set<std::string> Forbidden;
  for (JTGraph *G : Src) {
    JTBlock &Entry = G->getEntryBlock();
    std::string ForbiddenStr = Entry.getName().str();
    if (G->getBlocks().size() > 1) {
      JTBlock &Next = G->getFirstConditionBlock();
      ForbiddenStr += "*" + (std::string)Next.getName();
    }
    
    // Could not be used because of some previous graph
    if (Forbidden.find(ForbiddenStr) != Forbidden.end()) {
      dbgs() << "Forbidden graph :(\n";
      continue;
    }
    Forbidden.insert(ForbiddenStr);

    attachGraph(Dst, *G, Changed);
  }
}

/*bool CodasipJumpThreading::hasIntersection(ThreadableGraphs &Src)
{
  dbgs() << "hasIntersection\n";

  // Stores all the nodes of the previous graphs
  std::set<std::string> Previous;
  // Nodes of the current graph
  std::set<std::string> Current;

  for (JTGraph *G : Src) {
    for (JTBlock *Block : G->getBlocks())
      if (Previous.find(Block->getLlvmBlock().getName()) != Previous.end()) {
        dbgs() << "Has intersection '" << Block->getLlvmBlock().getName() << "'\n";
        assert(false);
        return true;
      }
      else
        Current.insert(Block->getLlvmBlock().getName());
    Previous.insert(Current.begin(), Current.end());
    Current.clear();
  }

  return false;
}*/

void CodasipJumpThreading::attachGraph(JTGraph &Dst, JTGraph &Src, bool &Changed)
{
  LLVM_DEBUG(dbgs() << "attachGraph\n");

  JTBlock &EntryDuplicate = Src.getEntryBlock();
  JTBlock *Entry = EntryDuplicate.getOriginal();
  assert(Entry && "Entry block does not have original block set.");
  assert(Src.getBlocks().size() >= 1 && "There should be at least one basic block.");

  // Special case for one basic block. It means that this block always continues unconditionally.
  if (Src.getBlocks().size() == 1) {
    LLVM_DEBUG(dbgs() << "Attaching one node graph.\n");

    assert(EntryDuplicate.getSuccessors().size() == 1 && "Expecting one successor.");
    JTEdge &NextEdge = EntryDuplicate.getSuccessor(0);
    JTBlock &NextBlock = NextEdge.getOutBlock();

    LLVM_DEBUG(dbgs() << EntryDuplicate.getName() << " => " << NextBlock.getName() << "\n");

    // Gather useless edges
    bool Found = false; // Remaining successor is found.
    std::vector<JTEdge*> Removal;
    for (JTEdge *Edge : Entry->getSuccessors()) {
      LLVM_DEBUG(dbgs() << "Inspecting edge " << Edge->getInBlock().getName() << " => " << Edge->getOutBlock().getName() << "\n");
      if (&Edge->getOutBlock() != &NextBlock)
        Removal.push_back(Edge);
      else if (Edge->isSwitch() && !(Edge->isDefault() == NextEdge.isDefault() &&
        (Edge->isDefault() ||
          (Edge->getSwitchValue()->getValue() == NextEdge.getSwitchValue()->getValue()))))
        Removal.push_back(Edge);
      else
        Found = true;
    }

    assert(Found && "Expecting existing successor.");
    (void)Found;

    // Remove useless edges
    for (JTEdge *Edge : Removal)
      Edge->deleteEdge();

    // Change remaining edge type to unconditional branch
    if (Entry->getSuccessors().size() == 1)
      Entry->getSuccessor(0).setType(JTEdge::UNCONDITIONAL);

    Changed = true;
    return;
  }

  JTBlock *NextBlock = Src.getBlocks()[Src.getBlocks().size() - 2];
  Entry->reroute(*NextBlock, *NextBlock->getOriginal());

  for (JTBlock *Block : Src.getBlocks()) {
    // Entry is already processed by reroute function
    if (Block == &Src.getEntryBlock())
      continue;

    // Complete predecessor edges of non-duplicate successor nodes
    // of attached nodes to attached nodes
    for (JTEdge *Edge : Block->getSuccessors())
      if (!Edge->getOutBlock().isDuplicate())
        Edge->getOutBlock().addPredecessor(*Edge);

    // Move non-entry nodes into Dst
    Dst.addBlock(*Block);
  }

  // Free successor edges of entry block of Src
  JTBlock::Edges Removal = Src.getEntryBlock().getSuccessors();
  for (JTEdge *E : Removal)
    E->deleteEdge();
  // Free entry block of Src
  delete &Src.getEntryBlock();

  // Remove all moved blocks from Src
  Src.getBlocks().clear();

  Changed = true;
}

/**
 *  \brief  Clone blocks.
 *  \param[in, out] Blocks
 *  \param[in, out] G
 *  \param[in, out] Builder
 */
void CodasipJumpThreading::cloneBlocks(JTGraph::JTBlocks &Blocks, JTGraph &G, JTBuilder &Builder)
{
  for (JTBlock *B : Blocks)
    if (B->isDuplicate()) {
      B->cloneBlock();
      G.finalizeBlock(*B);
      Builder.updateDuplicateName(*B);
      // Set original block as duplicated
      B->getOriginal()->setDuplicated(true);
      // Update statistics
      NumDupes++;
    }
}

void CodasipJumpThreading::fixTerminators(JTGraph::JTBlocks &Blocks, Function &F)
{
  for (JTBlock *Block : Blocks) {
    Instruction* Terminator = Block->getReference().getTerminator();
    LLVM_DEBUG(dbgs() << "Block.getSuccessors().size(): " << Block->getSuccessors().size() << "\n"
      << "Block: " << Block->getName() << " " << *Terminator << "\n");
    if (Block->getSuccessors().size() == 0) {
      // Replace jump with unreachable instruction
      if (BranchInst *Branch = dyn_cast<BranchInst>(Terminator)) {
        // First note successor about his predecessor
        assert(!Branch->isConditional());
        Branch->getSuccessor(0)->removePredecessor(&Block->getReference());
        // Then replace terminator
        Terminator = new UnreachableInst(F.getContext());
        ReplaceInstWithInst(Block->getReference().getTerminator(), Terminator);
      }
      // Terminator should be return or unreachable
      assert(isa<ReturnInst>(Terminator) || isa<UnreachableInst>(Terminator));
    } else if (Block->getSuccessors().size() == 1) {
      JTEdge &Edge = Block->getSuccessor(0);
      assert(Edge.isUnconditional() && "Expected unconditional edge.");
      LLVM_DEBUG(Edge.dump();
        dbgs() << "\n");
      BranchInst *Jump = BranchInst::Create(&Edge.getOutBlock().getReference());
      ReplaceInstWithInst(Terminator, Jump);

      // Set edge as unconditional jump
      //Edge.setType(JTEdge::UNCONDITIONAL);
      // Update statistics, TODO is it precise here?
      if (Block->getOriginal(true)->getSuccessors().size() > 1)
        NumThreads++;
    } else {
      if (Block->getSuccessors().front()->isConditional())
        fixConditionalBranch(Terminator, *Block);
      else
        fixSwitch(Terminator, *Block);
    }
  }
}

void CodasipJumpThreading::fixConditionalBranch(Instruction *Terminator, JTBlock &Block)
{
  // sort edges on true edge and false edge
  JTEdge *TrueEdge = &Block.getSuccessor(0);
  JTEdge *FalseEdge = &Block.getSuccessor(1);

  LLVM_DEBUG(
    TrueEdge->dump();
    dbgs() << "\n";
    FalseEdge->dump();
    dbgs() << "\n");

  // create conditional branch when there is one true and one false edge
  assert(TrueEdge->isConditional());
  assert(FalseEdge->isConditional());

  // swap successors if needed
  if (FalseEdge->getCondition()) {
    TrueEdge = &Block.getSuccessor(1);
    FalseEdge = &Block.getSuccessor(0);
  }
  assert(TrueEdge->getCondition());
  assert(!FalseEdge->getCondition());

  // prepare condition
  assert(isa<BranchInst>(Terminator));
  BranchInst *Image = dyn_cast<BranchInst>(Terminator);
  assert(Image->isConditional());

  BranchInst *Jump = BranchInst::Create(
    &TrueEdge->getOutBlock().getReference(),
    &FalseEdge->getOutBlock().getReference(),
    Image->getCondition());
  ReplaceInstWithInst(Terminator, Jump);
}

void CodasipJumpThreading::fixSwitch(Instruction *Terminator, JTBlock &Block)
{
  // Indirect jump
  if (IndirectBrInst *IndirectImage = dyn_cast<IndirectBrInst>(Terminator)) {
    IndirectBrInst *Indirect = IndirectBrInst::Create(IndirectImage->getAddress(), Block.getSuccessors().size());
    size_t ExpectedAddress = 0;
    for (JTEdge *Edge : Block.getSuccessors()) {
      assert(Edge->isIndirect());
      BasicBlock *Successor = &Edge->getOutBlock().getReference();
      assert(Edge->getAddress() == ExpectedAddress++ && "Unexpected indirect jump address.");
      (void)ExpectedAddress;
      Indirect->addDestination(Successor);
    }
    ReplaceInstWithInst(Terminator, Indirect);
  } else if (SwitchInst *SwitchImage = dyn_cast<SwitchInst>(Terminator)) {
    SwitchInst *Switch = SwitchInst::Create(
      SwitchImage->getCondition(), Block.getDefaultCase(), Block.getSuccessors().size() - 1);
    for (JTEdge *Edge : Block.getSuccessors()) {
      assert(Edge->isSwitch());
      BasicBlock *Successor = &Edge->getOutBlock().getReference();
      if (Edge->getCondition())
        Switch->addCase(Edge->getSwitchValue(), Successor);
    }
    ReplaceInstWithInst(Terminator, Switch);
  } else
    assert(false && "Unknown jump with multiple operands to fix.");
}

void CodasipJumpThreading::placeDuplicatedBlocksIntoFunction(Function &F, JTGraph::JTBlocks &Blocks)
{
  for (JTBlock *Block : Blocks)
    if (Block->isDuplicate())
      Block->getReference().insertInto(&F);
}

void CodasipJumpThreading::createDuplicateMap(
  ValueToValueMapTy &Duplicate2Original, JTBlock &Duplicate)
{
  assert(Duplicate.isDuplicate() && "Unexpected original block.");
  JTBlock &Original = *Duplicate.getOriginal();
  for (Instruction &I : Original.getReference()) {
    auto it = Duplicate.getOriginal2Duplicate().find(&I);
    assert(it != Duplicate.getOriginal2Duplicate().end());
    Instruction *DuplicateI = dyn_cast<Instruction>(it->second);
    assert(DuplicateI);
    Duplicate2Original.insert(std::make_pair(DuplicateI, &I));
  }
}
