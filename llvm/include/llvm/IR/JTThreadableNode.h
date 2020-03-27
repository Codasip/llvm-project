//===- JTThreadableNode.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTTHREADABLENODE_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTTHREADABLENODE_H_

#include "llvm/Support/raw_ostream.h"

#include "JTBlock.h"

namespace llvm {
namespace jumpthreading {

// Forward declaration.
class JTBuilder;

/**
 *  \brief  Used as a representation during initial analysis of JT.
 */
class JTThreadableNode
{
public:
  /// Block that are visited when entered
  typedef std::vector<JTBlock*> BlockList;
  /// Children
  typedef std::vector<JTThreadableNode*> ChildrenList;

  ~JTThreadableNode();

  /**
   *  \{
   *  \brief  Getter.
   */
  const std::string &getId() const;
  const Value &getValue() const;
  const BlockList &getBlocks() const;
  JTThreadableNode *getParent() const;
  const ChildrenList &getChildren() const;
  JTThreadableNode *getChild(const size_t Index);
  size_t getCost() const;
  /// \}
  /**
   *  \brief  Get node name for debugging purposes.
   *  \example  Return '%add' for '%add = %x + %y'.
   *  \example  Return '7' for '%0 = int32 7.
   */
  const std::string getNodeName() const;

  /**
   *  \brief  Insert block into blocks.
   *  \param[in]  Block JT block
   *  \param[in]  InsertAtEnd Insert at vector end, on false on vector begin
   */
  void addBlock(JTBlock &Block, const bool InsertAtEnd = true);
  /**
   *  \{
   *  \brief  Setter.
   */
  void setParent(JTThreadableNode &Value);
  void addChild(JTThreadableNode *Child);
  void setCost(const size_t Value);
  /// \}

  /**
   *  \brief  Print *.dot to given path.
   *  \param[in]  Path path of created *.dot file
   */
  void printDot(const std::string &Path) const;

private:
  friend JTBuilder;

  JTThreadableNode(const std::string &Identifier, const Value &Node);

  void printDot(raw_fd_ostream &Out) const;

  /// Id
  const std::string Id;
  /// Operation
  const Value &V;
  /// Blocks in the path
  BlockList Blocks;
  /// Parent
  JTThreadableNode *Parent;
  /// Children
  ChildrenList Children;
  /// Cost
  size_t Cost;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline const std::string &JTThreadableNode::getId() const
{
  return Id;
}

inline const Value &JTThreadableNode::getValue() const
{
  return V;
}

inline const JTThreadableNode::BlockList &JTThreadableNode::getBlocks() const
{
  return Blocks;
}

inline JTThreadableNode *JTThreadableNode::getParent() const
{
  return Parent;
}

inline const JTThreadableNode::ChildrenList &JTThreadableNode::getChildren() const
{
  return Children;
}

inline JTThreadableNode *JTThreadableNode::getChild(const size_t Index)
{
  assert(Index < getChildren().size() && "ThreadableNode: Child index out of range.");
  return getChildren()[Index];
}

inline size_t JTThreadableNode::getCost() const
{
  return Cost;
}

inline const std::string JTThreadableNode::getNodeName() const
{
  std::string NodeName = getValue().getName().str();
  if (const ConstantInt *C = dyn_cast<ConstantInt>(&getValue()))
    NodeName = C->getValue().toString(10, true);
  return NodeName;
}

inline void JTThreadableNode::addBlock(JTBlock &Block, const bool InsertAtEnd)
{
  if (InsertAtEnd)
    Blocks.push_back(&Block);
  else
    Blocks.insert(Blocks.begin(), &Block);
}

inline void JTThreadableNode::setParent(JTThreadableNode &Value)
{
  Parent = &Value;
}

inline void JTThreadableNode::addChild(JTThreadableNode *Child)
{
  Children.push_back(Child);
  Child->setParent(*this);
}

inline void JTThreadableNode::setCost(const size_t Value)
{
  Cost = Value;
}

}   // namespace llvm::jumpthreading
}   // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTTHREADABLENODE_H_
