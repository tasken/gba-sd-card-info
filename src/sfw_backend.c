/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gba.h"
#include "sfw_backend.h"
#include "superfw/supercard_driver.h"

bool isgba = true;
bool slowsd = true;
static uint16_t saved_waitcnt;
static bool active;
bool wait_dat0_idle(unsigned timeout);

unsigned sfw_initialize(uint32_t *sectors, bool *block_addressed)
{
    if (!active)
        saved_waitcnt = REG16(0x04000204);
    active = true;
    /* SuperFW v0.21 main_gba: WS0 default, WS1 fast, prefetch enabled. */
    REG16(0x04000204) = 0x40c0;
    set_supercard_mode(MAPPED_SDRAM, true, true);
    t_card_info card = {0};
    unsigned result = sdcard_init(&card);
    *sectors = card.block_cnt;
    *block_addressed = card.sdhc;
    return result;
}

unsigned sfw_read(uint8_t *buffer, uint32_t sector, unsigned count)
{
    return sdcard_read_blocks(buffer, sector, count);
}

unsigned sfw_write(const uint8_t *buffer, uint32_t sector, unsigned count)
{
    return sdcard_write_blocks(buffer, sector, count);
}

bool sfw_sync(void)
{
    return wait_dat0_idle(0x200000);
}

void sfw_finish(void)
{
    if (!active)
        return;
    set_supercard_mode(MAPPED_SDRAM, false, true);
    REG16(0x04000204) = saved_waitcnt;
    active = false;
}
