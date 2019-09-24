# RUN: llvm-mc -filetype=obj -triple=riscv64 < %s \
# RUN:     | llvm-objdump -riscv-no-aliases -d -r - \
# RUN:     | FileCheck %s

# CHECK:      lui a4, 0
# CHECK-NEXT:   R_RISCV_OVLPLT_HI20 foo
lui a4, %overlay_plthi(foo)

# CHECK:      addi a4, a5, 0
# CHECK-NEXT:   R_RISCV_OVLPLT_LO12_I  foo
addi a4, a5, %overlay_pltlo(foo)

