; RUN: opt -inline -S %s | FileCheck %s
; RUN: opt -passes='cgscc(inline)' -S %s | FileCheck %s

define riscv_overlaycall void @f() {
entry:
  tail call void @g()
  unreachable

; CHECK-LABEL: @f
; CHECK: call
}

define void @g() {
entry:
; CHECK-LABEL: @g
  unreachable
}

