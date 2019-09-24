//===-- RISCVMCCodeEmitter.cpp - Convert RISCV code to machine code -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RISCVMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVFixupKinds.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "Utils/RISCVBaseInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

STATISTIC(MCNumEmitted, "Number of MC instructions emitted");
STATISTIC(MCNumFixups, "Number of MC fixups created");

namespace {
class RISCVMCCodeEmitter : public MCCodeEmitter {
  RISCVMCCodeEmitter(const RISCVMCCodeEmitter &) = delete;
  void operator=(const RISCVMCCodeEmitter &) = delete;
  MCContext &Ctx;
  MCInstrInfo const &MCII;

public:
  RISCVMCCodeEmitter(MCContext &ctx, MCInstrInfo const &MCII)
      : Ctx(ctx), MCII(MCII) {}

  ~RISCVMCCodeEmitter() override {}

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  void expandFunctionCall(const MCInst &MI, raw_ostream &OS,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  unsigned expandFunctionCallOverlay(const MCInst &MI, raw_ostream &OS,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const;

  void expandAddTPRel(const MCInst &MI, raw_ostream &OS,
                      SmallVectorImpl<MCFixup> &Fixups,
                      const MCSubtargetInfo &STI) const;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Return binary encoding of operand. If the machine operand requires
  /// relocation, record the relocation and return zero.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  unsigned getImmOpValue(const MCInst &MI, unsigned OpNo,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const;
};
} // end anonymous namespace

MCCodeEmitter *llvm::createRISCVMCCodeEmitter(const MCInstrInfo &MCII,
                                              const MCRegisterInfo &MRI,
                                              MCContext &Ctx) {
  return new RISCVMCCodeEmitter(Ctx, MCII);
}

// Expand PseudoCALL(Reg), PseudoTAIL and PseudoJump to AUIPC and JALR with
// relocation types. We expand those pseudo-instructions while encoding them,
// meaning AUIPC and JALR won't go through RISCV MC to MC compressed
// instruction transformation. This is acceptable because AUIPC has no 16-bit
// form and C_JALR has no immediate operand field.  We let linker relaxation
// deal with it. When linker relaxation is enabled, AUIPC and JALR have a
// chance to relax to JAL.
// If the C extension is enabled, JAL has a chance relax to C_JAL.
void RISCVMCCodeEmitter::expandFunctionCall(const MCInst &MI, raw_ostream &OS,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  MCInst TmpInst;
  MCOperand Func;
  Register Ra;
  if (MI.getOpcode() == RISCV::PseudoTAIL) {
    Func = MI.getOperand(0);
    Ra = RISCV::X6;
  } else if (MI.getOpcode() == RISCV::PseudoCALLReg) {
    Func = MI.getOperand(1);
    Ra = MI.getOperand(0).getReg();
  } else if (MI.getOpcode() == RISCV::PseudoCALL) {
    Func = MI.getOperand(0);
    Ra = RISCV::X1;
  } else if (MI.getOpcode() == RISCV::PseudoJump) {
    Func = MI.getOperand(1);
    Ra = MI.getOperand(0).getReg();
  }
  uint32_t Binary;

  assert(Func.isExpr() && "Expected expression");

  const MCExpr *CallExpr = Func.getExpr();

  // Emit AUIPC Ra, Func with R_RISCV_CALL relocation type.
  TmpInst = MCInstBuilder(RISCV::AUIPC)
                .addReg(Ra)
                .addOperand(MCOperand::createExpr(CallExpr));
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(OS, Binary, support::little);

  if (MI.getOpcode() == RISCV::PseudoTAIL ||
      MI.getOpcode() == RISCV::PseudoJump)
    // Emit JALR X0, Ra, 0
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(RISCV::X0).addReg(Ra).addImm(0);
  else
    // Emit JALR Ra, Ra, 0
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(Ra).addReg(Ra).addImm(0);
  Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(OS, Binary, support::little);
}

unsigned
RISCVMCCodeEmitter::expandFunctionCallOverlay(const MCInst &MI, raw_ostream &OS,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              const MCSubtargetInfo &STI) const {
  MCInst TmpInst;
  uint32_t Binary;
  bool Relax = STI.getFeatureBits()[RISCV::FeatureRelax];

  unsigned Opcode = MI.getOpcode();
  unsigned NumInstrs = 0;
  if (Opcode == RISCV::PseudoOVLCALL ||
      Opcode == RISCV::PseudoOVLCALLIndirectFromOverlay) {
    // OVLCALL -> LUI ; ADDI ; JALR
    // Note: Fixups are added manually since they are needed at non-zero offsets
    Register Ra = RISCV::X1;
    Register Token = RISCV::X30;
    Register EntryPoint = RISCV::X31;

    if (Opcode == RISCV::PseudoOVLCALL) {
      // Extract the call symbol that should be targetted by this call.
      assert(MI.getOperand(0).isExpr() && "Something has gone wrong?");
      const RISCVMCExpr *RCallExpr = cast<RISCVMCExpr>(MI.getOperand(0).getExpr());
      assert(RCallExpr->getSubExpr()->getKind() == MCExpr::SymbolRef);
      const MCSymbolRefExpr *Sym = cast<MCSymbolRefExpr>(RCallExpr->getSubExpr());
      bool isOVL = RCallExpr->getKind() == RISCVMCExpr::VK_RISCV_OVLCALL;
      if (!isOVL)
        assert(RCallExpr->getKind() == RISCVMCExpr::VK_RISCV_OVL2RESCALL);

      // LUI
      TmpInst = MCInstBuilder(RISCV::LUI).addReg(Token).addImm(0);
      Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
      support::endian::write(OS, Binary, support::little);
      NumInstrs++;
      Fixups.push_back(MCFixup::create(0, Sym,
                                       isOVL ? MCFixupKind(RISCV::fixup_riscv_ovl_hi20)
                                             : MCFixupKind(RISCV::fixup_riscv_hi20),
                                       MI.getLoc()));
      if (Relax)
        Fixups.push_back(MCFixup::create(0, MCConstantExpr::create(0, Ctx),
                                         MCFixupKind(RISCV::fixup_riscv_relax),
                                         MI.getLoc()));
      // ADDI
      TmpInst = MCInstBuilder(RISCV::ADDI).addReg(Token).addReg(Token).addImm(0);
      Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
      support::endian::write(OS, Binary, support::little);
      NumInstrs++;
      Fixups.push_back(MCFixup::create(4, Sym,
                                       isOVL ? MCFixupKind(RISCV::fixup_riscv_ovl_lo12_i)
                                             : MCFixupKind(RISCV::fixup_riscv_lo12_i),
                                       MI.getLoc()));
      if (Relax)
        Fixups.push_back(MCFixup::create(4, MCConstantExpr::create(0, Ctx),
                                         MCFixupKind(RISCV::fixup_riscv_relax),
                                         MI.getLoc()));
    } else {
      assert(Opcode == RISCV::PseudoOVLCALLIndirectFromOverlay);
      Register TargetReg = MI.getOperand(0).getReg();
      // MV
      TmpInst = MCInstBuilder(RISCV::ADDI).addReg(Token).addReg(TargetReg).addImm(0);
      Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
      support::endian::write(OS, Binary, support::little);
      NumInstrs++;
    }

    // JALR
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(Ra).addReg(EntryPoint).addImm(0);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;
  } else if (MI.getOpcode() == RISCV::PseudoOVLCALLIndirectFromResident) {
    Register Ra = RISCV::X1;
    Register Token = RISCV::X30;
    Register EntryPoint = RISCV::X31;

    // Check the low bit of the target register to determine whether the
    // call needs to go via the overlay system.
    // Use x30 as a scratch register
    unsigned TargetReg = MI.getOperand(0).getReg();
    // andi x30, <target>, 1
    TmpInst = MCInstBuilder(RISCV::ANDI).addReg(Token).addReg(TargetReg).addImm(1);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;
    // beqz x30, end
    TmpInst = MCInstBuilder(RISCV::BEQ).addReg(Token).addReg(RISCV::X0).addImm(0);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;

    // FIXME: How to refer to a symbol here, instead of using a constant
    Fixups.push_back(MCFixup::create(4, MCConstantExpr::create(8, Ctx),
                                     MCFixupKind(RISCV::fixup_riscv_branch),
                                     MI.getLoc()));
    if (Relax)
      Fixups.push_back(MCFixup::create(4, MCConstantExpr::create(0, Ctx),
                                       MCFixupKind(RISCV::fixup_riscv_relax),
                                       MI.getLoc()));

    // If it's not equal, then call into the overlay system
    // mv x30, <target>
    TmpInst = MCInstBuilder(RISCV::ADDI).addReg(Token).addReg(TargetReg).addImm(0);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;
    // mv <target>, x31
    TmpInst = MCInstBuilder(RISCV::ADDI).addReg(TargetReg).addReg(EntryPoint).addImm(0);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;

    // jalr ra, <target>
    TmpInst = MCInstBuilder(RISCV::JALR).addReg(Ra).addReg(TargetReg).addImm(0);
    Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
    support::endian::write(OS, Binary, support::little);
    NumInstrs++;
  } else
    llvm_unreachable("Unexpected opcode!");

  return NumInstrs;
}

// Expand PseudoAddTPRel to a simple ADD with the correct relocation.
void RISCVMCCodeEmitter::expandAddTPRel(const MCInst &MI, raw_ostream &OS,
                                        SmallVectorImpl<MCFixup> &Fixups,
                                        const MCSubtargetInfo &STI) const {
  MCOperand DestReg = MI.getOperand(0);
  MCOperand SrcReg = MI.getOperand(1);
  MCOperand TPReg = MI.getOperand(2);
  assert(TPReg.isReg() && TPReg.getReg() == RISCV::X4 &&
         "Expected thread pointer as second input to TP-relative add");

  MCOperand SrcSymbol = MI.getOperand(3);
  assert(SrcSymbol.isExpr() &&
         "Expected expression as third input to TP-relative add");

  const RISCVMCExpr *Expr = dyn_cast<RISCVMCExpr>(SrcSymbol.getExpr());
  assert(Expr && Expr->getKind() == RISCVMCExpr::VK_RISCV_TPREL_ADD &&
         "Expected tprel_add relocation on TP-relative symbol");

  // Emit the correct tprel_add relocation for the symbol.
  Fixups.push_back(MCFixup::create(
      0, Expr, MCFixupKind(RISCV::fixup_riscv_tprel_add), MI.getLoc()));

  // Emit fixup_riscv_relax for tprel_add where the relax feature is enabled.
  if (STI.getFeatureBits()[RISCV::FeatureRelax]) {
    const MCConstantExpr *Dummy = MCConstantExpr::create(0, Ctx);
    Fixups.push_back(MCFixup::create(
        0, Dummy, MCFixupKind(RISCV::fixup_riscv_relax), MI.getLoc()));
  }

  // Emit a normal ADD instruction with the given operands.
  MCInst TmpInst = MCInstBuilder(RISCV::ADD)
                       .addOperand(DestReg)
                       .addOperand(SrcReg)
                       .addOperand(TPReg);
  uint32_t Binary = getBinaryCodeForInstr(TmpInst, Fixups, STI);
  support::endian::write(OS, Binary, support::little);
}

void RISCVMCCodeEmitter::encodeInstruction(const MCInst &MI, raw_ostream &OS,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  // Get byte count of instruction.
  unsigned Size = Desc.getSize();

  if (MI.getOpcode() == RISCV::PseudoCALLReg ||
      MI.getOpcode() == RISCV::PseudoCALL ||
      MI.getOpcode() == RISCV::PseudoTAIL ||
      MI.getOpcode() == RISCV::PseudoJump) {
    expandFunctionCall(MI, OS, Fixups, STI);
    MCNumEmitted += 2;
    return;
  }

  if (MI.getOpcode() == RISCV::PseudoOVLCALL ||
      MI.getOpcode() == RISCV::PseudoOVLCALLIndirectFromOverlay ||
      MI.getOpcode() == RISCV::PseudoOVLCALLIndirectFromResident) {
    MCNumEmitted += expandFunctionCallOverlay(MI, OS, Fixups, STI);
    return;
  }

  if (MI.getOpcode() == RISCV::PseudoAddTPRel) {
    expandAddTPRel(MI, OS, Fixups, STI);
    MCNumEmitted += 1;
    return;
  }

  switch (Size) {
  default:
    llvm_unreachable("Unhandled encodeInstruction length!");
  case 2: {
    uint16_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write<uint16_t>(OS, Bits, support::little);
    break;
  }
  case 4: {
    uint32_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);
    support::endian::write(OS, Bits, support::little);
    break;
  }
  }

