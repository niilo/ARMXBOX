// AAPCS64 boundary witness. Never writes Android's reserved x18.
.text
.p2align 2
.global armxbox_probe_execute_abi
.hidden armxbox_probe_execute_abi
.type armxbox_probe_execute_abi, %function
// x0=context, x1=thread, x2=void callback(context,thread), x3=uint64_t checks[6].
armxbox_probe_execute_abi:
  .inst 0xd503245f // BTI c (NOP on hosts without BTI)
  sub sp, sp, #224
  stp x19, x20, [sp, #0]
  stp x21, x22, [sp, #16]
  stp x23, x24, [sp, #32]
  stp x25, x26, [sp, #48]
  stp x27, x28, [sp, #64]
  stp x29, x30, [sp, #80]
  stp d8, d9, [sp, #96]
  stp d10, d11, [sp, #112]
  stp d12, d13, [sp, #128]
  stp d14, d15, [sp, #144]
  mrs x9, fpcr
  mrs x10, fpsr
  stp x9, x10, [sp, #160]
  mov x9, sp
  stp x18, x9, [sp, #176]
  str x3, [sp, #192]
  mov x9, #0x400000 // native rounding mode deliberately differs from guest default
  msr fpcr, x9
  mov x9, #0x10
  msr fpsr, x9
.macro seed_gpr reg, value
  mov \reg, #\value
.endm
  seed_gpr x19, 0x1919
  seed_gpr x20, 0x2020
  seed_gpr x21, 0x2121
  seed_gpr x22, 0x2222
  seed_gpr x23, 0x2323
  seed_gpr x24, 0x2424
  seed_gpr x25, 0x2525
  seed_gpr x26, 0x2626
  seed_gpr x27, 0x2727
  seed_gpr x28, 0x2828
.macro seed_fpr reg, value
  mov x9, #\value
  fmov \reg, x9
.endm
  seed_fpr d8, 0xd008
  seed_fpr d9, 0xd009
  seed_fpr d10, 0xd00a
  seed_fpr d11, 0xd00b
  seed_fpr d12, 0xd00c
  seed_fpr d13, 0xd00d
  seed_fpr d14, 0xd00e
  seed_fpr d15, 0xd00f
  blr x2
  ldr x11, [sp, #192]
  ldr x10, [sp, #176]
  cmp x18, x10
  cset x9, eq
  str x9, [x11, #0]
  mov x10, sp
  ldr x12, [sp, #184]
  cmp x10, x12
  cset x9, eq
  tst x10, #15
  cset x12, eq
  and x9, x9, x12
  str x9, [x11, #8]
  mov x9, #1
.macro check_gpr reg, value
  mov x10, #\value
  cmp \reg, x10
  cset x12, eq
  and x9, x9, x12
.endm
  check_gpr x19, 0x1919
  check_gpr x20, 0x2020
  check_gpr x21, 0x2121
  check_gpr x22, 0x2222
  check_gpr x23, 0x2323
  check_gpr x24, 0x2424
  check_gpr x25, 0x2525
  check_gpr x26, 0x2626
  check_gpr x27, 0x2727
  check_gpr x28, 0x2828
  ldr x10, [sp, #80]
  cmp x29, x10
  cset x12, eq
  and x9, x9, x12
  str x9, [x11, #16]
  mov x9, #1
.macro check_fpr reg, value
  fmov x10, \reg
  mov x12, #\value
  cmp x10, x12
  cset x12, eq
  and x9, x9, x12
.endm
  check_fpr d8, 0xd008
  check_fpr d9, 0xd009
  check_fpr d10, 0xd00a
  check_fpr d11, 0xd00b
  check_fpr d12, 0xd00c
  check_fpr d13, 0xd00d
  check_fpr d14, 0xd00e
  check_fpr d15, 0xd00f
  str x9, [x11, #24]
  mrs x10, fpcr
  mov x12, #0x400000
  cmp x10, x12
  cset x9, eq
  str x9, [x11, #32]
  mrs x10, fpsr
  cmp x10, #0x10
  cset x9, eq
  str x9, [x11, #40]
  ldp x9, x10, [sp, #160]
  msr fpcr, x9
  msr fpsr, x10
  ldp d8, d9, [sp, #96]
  ldp d10, d11, [sp, #112]
  ldp d12, d13, [sp, #128]
  ldp d14, d15, [sp, #144]
  ldp x19, x20, [sp, #0]
  ldp x21, x22, [sp, #16]
  ldp x23, x24, [sp, #32]
  ldp x25, x26, [sp, #48]
  ldp x27, x28, [sp, #64]
  ldp x29, x30, [sp, #80]
  add sp, sp, #224
  ret
.size armxbox_probe_execute_abi, .-armxbox_probe_execute_abi
.section .note.GNU-stack,"",%progbits
