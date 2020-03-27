//===-- JTPhiRebuilder.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/JTDebug.h"
#include "llvm/Transforms/Scalar/JTPhiRebuilder.h"

using namespace llvm;
using namespace jumpthreading;

void JTPhiRebuilder::run(
  JTGraph::JTBlocks &Blocks,
  JTGraph &G,
  JTBuilder &Builder,
  ValueToValueMapTy &Duplicate2Original)
{
  JTPhiRebuilder PR(G, Builder, Duplicate2Original);
  PR(Blocks);
}

JTPhiRebuilder::JTPhiRebuilder(JTGraph &G, JTBuilder &Builder, ValueToValueMapTy &Duplicate2Original)
: G(G),
  Builder(Builder),
  Duplicate2Original(Duplicate2Original)
{}

JTPhiRebuilder::~JTPhiRebuilder()
{
  /// Delete caches
  for (auto &Pair : DDCaches)
    delete Pair.second;
  DDCaches.clear();
}

/**
 *  \brief  Main method of PHI rebuilder. Runs across all given blocks.
 *  \param[in, out]  Blocks Reachable blocks to be processed
 */
void JTPhiRebuilder::operator()(JTGraph::JTBlocks &Blocks)
{
  LLVM_DEBUG(dbgs() << "Computing dominating definitions\n");

  for (JTBlock *Block : Blocks) {
    BasicBlock &Reference = Block->getReference();

    // move instructions here first, because block could be changed later
    std::vector<Instruction*> Instructions;
    for (Instruction &I : Reference)
      Instructions.push_back(&I);

    for (Instruction *I : Instructions)
      // for non-PHI instructions
      if (!isa<PHINode>(I))
        // Recursively get the dominating definitions of nodes reachable via regular control flow
        rebuildInstruction(*I, 0);
  }
}

/**
 *  \brief  Rebuilds values used by given instruction.
 *  \param[in]  I Instruction
 *  \param[in]  Indent Indentation of debug messages
 */
void JTPhiRebuilder::rebuildInstruction(Instruction &I, const size_t Indent)
{
  assert(I.getParent() && "Instruction is missing parent block.");

  LLVM_DEBUG(dbgs().indent(Indent) << "rebuildInstruction " << I
    << " of " << I.getParent()->getName() << "\n");

  assert(!isa<PHINode>(I));

  JTBlock &Block = G.getBlock(*I.getParent());

  for (int i = 0, len = I.getNumOperands(); i < len; i++) {
    Value *PredNode = I.getOperand(i);
    // Operands of jumps are basic blocks and they are skipped
    if (Instruction *PredI = dyn_cast<Instruction>(PredNode)) {
      Instruction &OriginalI = getOriginal(*PredI);
      const size_t StartPositionBack = getPositionFromBack(I) + 1;
      Value *V = getDominatingDefinition(OriginalI, StartPositionBack, &Block, nullptr, Indent);
      I.setOperand(i, V);
    }
  }
}

/**
 *  \brief  Get position from back of given instruction in her parent basic block.
 *  \param[in]  I Instruction
 */
size_t JTPhiRebuilder::getPositionFromBack(Instruction &I)
{
  assert(I.getParent() && "Instruction is missing parent block.");

  BasicBlock &Block = *I.getParent();
  size_t Position = 0;
  for (auto RIt = Block.rbegin(); RIt != Block.rend(); ++RIt, Position++)
    if (&*RIt == &I)
      return Position;

  assert(false && "Instruction missing in parent block.");
  return 0;
}

/**
 *  \brief  Get dominating definition of given instruction and given block.
 *  \param[in]  I Instruction
 *  \param[in]  StartPositionBack From which position should be blocked reverse looked up for dominating definition.
 *  \param[in]  Block Block where dominating definition should be found or created as new PHI
 *  \param[in]  DominatingDefCache Cache of dominating definitions
 *  \param[in]  Indent Indentation of debug messages
 */
