// The routines that -msave-restore calls (Makefile, SIZE_CFLAGS): with
// that flag a function does not save and restore ra and the s registers
// itself, but starts with "call t0, __riscv_save_N" and ends with
// "tail __riscv_restore_N" (N: how many s registers). They are normally in
// libgcc, but the riscv64-elf toolchain has no rv32 libgcc, so the
// firmware brings its own: the rv32 (ilp32, not ilp32e) part of libgcc's
// config/riscv/save-restore.S, the same instructions and the same stack
// layout, which GCC relies on.
//
// Each save allocates 16, 32, 48 or 64 bytes (the frame stays 16-byte
// aligned) and keeps ra at the top, s0 under it, and so on. It may change
// t0 (the return address) and t1, which no function expects to keep
// across its own prologue. Interrupt handlers may use them too: HPE saves
// t0 and t1 before the handler's body runs (IRQ_HANDLER, funconfig.h).
//
// Copyright (C) 2015-2025 Free Software Foundation, Inc. (libgcc)
// SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1

  .section .text.riscv_save_restore,"ax",@progbits
  .align 1

  .globl __riscv_save_12
  .globl __riscv_save_11
  .globl __riscv_save_10
  .globl __riscv_save_9
  .globl __riscv_save_8
  .globl __riscv_save_7
  .globl __riscv_save_6
  .globl __riscv_save_5
  .globl __riscv_save_4
  .globl __riscv_save_3
  .globl __riscv_save_2
  .globl __riscv_save_1
  .globl __riscv_save_0

  // Each entry takes 64 bytes and gives back (in t1, negative) what it
  // does not use
__riscv_save_12:
  li t1, 0
  addi sp, sp, -64
  sw s11, 12(sp)
  j .Ls10
__riscv_save_11:
__riscv_save_10:
__riscv_save_9:
__riscv_save_8:
  li t1, -16
  addi sp, sp, -64
.Ls10:
  sw s10, 16(sp)
  sw s9, 20(sp)
  sw s8, 24(sp)
  sw s7, 28(sp)
  j .Ls6
__riscv_save_7:
__riscv_save_6:
__riscv_save_5:
__riscv_save_4:
  li t1, -32
  addi sp, sp, -64
.Ls6:
  sw s6, 32(sp)
  sw s5, 36(sp)
  sw s4, 40(sp)
  sw s3, 44(sp)
  j .Ls2
__riscv_save_3:
__riscv_save_2:
__riscv_save_1:
__riscv_save_0:
  li t1, -48
  addi sp, sp, -64
.Ls2:
  sw s2, 48(sp)
  sw s1, 52(sp)
  sw s0, 56(sp)
  sw ra, 60(sp)
  sub sp, sp, t1
  jr t0

  .globl __riscv_restore_12
  .globl __riscv_restore_11
  .globl __riscv_restore_10
  .globl __riscv_restore_9
  .globl __riscv_restore_8
  .globl __riscv_restore_7
  .globl __riscv_restore_6
  .globl __riscv_restore_5
  .globl __riscv_restore_4
  .globl __riscv_restore_3
  .globl __riscv_restore_2
  .globl __riscv_restore_1
  .globl __riscv_restore_0

  // Restores and returns to the function's caller (ra)
__riscv_restore_12:
  lw s11, 12(sp)
  addi sp, sp, 16
__riscv_restore_11:
__riscv_restore_10:
__riscv_restore_9:
__riscv_restore_8:
  lw s10, 0(sp)
  lw s9, 4(sp)
  lw s8, 8(sp)
  lw s7, 12(sp)
  addi sp, sp, 16
__riscv_restore_7:
__riscv_restore_6:
__riscv_restore_5:
__riscv_restore_4:
  lw s6, 0(sp)
  lw s5, 4(sp)
  lw s4, 8(sp)
  lw s3, 12(sp)
  addi sp, sp, 16
__riscv_restore_3:
__riscv_restore_2:
__riscv_restore_1:
__riscv_restore_0:
  lw s2, 0(sp)
  lw s1, 4(sp)
  lw s0, 8(sp)
  lw ra, 12(sp)
  addi sp, sp, 16
  ret
