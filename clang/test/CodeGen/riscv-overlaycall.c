// RUN: %clang_cc1 %s -triple=riscv64 -fcomrv -emit-llvm -o - \
// RUN:    | FileCheck %s

int __attribute__((overlaycall)) test_overlay_func(void) {
// CHECK-LABEL: define riscv_overlaycall{{.*}}@test_overlay_func()
  return 5;
}

int test_call_overlay_func(void) {
// CHECK-LABEL: test_call_overlay_func
// CHECK: call riscv_overlaycall{{.*}}@test_overlay_func()
  return test_overlay_func();
}
