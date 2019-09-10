// RUN: %clang_cc1 -triple riscv32 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple riscv64 -fsyntax-only -verify %s

int notAFunction __attribute__((overlaycall));
// expected-warning@-1 {{'overlaycall' only applies to function types; type here is 'int'}}

void variadic(int x, ...) __attribute__((overlaycall));
// expected-error@-1 {{variadic function cannot use overlaycall calling convention}}

void multipleCC(int x) __attribute__((overlaycall)) __attribute__((cdecl));
// expected-error@-1 {{cdecl and overlaycall attributes are not compatible}}

void incompatForwardDecl(int x);
void __attribute__((overlaycall)) incompatForwardDecl(int x) {}
// expected-error@-1 {{function declared 'overlaycall' here was previously declared without calling convention}}
// expected-note@-3 {{previous declaration is here}}

void foo(int x) __attribute__((overlaycall));
void bar(int x) {
  void (*incompatFooPtr)(int) = &foo;
  // expected-warning@-1 {{incompatible function pointer types initializing 'void (*)(int)' with an expression of type 'void (*)(int) __attribute__((overlaycall))'}}
  void __attribute__((overlaycall)) (*fooPtr)(int) = &foo;
  fooPtr(x);
}
