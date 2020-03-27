//===-- JTDebug.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/JTGraph.h"
#include "llvm/IR/JTThreadableNode.h"
#include "llvm/Transforms/Scalar/JTDebug.h"

using namespace llvm;
using namespace jumpthreading;

// Give here absolute path as debug file destination
const std::string DEBUG_PATH = "jt/";

size_t JTDebug::PassCount = 0;

void JTDebug::printDot(JTGraph &G, const std::string& FileName)
{
  LLVM_DEBUG(G.printDot(DEBUG_PATH + std::to_string(PassCount) + "_" + FileName));
}

void JTDebug::printDot(JTThreadableNode &Tree, const std::string& FileName)
{
  LLVM_DEBUG(Tree.printDot(DEBUG_PATH + std::to_string(PassCount) + "_" + FileName));
}
