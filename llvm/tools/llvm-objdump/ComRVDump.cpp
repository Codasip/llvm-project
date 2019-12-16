//===-- ComRVDump.cpp - ComRV-specific dumper -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ComRV specific dumping info for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "llvm-objdump.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "comrv"

namespace {
// Information held on a particular function clone
struct FunctionInfo {
  StringRef Name;
  uint64_t Offset;
};
// Information held on a particular group data
struct GroupData {
  uint64_t StartAddress;
  uint64_t EndAddress;
  uint16_t CRC;
  SmallVector<FunctionInfo, 16> Functions;
};
// Information on the overlay system
struct OverlayData {
  const SectionRef *Section;
  DenseMap<uint64_t, GroupData> Groups;
  uint64_t StartAddress;
  uint64_t EndAddress;
  uint64_t GroupCount;
};

} // namespace

// Initilizes the data structure in OverlayData, ready for the addition of
// symbols
static void InitilizeGroupTables(OverlayData &OD, const SectionRef &Data) {
  support::endianness Endian =
      Data.getObject()->isLittleEndian() ? support::little : support::big;
  OD.Section = &Data;
  uint64_t StartAddress = Data.getAddress();
  OD.StartAddress = StartAddress;
  StringRef Contents =
      unwrapOrError(Data.getContents(), Data.getObject()->getFileName());
  uint64_t Index = 0;
  uint64_t Group = 0;
  while (support::endian::read16(Contents.data() + Index + 2, Endian) != 0) {
    uint16_t Start = support::endian::read16(Contents.data() + Index, Endian);
    uint16_t End = support::endian::read16(Contents.data() + Index + 2, Endian);
    OD.EndAddress = End;
    LLVM_DEBUG(errs() << "* Group " << Group << " starts at index " << Start
                      << " and finishes at " << End << "\n");
    uint16_t CRC =
        support::endian::read16(Contents.data() + End * 512 - 2, Endian);
    OD.Groups[Group] = {StartAddress + Start * 512, StartAddress + End * 512,
                        CRC};
    Group++;
    Index += 2;
  }
  OD.GroupCount = Group;
}

// Add sybmols to group tables
static void PopulateGroupSymbols(OverlayData &OD, const ObjectFile *Obj) {
  for (auto &Sym : Obj->symbols()) {
    section_iterator SecI = unwrapOrError(Sym.getSection(), Obj->getFileName());
    if (SecI == Obj->section_end())
      continue;
    SymbolRef::Type Type = unwrapOrError(Sym.getType(), Obj->getFileName());

    // FIXME: Data flag
    if (*SecI == *OD.Section && Type == SymbolRef::ST_Function) {
      uint64_t Address = unwrapOrError(Sym.getAddress(), Obj->getFileName());
      StringRef Name = unwrapOrError(Sym.getName(), Obj->getFileName());
      auto NameInfo = Name.split("$group");
      LLVM_DEBUG(errs() << "- Symbol " << Name << " (" << NameInfo.first
                        << ") is at " << format("%08" PRIx32, Address) << "\n");

      // Find the group this should belong in, and store info
      for (auto &G : OD.Groups) {
        if (Address >= G.getSecond().StartAddress &&
            Address < G.getSecond().EndAddress) {
          LLVM_DEBUG(errs()
                     << " - Filing under group " << G.getFirst() << "\n");
          G.getSecond().Functions.push_back(
              {NameInfo.first, Address - G.getSecond().StartAddress});
        }
      }
    }
  }
}

static void PrintGroupInfo(OverlayData &OD) {
  outs() << "ComRV Group Info:\n\n";
  for (uint64_t Group = 0; Group < OD.GroupCount; Group++) {
    auto &G = OD.Groups[Group];
    uint64_t Size = G.EndAddress - G.StartAddress;
    outs() << "Group " << Group << " (size 0x" << format("%" PRIx16, Size)
           << "):\n";
    for (auto &F : G.Functions)
      outs() << "  " << F.Name << " (offset 0x" << format("%" PRIx32, F.Offset)
             << ")\n";
    outs() << "  CRC: " << format("%04" PRIx16, G.CRC) << "\n";
  }
}

void llvm::printComRVOverlayData(const ObjectFile *Obj) {
  if (!Obj->isELF()) {
    WithColor::error(errs(), "llvm-objdump")
        << "This operation is only supported for ELF object files.\n";
    return;
  }

  // Find the .ovlgrpdata section
  const SectionRef *GrpData = nullptr;
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name = unwrapOrError(Section.getName(), Obj->getFileName());
    if (Name == ".ovlgrpdata") {
      GrpData = &Section;
      break;
    }
  }
  if (!GrpData) {
    WithColor::error(errs(), "llvm-objdump")
        << "Could not find Overlay Group Section table.\n";
    return;
  }

  OverlayData OD;
  InitilizeGroupTables(OD, *GrpData);
  PopulateGroupSymbols(OD, Obj);
  PrintGroupInfo(OD);
}
