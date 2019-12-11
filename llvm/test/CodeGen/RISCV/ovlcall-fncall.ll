; RUN: llc -mtriple riscv32-unknown-elf -filetype=obj < %s\
; RUN: | llvm-objdump -d -r -M no-aliases -M numeric - | FileCheck %s

; Test for overlay system function call instructions/relocations

@indirect_foo = dso_local global i32 (...)* bitcast (i32 ()* @foo to i32 (...)*), align 4
@indirect_bar = dso_local global i32 (...)* bitcast (i32 ()* @bar to i32 (...)*), align 4

define dso_local i32 @foo() #0 {
entry:
  ret i32 0
}

define dso_local riscv_overlaycall i32 @bar() #0 align 4 {
entry:
  ret i32 0
}

define dso_local i32 @normal_to_normal() #0 {
entry:
; CHECK-LABEL: normal_to_normal:
; CHECK:      auipc
; (no reloc check because assembler will have resolved this)
  %call = call i32 @foo()
  ret i32 0
}

define dso_local i32 @normal_to_ovl() #0 {
entry:
; CHECK-LABEL: normal_to_ovl:
; CHECK:      lui x30, 0
; CHECK-NEXT:   R_RISCV_OVL_HI20
; CHECK-NEXT: addi x30, x30
; CHECK-NEXT:   R_RISCV_OVL_LO12_I
; CHECK-NEXT: jalr x1, 0(x31)
  %call = call riscv_overlaycall i32 @bar()
  ret i32 0
}

define dso_local i32 @normal_to_normal_indirect() #0 {
entry:
; CHECK-LABEL: normal_to_normal_indirect:
; CHECK:      lui x[[REG:[0-9]+]], 0
; CHECK-NEXT:   R_RISCV_HI20 indirect_foo
; CHECK-NEXT: lw x[[REG]], 0(x[[REG]])
; CHECK-NEXT:   R_RISCV_LO12_I indirect_foo
; CHECK-NEXT: jalr x1, 0(x[[REG]])
  %0 = load i32 (...)*, i32 (...)** @indirect_foo, align 4
  %callee.knr.cast = bitcast i32 (...)* %0 to i32 ()*
  %call = call i32 %callee.knr.cast()
  ret i32 0
}

define dso_local i32 @normal_to_ovl_indirect() #0 {
entry:
; CHECK-LABEL: normal_to_ovl_indirect:
; CHECK:      lui x[[REG:[0-9]+]], 0
; CHECK-NEXT:   R_RISCV_HI20 indirect_bar
; CHECK-NEXT: lw x[[REG]], 0(x[[REG]])
; CHECK-NEXT:   R_RISCV_LO12_I indirect_bar
; CHECK-NEXT: jalr x1, 0(x[[REG]])
  %0 = load i32 (...)*, i32 (...)** @indirect_bar, align 4
  %callee.knr.cast = bitcast i32 (...)* %0 to i32 ()*
  %call = call riscv_overlaycall i32 %callee.knr.cast()
  ret i32 0
}

define dso_local riscv_overlaycall i32 @ovl_to_normal() #0 align 4 {
entry:
; CHECK-LABEL: ovl_to_normal:
; CHECK:      lui x30, 0
; CHECK-NEXT:   R_RISCV_HI20 foo
; CHECK-NEXT: addi x30, x30, 0
; CHECK-NEXT:   R_RISCV_LO12_I foo
; CHECK-NEXT: jalr x1, 0(x31)
  %call = call i32 @foo()
  ret i32 0
}

define dso_local riscv_overlaycall i32 @ovl_to_ovl() #0 align 4 {
entry:
; CHECK-LABEL: ovl_to_ovl:
; CHECK:      lui x30, 0
; CHECK-NEXT:   R_RISCV_OVL_HI20 bar
; CHECK-NEXT: addi x30, x30, 0
; CHECK-NEXT:   R_RISCV_OVL_LO12_I bar
; CHECK-NEXT: jalr x1, 0(x31)
  %call = call riscv_overlaycall i32 @bar()
  ret i32 0
}

define dso_local riscv_overlaycall i32 @ovl_to_normal_indirect() #0 align 4 {
entry:
; NOTE: Since x30 is reserved, assumes that the value is materialised then moved
; CHECK-LABEL: ovl_to_normal_indirect:
; CHECK:      lui x[[REG:[0-9]+]], 0
; CHECK-NEXT:   R_RISCV_HI20 indirect_foo
; CHECK-NEXT: lw x[[REG]], 0(x[[REG]])
; CHECK-NEXT:   R_RISCV_LO12_I indirect_foo
; CHECK-NEXT: addi x30, x[[REG]], 0
; CHECK-NEXT: jalr x1, 0(x31)
  %0 = load i32 (...)*, i32 (...)** @indirect_foo, align 4
  %callee.knr.cast = bitcast i32 (...)* %0 to i32 ()*
  %call = call i32 %callee.knr.cast()
  ret i32 0
}

define dso_local riscv_overlaycall i32 @ovl_to_ovl_indirect() #0 align 4 {
entry:
; NOTE: Since x30 is reserved, assumes that the value is materialised then moved
; CHECK-LABEL: ovl_to_ovl_indirect:
; CHECK:      lui x[[REG:[0-9]+]], 0
; CHECK-NEXT:   R_RISCV_HI20 indirect_bar
; CHECK-NEXT: lw x[[REG]], 0(x[[REG]])
; CHECK-NEXT:   R_RISCV_LO12_I indirect_bar
; CHECK-NEXT: addi x30, x[[REG]], 0
; CHECK-NEXT: jalr x1, 0(x31)
  %0 = load i32 (...)*, i32 (...)** @indirect_bar, align 4
  %callee.knr.cast = bitcast i32 (...)* %0 to i32 ()*
  %call = call riscv_overlaycall i32 %callee.knr.cast()
  ret i32 0
}

attributes #0 = { noinline nounwind optnone "target-features"="+reserve-x28,+reserve-x29,+reserve-x30,+reserve-x31,-relax" }
