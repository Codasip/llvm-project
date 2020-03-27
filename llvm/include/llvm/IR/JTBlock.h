//===- JTBlock.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTBLOCK_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTBLOCK_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include "JTEdge.h"

namespace llvm {
namespace jumpthreading {

// Forward declaration.
class JTBuilder;

/**
 *  \brief  Represents Jump Threading basic block.
 */
class JTBlock
{
public:
  /// List of edges
  typedef std::vector<JTEdge*> Edges;

  const std::string &getId() const;
  bool isDuplicate() const;
  /**
   *  \brief  Get LLVM basic block, firstly reference, then original.
   */
  BasicBlock &getLlvmBlock();
  const BasicBlock &getLlvmBlock() const;
  JTBlock *getOriginal(const bool IncludeItself = false);
  const JTBlock *getOriginal(const bool IncludeItself = false) const;
  BasicBlock &getReference();
  const BasicBlock &getReference() const;
  Edges &getPredecessors();
  JTEdge &getPredecessor(const size_t Index);
  Edges &getSuccessors();
  const Edges &getSuccessors() const;
  JTEdge &getSuccessor(const size_t Index);
  ValueToValueMapTy &getOriginal2Duplicate();
  Instruction &getDuplicate(Instruction &I);
  size_t getCost() const;
  bool IsDuplicated() const;
  bool IsReachable() const;

  bool hasReference() const;
  bool isPredecessor(BasicBlock &BB);
  /**
   *  \brief  Determines whether this block is predecessor of given block.
   *  \param[in]  Block given block
   */
  bool isPredecessorOf(JTBlock &Block);
  /**
   *  \brief  Determines whether this block is successor of given block.
   *  \param[in]  Block given block
   */
  bool isSuccessorOf(JTBlock &Block);

  void setOriginal(JTBlock &Block);
  void setReference(BasicBlock &Block);
  void addPredecessor(JTEdge &Predecessor);
  void addSuccessor(JTEdge &Successor);
  void removePredecessor(JTEdge &Edge);
  void removeSuccessor(JTEdge &Edge);
  void setDuplicated(const bool Value);
  void setReachable(const bool Value);
  /**
   *  \brief  Use To as successor block instead of successor block From.
   *  \param[out] To new successor
   *  \param[in]  From old successor
   */
  void reroute(JTBlock &To, JTBlock &From);
  void cloneBlock();

  // shortcuts
  /// \brief  Get address value of indirect jump, otherwise throws exception.
  const Value &getAddressValue() const;
  /// \brief  Get condition of conditional jump, otherwise throws exception.
  Value &getCondition() const;
  const BranchInst &getConditionalJump() const;
  const IndirectBrInst &getIndirectJump() const;
  const SwitchInst &getSwitch() const;
  StringRef getName() const;
  bool isConditional() const;
  bool isSwitch() const;

  /// \brief  Tries to find SWITCH default case, asserts on failure.
  BasicBlock *getDefaultCase();

private:
  friend JTBuilder;

  JTBlock(const std::string &Identifier);

  bool containsPredecessorEdge(JTEdge *Edge);