Value *JTPhiRebuilder::getDominatingDefinition(
  Instruction &I,
  const size_t StartPositionBack,
  JTBlock *Block,
  DDCache *DominatingDefCache,
  const size_t Indent)
{
  assert(I.getParent() && "Missing parent of instruction.");

  LLVM_DEBUG(dbgs().indent(Indent) << "getDominatingDef " << I
    << " of " << I.getParent()->getName() << ", useblock '" << Block->getName() << "'\n");

  if (!Block->isDuplicate())
    assert(Block == Block->getOriginal(true));
  else
    assert(Block != Block->getOriginal(true));

  assert(&I == &getOriginal(I) && "Expected original instruction.");

  // Try to fetch a cached result.
  Value *Result = nullptr;
  if (DominatingDefCache == nullptr)
    DominatingDefCache = getDDCache(I);

  if (DominatingDefCache == nullptr) {
    // Shortcut: if the original instruction's block has not been duplicated, there is only one definition.
    // We still need to fix this instruction's predecessors though.
    if (!G.getBlock(*I.getParent()).IsDuplicated()) {
      Result = &I;
      Block = &G.getBlock(*I.getParent());
    } else {
      DominatingDefCache = new DDCache();
      DDCaches.insert(std::make_pair(&I, DominatingDefCache));
    }
  } else
    Result = getCachedDominatingDefinition(*Block, *DominatingDefCache);

  // Cache miss
  if (Result == nullptr) {
    size_t Position = 0;
    BasicBlock &BB = Block->getReference();
    for (auto RIt = BB.rbegin(); RIt != BB.rend(); ++RIt, Position++) {
      // Skip nodes before start search
      if (Position < StartPositionBack)
        continue;

      // Find instruction, it is original or duplicate in current block
      LLVM_DEBUG(dbgs() << "Original I: ";
        getOriginal(*RIt).dump();
        dbgs() << "I: ";
        I.dump();
        dbgs() << &getOriginal(*RIt) << " vs " << &I << "\n");
      if (&getOriginal(*RIt) == &I) {
        Result = &*RIt;
        break;
      }
    }

    // Update name when found Result is a duplicate
    if (Result) {
      assert(isa<Instruction>(Result));
      Instruction &ResultI = *dyn_cast<Instruction>(Result);
      assert(ResultI.getParent());
      if (G.getBlock(*ResultI.getParent()).isDuplicate())
        Builder.updateDuplicateName(*Result);
    }
  }

  // Cache miss && not in current block
  if (Result == nullptr) {
    LLVM_DEBUG(dbgs().indent(Indent) << "GDD: PHI(" << Block->getPredecessors().size() << ")\n");
    if (Block->getPredecessors().size() == 1) {
      JTEdge *PredecessorEdge = Block->getPredecessors().front();
      assert(PredecessorEdge != nullptr);
      Result = getDominatingDefinition(I, 0, &PredecessorEdge->getInBlock(), DominatingDefCache, Indent + 2);
      assert(Result != nullptr);
      assert(isa<Instruction>(Result));
      BasicBlock *Parent = dyn_cast<Instruction>(Result)->getParent();
      // TODO? if block is null return result
      if (Parent == nullptr) {
        LLVM_DEBUG(dbgs() << "return result\n");
        return Result;
      }
      LLVM_DEBUG(dbgs() << "result is";
        Result->dump());
      Block = &G.getBlock(*Parent);
    } else {
      // have NULL block?
      PHINode* Phi = PHINode::Create(I.getType(), Block->getPredecessors().size(), I.getName());
      Builder.updateDuplicateName(*Phi);
      // insert newly created PHI instruction at start of the use block
      // FIXME not working here Phi->insertBefore(&*Block->getReference().begin());
      // TODO is this really needed? or it will be always taken from cache?
      Duplicate2Original.insert(std::make_pair(Phi, &I));
      // insert newly created PHI instruction at start of the use block
      Phi->insertBefore(&*Block->getReference().begin());
      Result = Phi;
    }
  }

  assert(Result && "Result is null.");

  // Recursively fix the arguments of the result. If it is a Phi, we might additionally need to adjust
  // the number of arguments to match the number of the block's cfpreds. We need to do this only once.
  if (!isVisitedElseMark(*Result)) {
    // TODO setNodeLink? set_irn_link(Result, &I);
    // Dominating definition cache could be missing when TODO investigate reason.
    // Happened on pr931009_1.
    if (DominatingDefCache != nullptr) {
      // Insert result into the cache before making any recursive calls.
      DominatingDefCache->insert(std::make_pair(Block, Result));
    }

    if (!isa<PHINode>(Result)) {
      assert(isa<Instruction>(Result));
      Instruction *ResultI = dyn_cast<Instruction>(Result);
      rebuildInstruction(*ResultI, Indent + 2);
    } else if (&Block->getOriginal(true)->getReference() == I.getParent()) {
      // result is equal to instruction or a duplicate thereof
      LLVM_DEBUG(dbgs().indent(Indent) << "Tweak duplicate\n");

      PHINode* Phi = dyn_cast<PHINode>(Result);

      // Correct incoming values of PHI instruction even when they are not predecessors anymore
      // Test pr20020615_1.c depends on it
      for (size_t i = 0; i < Phi->getNumIncomingValues(); i++) {
        Value *V = Phi->getIncomingValue(i);
        BasicBlock *BB = Phi->getIncomingBlock(i);
        if (Instruction *PredI = dyn_cast<Instruction>(V)) {
          // Get original instruction
          PredI = &getOriginal(*PredI);
          LLVM_DEBUG(dbgs() << "PredI: " << *PredI << " in " << BB->getName() << "\n");
          Phi->setIncomingValue(i,
            getDominatingDefinition(*PredI, 0, &G.getBlock(*BB), nullptr, Indent + 2));
        }
      }

      // Add extra basic blocks
      // First gather all present predecessors, needs to be done this way for test pr20010129_1.c
      std::list<BasicBlock*> Predecessors;
      for (size_t i = 0; i < Phi->getNumIncomingValues(); i++)
        Predecessors.push_back(Phi->getIncomingBlock(i));

      LLVM_DEBUG(dbgs().indent(Indent) << "Add extra basic blocks " << Block->getName() << "\n");
      for (JTEdge *Edge : Block->getPredecessors()) {
        JTBlock &Predecessor = Edge->getInBlock();

        // Skip already present predecessors
        bool exist = false;
        for (auto it = Predecessors.begin(); it != Predecessors.end(); ++it)
          if (*it == &Predecessor.getReference()) {
            exist = true;
            Predecessors.erase(it);
            break;
          }
        if (exist)
          continue;

        // Find original information in Phi and use it to create proper incoming information
        bool Found = false;
        for (size_t i = 0; i < Phi->getNumIncomingValues(); i++) {
          Value *V = Phi->getIncomingValue(i);
          BasicBlock *BB = Phi->getIncomingBlock(i);
          if (&Predecessor.getOriginal(true)->getReference() == BB) {
            if (Instruction *PredI = dyn_cast<Instruction>(V)) {
              Phi->addIncoming(
                getDominatingDefinition(getOriginal(*PredI), 0, &Predecessor, nullptr, Indent + 2),
                &Predecessor.getReference());
            }
            else
              Phi->addIncoming(V, &Predecessor.getReference());
            Found = true;
            break;
          }
        }
        assert(Found);
      }

      // Clean useless incoming values of PHI instruction
      LLVM_DEBUG(dbgs().indent(Indent) << "Clean useless incoming values of PHI instruction " << Block->getName() << "\n");

      // Get all predecessor blocks and their count
      std::map<BasicBlock*, size_t> Predecessors2;
      for (JTEdge *Edge : Block->getPredecessors()) {
        BasicBlock &Predecessor = Edge->getInBlock().getReference();
        auto It = Predecessors2.find(&Predecessor);
        if (It == Predecessors2.end()) {
            Predecessors2.insert(std::make_pair(&Predecessor, 0));
          It = Predecessors2.find(&Predecessor);
        }
        assert(It != Predecessors2.end());
        It->second++;
      }

      // Get all basic blocks for removal
      std::vector<BasicBlock*> Removal;
      for (size_t i = 0; i < Phi->getNumIncomingValues(); i++) {
        BasicBlock *BB = Phi->getIncomingBlock(i);
        auto It = Predecessors2.find(BB);
        // Block is not a predecessor
        if (It == Predecessors2.end()) {
          LLVM_DEBUG(dbgs() << "Remove " << BB->getName() << " from ";
            Phi->dump());
          Removal.push_back(BB);
        } else {
          assert(It->second != 0);
          It->second--;
          if (It->second == 0)
            Predecessors2.erase(It);
        }
      }

      // Remove all basic blocks from PHI instruction operands that are not predecessors anymore
      for (BasicBlock *BB : Removal)
        Phi->removeIncomingValue(BB, false); // TODO remove PHI when empty?
    } else {
      // Pending PHI code
      LLVM_DEBUG(dbgs().indent(Indent) << "Tweak PHI of block '" << Block->getId() << "'\n");

      Value *FirstNonbadPred = nullptr;
      bool NeedPhi = false;
      for (size_t i = 0; i < Block->getPredecessors().size(); i++) {
        JTEdge *Edge = Block->getPredecessors()[i];
        JTBlock &Predecessor = Edge->getInBlock();
        Value *PredDominatingDef = getDominatingDefinition(
          I, 0, &Predecessor, DominatingDefCache, Indent + 2);
        assert(PredDominatingDef && "Bad predecessor result.");
        dyn_cast<PHINode>(Result)->addIncoming(PredDominatingDef, &Predecessor.getReference());
        if (FirstNonbadPred == nullptr)
          FirstNonbadPred = PredDominatingDef;
        else
          NeedPhi |= (i > 0 && PredDominatingDef != Result && PredDominatingDef != FirstNonbadPred);
      }
      /*if (!NeedPhi) { TODO need this?
        Value *ResultOld = Result;
        Result = FirstNonbadPred;
        dbgs() << "EXCHANGE ";
        ResultOld->dump();
        dbgs() << " <=> ";
        Result->dump();
        //assert(false && "EXCHANGE");
        //assert(false && "EXCHANGE"); TODO?
        // The dummy PHI might linger around somewhere in the cache from recursive calls.
        // Exchange it there as well.
        for (auto &Entry : *DominatingDefCache)
          if (Entry.second == ResultOld)
            Entry.second = Result;
      }*/
    }
  }

  assert(Result != nullptr);
  LLVM_DEBUG(dbgs() << "Result ";
    Result->dump());
  return Result;
}

