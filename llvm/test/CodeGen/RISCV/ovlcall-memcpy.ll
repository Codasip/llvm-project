; RUN: llc -mtriple riscv32-unknown-elf < %s | FileCheck %s

; Check that memcpy lowers to an ovlcall@resident call
define riscv_overlaycall void @foo(i8* %a, i8* %b, i32 %c) #0 align 4 {
entry:
  %a.addr = alloca i8*, align 4
  %b.addr = alloca i8*, align 4
  %c.addr = alloca i32, align 4
  store i8* %a, i8** %a.addr, align 4
  store i8* %b, i8** %b.addr, align 4
  store i32 %c, i32* %c.addr, align 4
  %0 = load i8*, i8** %a.addr, align 4
  %1 = load i8*, i8** %b.addr, align 4
  %2 = load i32, i32* %c.addr, align 4
  call void @llvm.memcpy.p0i8.p0i8.i32(i8* align 1 %0, i8* align 1 %1, i32 %2, i1 false)
; CHECK: ovlcall memcpy@resident
  ret void
}

declare void @llvm.memcpy.p0i8.p0i8.i32(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i32, i1 immarg) #1

attributes #0 = { noinline nounwind optsize "target-features"="+a,+c,+m,+relax,+reserve-x28,+reserve-x29,+reserve-x30,+reserve-x31" }
attributes #1 = { argmemonly nounwind willreturn }
