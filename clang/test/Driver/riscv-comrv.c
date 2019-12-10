// Check ComRV Driver Arguments
//
// REQUIRES: riscv-registered-target
//
// RUN: %clang -target riscv32-unknown-elf -fcomrv \
// RUN:   -march=rv32if -mabi=ilp32f -### -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=INVALID-ABI %s
// INVALID-ABI: invalid ABI 'ilp32f' when using -fcomrv