  ++MCNumEmitted; // Keep track of the # of mi's emitted.
}

unsigned
RISCVMCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {

  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  llvm_unreachable("Unhandled expression!");
  return 0;
}

unsigned
RISCVMCCodeEmitter::getImmOpValueAsr1(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);

  if (MO.isImm()) {
    unsigned Res = MO.getImm();
    assert((Res & 1) == 0 && "LSB is non-zero");
    return Res >> 1;
  }

  return getImmOpValue(MI, OpNo, Fixups, STI);
}

unsigned RISCVMCCodeEmitter::getImmOpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  bool EnableRelax = STI.getFeatureBits()[RISCV::FeatureRelax];
  const MCOperand &MO = MI.getOperand(OpNo);

  MCInstrDesc const &Desc = MCII.get(MI.getOpcode());
  unsigned MIFrm = Desc.TSFlags & RISCVII::InstFormatMask;

  // If the destination is an immediate, there is nothing to do.
  if (MO.isImm())
    return MO.getImm();

  assert(MO.isExpr() &&
         "getImmOpValue expects only expressions or immediates");
  const MCExpr *Expr = MO.getExpr();
  MCExpr::ExprKind Kind = Expr->getKind();
  RISCV::Fixups FixupKind = RISCV::fixup_riscv_invalid;
  bool RelaxCandidate = false;
  if (Kind == MCExpr::Target) {
    const RISCVMCExpr *RVExpr = cast<RISCVMCExpr>(Expr);

    switch (RVExpr->getKind()) {
    case RISCVMCExpr::VK_RISCV_None:
    case RISCVMCExpr::VK_RISCV_Invalid:
    case RISCVMCExpr::VK_RISCV_32_PCREL:
      llvm_unreachable("Unhandled fixup kind!");
    case RISCVMCExpr::VK_RISCV_OVLCALL:
    case RISCVMCExpr::VK_RISCV_OVL2RESCALL:
      // Overlay calls are expanded into multiple instructions with different
      // fixups depending on the instruction sequence, so they aren't
      // handled here.
      return 0;
    case RISCVMCExpr::VK_RISCV_TPREL_ADD:
      // tprel_add is only used to indicate that a relocation should be emitted
      // for an add instruction used in TP-relative addressing. It should not be
      // expanded as if representing an actual instruction operand and so to
      // encounter it here is an error.
      llvm_unreachable(
          "VK_RISCV_TPREL_ADD should not represent an instruction operand");
    case RISCVMCExpr::VK_RISCV_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_lo12_s;
      else
        llvm_unreachable("VK_RISCV_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_HI:
      FixupKind = RISCV::fixup_riscv_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_PCREL_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_pcrel_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_pcrel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCV_PCREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_PCREL_HI:
      FixupKind = RISCV::fixup_riscv_pcrel_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_GOT_HI:
      FixupKind = RISCV::fixup_riscv_got_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_TPREL_LO:
      if (MIFrm == RISCVII::InstFormatI)
        FixupKind = RISCV::fixup_riscv_tprel_lo12_i;
      else if (MIFrm == RISCVII::InstFormatS)
        FixupKind = RISCV::fixup_riscv_tprel_lo12_s;
      else
        llvm_unreachable(
            "VK_RISCV_TPREL_LO used with unexpected instruction format");
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_TPREL_HI:
      FixupKind = RISCV::fixup_riscv_tprel_hi20;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_TLS_GOT_HI:
      FixupKind = RISCV::fixup_riscv_tls_got_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_TLS_GD_HI:
      FixupKind = RISCV::fixup_riscv_tls_gd_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_CALL:
      FixupKind = RISCV::fixup_riscv_call;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_CALL_PLT:
      FixupKind = RISCV::fixup_riscv_call_plt;
      RelaxCandidate = true;
      break;
    case RISCVMCExpr::VK_RISCV_OVL_LO:
      FixupKind = RISCV::fixup_riscv_ovl_lo12_i;
      break;
    case RISCVMCExpr::VK_RISCV_OVL_HI:
      FixupKind = RISCV::fixup_riscv_ovl_hi20;
      break;
    case RISCVMCExpr::VK_RISCV_OVLPLT_LO:
      FixupKind = RISCV::fixup_riscv_ovlplt_lo12_i;
      break;
    case RISCVMCExpr::VK_RISCV_OVLPLT_HI:
      FixupKind = RISCV::fixup_riscv_ovlplt_hi20;
      break;
    }
  } else if (Kind == MCExpr::SymbolRef &&
             cast<MCSymbolRefExpr>(Expr)->getKind() == MCSymbolRefExpr::VK_None) {
    if (Desc.getOpcode() == RISCV::JAL) {
      FixupKind = RISCV::fixup_riscv_jal;
    } else if (MIFrm == RISCVII::InstFormatB) {
      FixupKind = RISCV::fixup_riscv_branch;
    } else if (MIFrm == RISCVII::InstFormatCJ) {
      FixupKind = RISCV::fixup_riscv_rvc_jump;
    } else if (MIFrm == RISCVII::InstFormatCB) {
      FixupKind = RISCV::fixup_riscv_rvc_branch;
    }
  }

  assert(FixupKind != RISCV::fixup_riscv_invalid && "Unhandled expression!");

  Fixups.push_back(
      MCFixup::create(0, Expr, MCFixupKind(FixupKind), MI.getLoc()));
  ++MCNumFixups;

  // Ensure an R_RISCV_RELAX relocation will be emitted if linker relaxation is
  // enabled and the current fixup will result in a relocation that may be
  // relaxed.
  if (EnableRelax && RelaxCandidate) {
    const MCConstantExpr *Dummy = MCConstantExpr::create(0, Ctx);
    Fixups.push_back(
    MCFixup::create(0, Dummy, MCFixupKind(RISCV::fixup_riscv_relax),
                    MI.getLoc()));
    ++MCNumFixups;
  }

  return 0;
}

#include "RISCVGenMCCodeEmitter.inc"
