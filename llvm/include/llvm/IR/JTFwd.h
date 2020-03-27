//===- JTFwd.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef INCLUDE_CODASIP_JUMPTHREADING_IR_JTFWD_H_
#define INCLUDE_CODASIP_JUMPTHREADING_IR_JTFWD_H_

namespace llvm {
namespace jumpthreading {

// Forward declaration.
class JTBlock;
class JTBuilder;
class JTEdge;
class JTGraph;
class JTThreadableNode;

} // namespace llvm::jumpthreading
} // namespace llvm

#endif  // INCLUDE_CODASIP_JUMPTHREADING_IR_JTFWD_H_
