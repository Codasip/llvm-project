//===- JTBuilder.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTBUILDER_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTBUILDER_H_

#include <map>

#include "llvm/IR/BasicBlock.h"

#include "JTFwd.h"

namespace llvm {
namespace jumpthreading {

/**
 *  \brief This class is used to build various JT objects.
 *         It also encapsulates variables for creation of unique block and instruction names.
 */
class JTBuilder
{
public:
  JTBuilder();

  /**
   *  \brief  Creates threadable node.
   *  \param[in]  V LLVM Value
   */
  JTThreadableNode &CreateThreadableNode(const Value &V);
  /**
   *  \brief  Creates abstract block over existing LLVM basic block.
   *          This method should be used only at start of the JT algorithm.
   *  \param[in]  Reference initial LLVM basic block
   */
  JTBlock &CreateBlock(BasicBlock &Reference);
  /**
   *  \brief  Creates abstract block that represents a duplicate of given Original block.
   *  \param[in]  Original
   */
  JTBlock &CreateBlock(JTBlock &Original);
  /// \brief  Creates graph, this represents either input program or threadable graph.
  JTGraph &CreateGraph();

  /**
   *  \brief Updates duplicated block name with suffix '.jtI',
   *         where I is the next unused index.
   *  \example First duplicate of 'for.cond' will be 'for.cond.jt0'.
   *  \param[in] Block
   */
  void updateDuplicateName(JTBlock &Block);
  /**
   *  \brief Updates duplicated instruction name with suffix '.jtI',
   *         where I is the next unused index.
   *  \example First duplicate of '%i.0' will be '%i.0.jt0'.
   *  \param[in] I
   */
  void updateDuplicateName(Value &I);

private:
  /// Maps given identifier into next unique index
  typedef std::map<std::string, unsigned int> Name2Index;

  /// Threadable node counter
  size_t ThreadableNodeCount;
  /// Block counter
  size_t BlockCount;
  /// Map of block indexes
  Name2Index BlockIndexes;
  /// Map of instruction indexes
  Name2Index InstructionIndexes;
};

} // namespace llvm::jumpthreading
} // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTBUILDER_H_
