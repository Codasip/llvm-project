; RUN: llc -mtriple=riscv32 -verify-machineinstrs < %s | FileCheck %s

; Test for overlay functions being placed into the correct sections

; foo is an overlay function so should be placed in .text.ovlfn.foo
define dso_local riscv_overlaycall i32 @foo() {
entry:
; CHECK: .section .text.ovlfn.foo
; CHECK-NEXT: .globl foo
; CHECK: foo:
; CHECK: .size foo
  ret i32 0
}

; bar is not an overlay function so should be placed in .text
define dso_local i32 @bar() {
entry:
; CHECK: .text
; CHECK-NEXT: .globl bar
; CHECK: bar:
; CHECK: .size bar
  ret i32 1
}
