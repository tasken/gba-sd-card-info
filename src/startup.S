/* SPDX-License-Identifier: GPL-3.0-or-later */
.syntax unified
.cpu arm7tdmi
.arm
.section .boot, "ax", %progbits
.global _start
_start:
    b rom_start
    /* gbafix supplies the standard GBA header after linking. */
    .space 188

rom_start:
    mov r0, #0x04000000
    mov r1, #0
    str r1, [r0, #0x208]
    msr cpsr_c, #0xd2
    ldr sp, =0x03007fa0
    msr cpsr_c, #0xd3
    ldr sp, =0x03007fe0
    msr cpsr_c, #0xdf
    ldr sp, =0x03007f00

    /* Stop inherited DMA and timers before copying code out of cartridge RAM. */
    strh r1, [r0, #0xba]
    strh r1, [r0, #0xc6]
    strh r1, [r0, #0xd2]
    strh r1, [r0, #0xde]
    ldr r4, =0x04000100
    strh r1, [r4, #2]
    strh r1, [r4, #6]
    strh r1, [r4, #10]
    strh r1, [r4, #14]

    ldr r0, =__ram_load
    ldr r1, =__ram_start
    ldr r2, =__ram_end
1:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 1b
    ldr r0, =__iwram_load
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
4:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo 4b
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
2:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 2b
    ldr r0, =main
    adr lr, 3f
    bx r0
3:
    b 3b
.pool
.section .note.GNU-stack,"",%progbits
