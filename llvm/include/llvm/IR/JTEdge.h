//===- JTEdge.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTEDGE_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTEDGE_H_

#include "llvm/IR/Constants.h"

namespace llvm {
namespace jumpthreading {

// Forward declaration.
class JTBlock;

/**
 *  \brief  Represents JT edge.
 */
class JTEdge
{
public:
  /// Enumeration of edge types.
  typedef enum {
    UNCONDITIONAL,
    CONDITIONAL,
    SWITCH,
    INDIRECT,
  } EdgeType;

  /**
   *  \brief  Creates unconditional jump edge.
   *  \param[in]  InBlock incoming block
   *  \param[in]  OutBlock outgoing block
   */
  static JTEdge &CreateUnconditional(JTBlock &InBlock, JTBlock &OutBlock);
  /**
   *  \brief  Creates conditional jump edge.
   *  \param[in]  InBlock incoming block
   *  \param[in]  OutBlock outgoing block
   *  \param[in]  Condition determines whether edge is taken on true or false result of condition
   */
  static JTEdge &CreateConditional(JTBlock &InBlock, JTBlock &OutBlock, const bool Condition);
  /**
   *  \brief  Creates switch case jump edge.
   *  \param[in]  InBlock incoming block
   *  \param[in]  OutBlock outgoing block
   *  \param[in]  IsDefault determines whether edge represents default case of switch
   *  \param[in]  SwitchValue represents value, that makes edge to be taken, null for default edge
   */
  static JTEdge &CreateSwitch(
    JTBlock &InBlock, JTBlock &OutBlock, const bool IsDefault, ConstantInt *SwitchValue = nullptr);
  /**
   *  \brief  Creates indirect jump edge.
   *  \param[in]  InBlock incoming block
   *  \param[in]  OutBlock outgoing block
   *  \param[in]  Address represents value, that makes edge to be taken
   */
  static JTEdge &CreateIndirect(JTBlock &InBlock, JTBlock &OutBlock, const unsigned int Address);
  /**
   *  \brief  Creates edge copy.
   *  \param[in]  Edge copied edge
   *  \param[in]  InBlock new incoming block
   *  \param[in]  OutBlock new outgoing block
   *  \param[in]  UpdateOutBlock adds new edge into predecessors of outgoing block when set
   */
  static JTEdge &CreateCopy(
    const JTEdge &Edge, JTBlock &InBlock, JTBlock &OutBlock, const bool UpdateOutBlock);

  /**
   *  \{
   *  \brief  Getter.
   */
  EdgeType getType() const;
  JTBlock &getInBlock() const;
  JTBlock &getOutBlock() const;
  bool getCondition() const;
  ConstantInt* getSwitchValue() const;
  unsigned int getAddress() const;
  /// /}
  /// \brief  Determines if edge type is unconditional.
  bool isUnconditional() const;
  /// \brief  Determines if edge type is conditional.
  bool isConditional() const;
  /// \brief  Determines if edge type is switch.
  bool isSwitch() const;
  /// \brief  Determines if edge represents default case of switch.
  bool isDefault() const;
  /// \brief  Determines if edge type is indirect jump.
  bool isIndirect() const;

  /**
   *  \{
   *  \brief  Getter.
   */
  void setType(const EdgeType Value);
  void setInBlock(JTBlock &Block);
  void setOutBlock(JTBlock &Block);
  void setCondition(const bool Value);
  void setSwitchValue(ConstantInt *Value);
  void setAddress(const unsigned int Value);
  /// /}

  /**
   *  \brief  Delete edge.
   *          Firstly removes edge from incoming block successors and outgoing block predecessors.
   */
  void deleteEdge();
  /// \brief  Dump edge content for debugging purposes.
  void dump() const;

private:
  JTEdge(
    const EdgeType Type,
    JTBlock &InBlock,
    JTBlock &OutBlock,
    const bool UpdateOutBlock = true);

  /// Edge type
  EdgeType Type;
  /// In-block
  JTBlock *In;
  /// Out-block
  JTBlock *Out;
  /// Condition, or is non-default case for switch typed edge
  bool Condition;
  /// Switch case value
  ConstantInt* SwitchValue;
  /// Indirect jump address
  unsigned int Address;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline JTEdge::EdgeType JTEdge::getType() const
{
  return Type;
}

inline JTBlock &JTEdge::getInBlock() const
{
  return *In;
}

inline JTBlock &JTEdge::getOutBlock() const
{
  return *Out;
}

inline bool JTEdge::getCondition() const
{
  return Condition;
}

inline ConstantInt *JTEdge::getSwitchValue() const
{
  return SwitchValue;
}

inline unsigned int JTEdge::getAddress() const
{
  return Address;
}

inline bool JTEdge::isUnconditional() const
{
  return getType() == UNCONDITIONAL;
}

inline bool JTEdge::isConditional() const
{
  return getType() == CONDITIONAL;
}

inline bool JTEdge::isSwitch() const
{
  return getType() == SWITCH;
}

inline bool JTEdge::isDefault() const
{
  return !Condition;
}

inline bool JTEdge::isIndirect() const
{
  return getType() == INDIRECT;
}

inline void JTEdge::setType(const EdgeType Value)
{
  Type = Value;
}

inline void JTEdge::setInBlock(JTBlock &Block)
{
  In = &Block;
}

inline void JTEdge::setOutBlock(JTBlock &Block)
{
  Out = &Block;
}

inline void JTEdge::setCondition(const bool Value)
{
  Condition = Value;
}

inline void JTEdge::setSwitchValue(ConstantInt *Value)
{
  SwitchValue = Value;
}

inline void JTEdge::setAddress(const unsigned int Value)
{
  Address = Value;
}

}   // namespace llvm::jumpthreading
}   // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTEDGE_H_
