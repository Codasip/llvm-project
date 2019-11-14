; RUN: opt -inline -S %s | FileCheck %s
; RUN: opt -passes='cgscc(inline)' -S %s | FileCheck %s

define void @f() {
entry:
  tail call void @g()
  unreachable

; CHECK-LABEL: @f
; CHECK: call
}

define riscv_overlaycall void @g() {
entry:
; CHECK-LABEL: @g
  unreachable
}

