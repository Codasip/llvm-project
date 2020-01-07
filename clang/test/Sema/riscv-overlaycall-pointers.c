// RUN: %clang_cc1 %s -triple riscv32-unknown-elf -verify -fsyntax-only -fcomrv
// RUN: %clang_cc1 %s -triple riscv64-unknown-elf -verify -fsyntax-only -fcomrv

// Test that casting pointers between overlaycall/CCC does not produce a warning

// expected-no-diagnostics

void A(void) __attribute__((overlaycall));

void B(void) {}

void AA(void (*x)(void));

void BB(void (*x)(void) __attribute__((overlaycall)));


void test() {
  AA(&A);
  AA(&B);
  BB(&A);
  BB(&B);
}
