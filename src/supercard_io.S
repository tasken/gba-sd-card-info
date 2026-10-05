/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Command transport subset of SuperFW src/supercard_io.S at
 * 1abd49bc18570956369283cbb724fbcf13e9a803.
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3, or (at your option) any later
 * version. This program is distributed WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See LICENSE for the GNU General Public License.
 */
.syntax unified
.cpu arm7tdmi
.thumb
.text
.align 2

/* At least 60 CPU cycles after a single-clock access, even in IWRAM.
 * Word data writes generate two bus accesses before this delay.
 */
.macro clock_delay
    movs r7, #12
9:
    subs r7, #1
    bne 9b
.endm

.global send_empty_clocks
.type send_empty_clocks, %function
.thumb_func
send_empty_clocks:
    push {r7}
    cmp r0, #0
    beq 2f
    ldr r1, =0x09800000
1:
    ldrh r2, [r1]
    clock_delay
    subs r0, #1
    bne 1b
2:
    pop {r7}
    bx lr
.size send_empty_clocks, .-send_empty_clocks

.global wait_sdcard_idle
.type wait_sdcard_idle, %function
.thumb_func
wait_sdcard_idle:
    push {r7}
    ldr r1, =0x09800000
    movs r3, r0
    movs r0, #0
    cmp r3, #0
    beq 3f
1:
    ldrh r2, [r1]
    clock_delay
    lsls r2, #31
    bne 2f
    subs r3, #1
    bne 1b
    b 3f
2:
    movs r0, #1
3:
    pop {r7}
    bx lr
.size wait_sdcard_idle, .-wait_sdcard_idle

.global receive_sdcard_response
.type receive_sdcard_response, %function
.thumb_func
receive_sdcard_response:
    push {r4-r7}
    cmp r1, #0
    beq 6f
    cmp r2, #0
    beq 6f
    ldr r4, =0x09800000
    movs r6, #7
    movs r5, #0
1:
    ldrh r3, [r4]
    clock_delay
    lsrs r3, #1
    bcc 4f
    subs r2, #1
    bne 1b
6:
    movs r0, #0
    b 7f
3:
    movs r6, #8
4:
    ldrh r3, [r4]
    clock_delay
    lsrs r3, #1
    adcs r5, r5
    subs r6, #1
    bne 4b
    strb r5, [r0]
    adds r0, #1
    subs r1, #1
    bne 3b
    movs r0, #1
7:
    pop {r4-r7}
    bx lr
.size receive_sdcard_response, .-receive_sdcard_response

.global send_sdcard_commandbuf
.type send_sdcard_commandbuf, %function
.thumb_func
send_sdcard_commandbuf:
    push {r4, r7}
    cmp r1, #0
    beq 3f
    ldr r3, =0x09800000
1:
    ldrb r2, [r0]
    adds r0, #1
    movs r4, #8
2:
    strh r2, [r3]
    clock_delay
    lsls r2, #1
    subs r4, #1
    bne 2b
    subs r1, #1
    bne 1b
3:
    pop {r4, r7}
    bx lr
.size send_sdcard_commandbuf, .-send_sdcard_commandbuf

/* SuperChis superchis.vhd: DAT[3:0] is sampled into bits 11:8
 * during a halfword read of the DAT read interface.
 */
.global sc_data_nibble
.type sc_data_nibble, %function
.thumb_func
sc_data_nibble:
    push {r7}
    ldr r1, =0x09100000
    ldrh r0, [r1]
    clock_delay
    lsrs r0, #8
    movs r1, #15
    ands r0, r1
    pop {r7}
    bx lr
.size sc_data_nibble, .-sc_data_nibble

/* SuperFW's word-write interface clocks the high then low nibble.
 * SuperChis superchis.vhd latches GP[7:4] and GP[3:0] at address load.
 * GBATEK gbasystemcontrol: a word is two halfword bus transfers; the
 * delays below do not stretch the interval within that pair.
 */
.global sc_write_data_byte
.type sc_write_data_byte, %function
.thumb_func
sc_write_data_byte:
    push {r7}
    ldr r1, =0x09000000
    str r0, [r1]
    clock_delay
    clock_delay
    pop {r7}
    bx lr
.size sc_write_data_byte, .-sc_write_data_byte

.global sc_wait_write_ready
.type sc_wait_write_ready, %function
.thumb_func
sc_wait_write_ready:
    push {r7}
    ldr r1, =0x09000000
    ldrh r0, [r1]
    clock_delay
    lsrs r0, #8
    movs r1, #1
    ands r0, r1
    pop {r7}
    bx lr
.size sc_wait_write_ready, .-sc_wait_write_ready
.pool

/* SuperFW SuperChis/Lite unaligned sector-read path, reduced to one block.
 * Preserve its start polling, discard access and packed-data access sequence.
 * Return 0 for a complete frame, 1 for timeout, 2 for an invalid end nibble.
 */
.arm
.align 2
.global sc_read_sector_raw
.type sc_read_sector_raw, %function
sc_read_sector_raw:
    push {r4-r7}
    mov r7, r1
    ldr r4, =0x09100000
    ldr r5, =0x09200000
1:
    subs r2, r2, #1
    beq 4f
    ldrh r3, [r4]
    tst r3, #0x0100
    bne 1b
    ldrh r2, [r5]
    mov r6, #64
2:
    ldmia r5, {r2, r3}
    strb r2, [r0], #1
    lsr r2, r2, #8
    strb r2, [r0], #1
    lsr r2, r2, #8
    strb r2, [r0], #1
    lsr r2, r2, #8
    strb r2, [r0], #1
    strb r3, [r0], #1
    lsr r3, r3, #8
    strb r3, [r0], #1
    lsr r3, r3, #8
    strb r3, [r0], #1
    lsr r3, r3, #8
    strb r3, [r0], #1
    subs r6, r6, #1
    bne 2b
    ldmia r5, {r2, r3}
    strb r2, [r7], #1
    lsr r2, r2, #8
    strb r2, [r7], #1
    lsr r2, r2, #8
    strb r2, [r7], #1
    lsr r2, r2, #8
    strb r2, [r7], #1
    strb r3, [r7], #1
    lsr r3, r3, #8
    strb r3, [r7], #1
    lsr r3, r3, #8
    strb r3, [r7], #1
    lsr r3, r3, #8
    strb r3, [r7], #1
    ldrh r2, [r4]
    and r2, r2, #0x0f00
    cmp r2, #0x0f00
    moveq r0, #0
    movne r0, #2
    b 5f
4:
    mov r0, #1
5:
    pop {r4-r7}
    bx lr
.size sc_read_sector_raw, .-sc_read_sector_raw
.pool
.section .note.GNU-stack,"",%progbits