/**
 *  \brief  Get original instruction to given instruction duplicate.
 *  \param[in]  I Instruction duplicate
 */
Instruction &JTPhiRebuilder::getOriginal(Instruction &I)
{
  auto it = Duplicate2Original.find(&I);
  return it != Duplicate2Original.end() ? *dyn_cast<Instruction>(it->second) : I;
}

/**
 *  \brief  Get cache of dominating definitions to given (original) value.
 *  \param[in]  V Value
 */
JTPhiRebuilder::DDCache *JTPhiRebuilder::getDDCache(Value &V)
{
  auto it = DDCaches.find(&V);
  return it != DDCaches.end() ? it->second : nullptr;
}

/**
 *  \brief  Get cached dominating definition for given block.
 *  \param[in]  Block Processed block
 *  \param[in]  Cache Cache of dominating definition of some original value.
 */
Value *JTPhiRebuilder::getCachedDominatingDefinition(JTBlock &Block, DDCache &Cache)
{
  auto it = Cache.find(&Block);
  return it != Cache.end() ? it->second : nullptr;
}

/**
 *  \brief  Determines whether given value was visited and if not it marks it as visited.
 *  \param[in]  V Value
 */
bool JTPhiRebuilder::isVisitedElseMark(Value &V)
{
  auto it = Visited.find(&V);
  if (it != Visited.end()) {
    return true;
  }
  Visited.insert(&V);
  return false;
}
