//===- JTDebug.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_JTDEBUG_H_
#define INCLUDE_CODASIP_JUMPTHREADING_JTDEBUG_H_

#include <string>

#include "llvm/IR/JTFwd.h"

#define DEBUG_TYPE "codasip-jump-threading"

namespace llvm {
namespace jumpthreading {

// Forward declaration.
class JTGraph;
class JTThreadableNode;

/**
 *  \brief  Provides debugging facility for the JT passes.
 */
class JTDebug
{
public:
  /**
   *  \brief  Prints dot representation of JT graph.
   *  \param[in]  G JT graph
   *  \param[in]  FileName Output file
   */
  static void printDot(JTGraph &G, const std::string& FileName);
  /**
   *  \brief  Prints dot representation of threadable node tree.
   *  \param[in]  Tree threadable node tree
   *  \param[in]  FileName Output file
   */
  static void printDot(JTThreadableNode &Tree, const std::string& FileName);

  /// \brief  Get pass count.
  static size_t getPassCount();
  /// \brief  Increment pass count.
  static void incrementPassCount();

private:
  /// Pass count, TODO support multithreading once?
  static size_t PassCount;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
inline size_t JTDebug::getPassCount()
{
  return PassCount;
}

inline void JTDebug::incrementPassCount()
{
  PassCount++;
}

} // namespace llvm
} // namespace llvm::jumpthreading

#endif  // INCLUDE_CODASIP_JUMPTHREADING_JTDEBUG_H_
