/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "transport.h"

static uint8_t written[528], response[16];
static unsigned used, read_index, ready_polls;
static bool busy, frozen;
static uint16_t ticks;
static unsigned raw_result;

unsigned sc_read_sector_raw(uint8_t buffer[512], uint8_t crc[8], unsigned timeout)
{
    assert(timeout == 262144);
    memset(buffer, 0xff, 512);
    const uint8_t tail[] = {0xff,0xf0,0xff,0x0f,0xf0,0xf0,0xf0,0x0f};
    memcpy(crc, tail, 8);
    return raw_result;
}
void sc_write_data_byte(uint8_t value)
{
    assert(used < sizeof(written));
    written[used++] = value;
}
uint8_t sc_data_nibble(void)
{
    assert(read_index < sizeof(response));
    return response[read_index++];
}
uint16_t sc_ticks(void)
{
    if (!frozen)
        ticks = (uint16_t)(ticks + 8192);
    return ticks;
}
bool sc_wait_write_ready(void) { ++ready_polls; return !busy; }

static void reset(unsigned token)
{
    used = read_index = ready_polls = ticks = 0;
    busy = frozen = false;
    memset(response, 15, sizeof(response));
    response[1] = 0;
    for (unsigned i = 0; i < 4; ++i)
        response[2 + i] = (token >> (3 - i)) & 1;
}

int main(void)
{
    uint8_t payload[512];
    uint8_t crc[8];
    bool captured;
    assert(sc_read_sector(payload, crc, &captured) == SD_OK && captured);
    raw_result = 1;
    assert(sc_read_sector(payload, crc, &captured) == SD_TIMEOUT && !captured);
    raw_result = 2;
    assert(sc_read_sector(payload, crc, &captured) == SD_FRAME && captured);
    memset(payload, 0xff, sizeof(payload));
    reset(5);
    assert(sc_write_block(payload) == SD_OK);
    assert(used == 524 && read_index == 6 && ready_polls == 9);
    assert(memcmp(written, "\xff\xff\xff\xf0", 4) == 0);
    assert(memcmp(written + 4, payload, 512) == 0);
    /* Each DAT lane carries 1024 ones; CRC16 = 0xEDA9 independently. */
    const uint8_t tail[] = {0xff,0xf0,0xff,0x0f,0xf0,0xf0,0xf0,0x0f};
    assert(memcmp(written + 516, tail, 8) == 0);
    reset(11); assert(sc_write_block(payload) == SD_CRC);
    reset(13); assert(sc_write_block(payload) == SD_CARD_ERROR);
    reset(5); response[0] = 0; assert(sc_write_block(payload) == SD_FRAME);
    reset(5); busy = true; assert(sc_write_block(payload) == SD_TIMEOUT);
    assert(ready_polls == 12);
    reset(5); busy = frozen = true; assert(sc_write_block(payload) == SD_TIMEOUT);
    assert(ready_polls == 524296);
    puts("Native data-write framing, CRC and busy tests passed.");
    return 0;
}
