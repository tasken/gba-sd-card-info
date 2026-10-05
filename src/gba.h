/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GBA_H
#define GBA_H

#include <stdint.h>

/* GBATEK: gbaiomap, lcdiodisplaycontrol, gbatimers, gbakeypadinput. */
#define REG16(address) (*(volatile uint16_t *)(uintptr_t)(address))
#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define DISPCNT REG16(0x04000000)
#define VCOUNT REG16(0x04000006)
#define KEYINPUT REG16(0x04000130)
#define TIMER0_DATA REG16(0x04000100)
#define TIMER0_CTRL REG16(0x04000102)
#define KEY_A (1u << 0)
#define KEY_B (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_RIGHT (1u << 4)
#define KEY_LEFT (1u << 5)
#define KEY_R (1u << 8)
#define KEY_L (1u << 9)

void gba_init(void);
void gba_wait_frame(void);

#endif
