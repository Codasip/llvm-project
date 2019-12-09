# RUN: llvm-mc -filetype=obj -triple=riscv64 < %s \
# RUN:     | llvm-objdump -riscv-no-aliases -d -r - \
# RUN:     | FileCheck %s

# CHECK:      lui   t5, 0
# CHECK-NEXT:   R_RISCV_OVL_HI20  a_symbol
# CHECK-NEXT: addi  t5, t5, 0
# CHECK-NEXT:   R_RISCV_OVL_LO12_I  a_symbol
# CHECK-NEXT: jalr  ra, 0(t6)
ovlcall a_symbol@overlay

# CHECK:      lui   t5, 0
# CHECK-NEXT:   R_RISCV_HI20  b_symbol
# CHECK-NEXT: addi  t5, t5, 0
# CHECK-NEXT:   R_RISCV_LO12_I  b_symbol
# CHECK-NEXT: jalr  ra, 0(t6)
ovlcall b_symbol@resident

# CHECK:      addi  t5, s0, 0
# CHECK-NEXT: jalr  ra, 0(t6)
ovlcall_indirect s0
