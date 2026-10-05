/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Cartridge mode switch adapted from SuperFW src/supercard_driver.c.
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3, or (at your option) any later
 * version. This program is distributed WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See LICENSE for the GNU General Public License.
 */
#include "gba.h"
#include "transport.h"

void gba_init(void)
{
    DISPCNT = 0x0080;
    /* GBATEK: lcdcolorpalettes. Mode 4 uses black backdrop and white text. */
    REG16(0x05000000) = 0;
    REG16(0x05000002) = 0x7fff;
    REG16(0x05000004) = 31 | (24 << 5) | (8 << 10);
    REG16(0x05000006) = 31 | (12 << 5) | (12 << 10);
    REG16(0x05000008) = 12 | (26 << 5) | (31 << 10);
    REG16(0x04000004) = 0;
    REG16(0x0400000c) = 0;
    REG16(0x04000020) = 0x0100;
    REG16(0x04000022) = 0;
    REG16(0x04000024) = 0;
    REG16(0x04000026) = 0x0100;
    REG32(0x04000028) = 0;
    REG32(0x0400002c) = 0;
    REG16(0x0400004c) = 0;
    REG16(0x04000050) = 0;
    REG16(0x04000084) = 0;
    REG16(0x04000132) = 0;
    REG16(0x04000200) = 0;
    REG16(0x04000202) = 0xffff;
    /* Conservative WS0 timing, matching SuperFW's command-interface timing. */
    REG16(0x04000204) = 0x0000;
}

void gba_wait_frame(void)
{
    while (VCOUNT >= 160) {}
    while (VCOUNT < 160) {}
}

void sc_enable(void)
{
    volatile uint16_t *mode = (volatile uint16_t *)(uintptr_t)0x09fffffe;
    *mode = 0xa55a;
    *mode = 0xa55a;
    /* SDRAM mapped, SD command interface enabled, SDRAM writes disabled. */
    *mode = 0x0003;
    *mode = 0x0003;
}

void sc_timer_start(void)
{
    TIMER0_CTRL = 0;
    TIMER0_DATA = 0;
    TIMER0_CTRL = 0x0083;
}

uint16_t sc_ticks(void)
{
    return TIMER0_DATA;
}

bool sc_wait_data_idle(void)
{
    uint16_t start = sc_ticks();
    do {
        if (REG16(0x09000000) & 0x0100)
            return true;
        send_empty_clocks(1);
    } while ((uint16_t)(sc_ticks() - start) < 16384);
    return false;
}