  /// Id
  const std::string Id;
  /// Original block
  JTBlock *Original;
  /// Reference to LLVM basic block.
  BasicBlock *Reference;
  /// Predecessors of JTBlock, does not equal to predecessors of LLVM basic block during work
  Edges Predecessors;
  /// Predecessors of JTBlock, does not equal to predecessors of LLVM basic block during work
  Edges Successors;
  /// Maps original instructions to duplicated instructions
  /// Duplicated block has information about original instructions, that creates his instructions
  ValueToValueMapTy Original2Duplicate;
  /// Determines if the block was duplicated.
  bool Duplicated;
  /// Determines if the block is reachable from the entry block, set before LLVM construction phase
  bool Reachable;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline const std::string &JTBlock::getId() const
{
  return Id;
}

inline bool JTBlock::isDuplicate() const
{
  return Original != nullptr;
}

inline BasicBlock &JTBlock::getLlvmBlock()
{
  return hasReference() ? getReference() : Original->getReference();
}

inline const BasicBlock &JTBlock::getLlvmBlock() const
{
  assert((hasReference() || Original) && "Missing both reference and original block.");
  return hasReference() ? getReference() : Original->getReference();
}

inline JTBlock *JTBlock::getOriginal(const bool IncludeItself)
{
  return (IncludeItself && Original == nullptr) ? this : Original;
}

inline const JTBlock *JTBlock::getOriginal(const bool IncludeItself) const
{
  return (IncludeItself && Original == nullptr) ? this : Original;
}

inline BasicBlock &JTBlock::getReference()
{
  /*if (Reference == nullptr)
    dbgs() << "Missing reference in " << getName() << "[" << getId() << "]\n";*/
  assert(Reference != nullptr);
  return *Reference;
}

inline const BasicBlock &JTBlock::getReference() const
{
  /*if (Reference == nullptr)
    dbgs() << "Missing reference in " << getName() << "[" << getId() << "]\n";*/
  assert(Reference != nullptr);
  return *Reference;
}

inline JTBlock::Edges &JTBlock::getPredecessors()
{
  return Predecessors;
}

inline JTEdge &JTBlock::getPredecessor(const size_t Index)
{
  assert(Index < getPredecessors().size());
  return *getPredecessors()[Index];
}

inline JTBlock::Edges &JTBlock::getSuccessors()
{
  return Successors;
}

inline const JTBlock::Edges &JTBlock::getSuccessors() const
{
  return Successors;
}

inline JTEdge &JTBlock::getSuccessor(const size_t Index)
{
  assert(Index < getSuccessors().size());
  return *getSuccessors()[Index];
}

inline ValueToValueMapTy &JTBlock::getOriginal2Duplicate()
{
  return Original2Duplicate;
}

inline Instruction &JTBlock::getDuplicate(Instruction &I)
{
  auto it = Original2Duplicate.find(&I);
  return it != Original2Duplicate.end() ? *dyn_cast<Instruction>(it->second) : I;
}

inline size_t JTBlock::getCost() const
{
  return getReference().size();
}

inline bool JTBlock::IsDuplicated() const
{
  return Duplicated;
}

inline bool JTBlock::IsReachable() const
{
  return Reachable;
}

inline bool JTBlock::hasReference() const
{
  return Reference != nullptr;
}

inline void JTBlock::setOriginal(JTBlock &Block)
{
  Original = &Block;
}

inline void JTBlock::setReference(BasicBlock &Block)
{
  Reference = &Block;
}

inline void JTBlock::addPredecessor(JTEdge &Predecessor)
{
  getPredecessors().push_back(&Predecessor);
}

inline void JTBlock::addSuccessor(JTEdge &Successor)
{
  getSuccessors().push_back(&Successor);
}

inline void JTBlock::removePredecessor(JTEdge &Edge)
{
  Edges &P = getPredecessors();
  P.erase(std::remove(P.begin(), P.end(), &Edge), P.end());
}

inline void JTBlock::removeSuccessor(JTEdge &Edge)
{
  Edges &S = getSuccessors();
  S.erase(std::remove(S.begin(), S.end(), &Edge), S.end());
}

inline void JTBlock::setDuplicated(const bool Value)
{
  Duplicated = Value;
}

inline void JTBlock::setReachable(const bool Value)
{
  Reachable = Value;
}

inline void JTBlock::reroute(JTBlock &To, JTBlock &From)
{
  //dbgs() << "Reroute " << From.getName() << " => " << To.getName() << " in " << getName() << "\n";

  // Reroute all successor edges of this form 'Entry => From' into 'Entry => To'
  for (JTEdge *Edge : getSuccessors())
    if (&Edge->getOutBlock() == &From) {
      Edge->setOutBlock(To);
      From.removePredecessor(*Edge);
      To.addPredecessor(*Edge);
    }
}

inline void JTBlock::cloneBlock()
{
  assert(isDuplicate() && "Block is not a duplicate.");
  assert(Reference == nullptr && "Block reference is not null.");
  assert(getOriginal() && "Original block is null.");
  Reference = CloneBasicBlock(&getOriginal()->getReference(), getOriginal2Duplicate());
  //dbgs() << "Cloning " << getOriginal()->getName() << " into " << getName() << "\n";
}

inline const Value &JTBlock::getAddressValue() const
{
  return *getIndirectJump().getAddress();
}

inline Value &JTBlock::getCondition() const
{
  return *getConditionalJump().getCondition();
}

inline const BranchInst &JTBlock::getConditionalJump() const
{
  const auto *Terminator = getLlvmBlock().getTerminator();
  assert(isa<BranchInst>(Terminator) && "Terminator is not a jump.");
  const BranchInst *Branch = dyn_cast<BranchInst>(Terminator);
  assert(Branch->isConditional() && "Terminator is not a conditional jump.");
  return *Branch;
}

inline const IndirectBrInst &JTBlock::getIndirectJump() const
{
  const auto *Terminator = getLlvmBlock().getTerminator();
  assert(isa<IndirectBrInst>(Terminator) && "Terminator is not an indirect jump.");
  return *dyn_cast<IndirectBrInst>(Terminator);
}

inline const SwitchInst &JTBlock::getSwitch() const
{
  const auto *Terminator = getLlvmBlock().getTerminator();
  assert(isa<SwitchInst>(Terminator) && "Terminator is not a switch.");
  return *dyn_cast<SwitchInst>(Terminator);
}

inline StringRef JTBlock::getName() const
{
  return getLlvmBlock().getName();
}

inline bool JTBlock::isConditional() const
{
  const auto *Terminator = getLlvmBlock().getTerminator();
  if (!isa<BranchInst>(Terminator))
    return false;
  const BranchInst *Branch = dyn_cast<BranchInst>(Terminator);
  return Branch->isConditional();
}

inline bool JTBlock::isSwitch() const
{
  const auto *Terminator = getLlvmBlock().getTerminator();
  return isa<SwitchInst>(Terminator);
}

}   // namespace llvm::jumpthreading
}   // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTBLOCK_H_
