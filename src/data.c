/* SPDX-License-Identifier: GPL-3.0-or-later
 * Write framing adapted from SuperFW supercard_io.S at
 * 1abd49bc18570956369283cbb724fbcf13e9a803.
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 */
#include "transport.h"

SdError sc_read_sector(uint8_t buffer[512], uint8_t crc[8], bool *captured)
{
    unsigned result = sc_read_sector_raw(buffer, crc, 262144);
    *captured = result != 1;
    if (result == 1)
        return SD_TIMEOUT;
    if (result != 0)
        return SD_FRAME;
    return sd_check_data(buffer, 512, crc);
}

SdError sc_read_register(uint8_t *buffer, unsigned size, uint8_t crc[8],
                         bool *captured)
{
    *captured = false;
    uint16_t start = sc_ticks();
    bool found = false;
    for (unsigned poll = 0; poll < 262144; ++poll) {
        uint8_t nibble = sc_data_nibble();
        if (!(nibble & 1)) {
            if (nibble != 0)
                return SD_FRAME;
            found = true;
            break;
        }

        if ((uint16_t)(sc_ticks() - start) >= 16384)
            return SD_TIMEOUT;
    }
    if (!found)
        return SD_TIMEOUT;
    for (unsigned i = 0; i < size; ++i) {
        uint8_t high = sc_data_nibble();
        buffer[i] = (uint8_t)((high << 4) | sc_data_nibble());
    }
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t high = sc_data_nibble();
        crc[i] = (uint8_t)((high << 4) | sc_data_nibble());
    }
    *captured = true;
    if (sc_data_nibble() != 15)
        return SD_FRAME;
    return sd_check_data(buffer, size, crc);
}

SdError sc_write_block(const uint8_t buffer[512])
{
    uint16_t crc[4];
    sd_crc16_lanes(buffer, 512, crc);
    sc_write_data_byte(0xff);
    sc_write_data_byte(0xff);
    sc_write_data_byte(0xff);
    sc_write_data_byte(0xf0);
    for (unsigned i = 0; i < 512; ++i)
        sc_write_data_byte(buffer[i]);
    uint8_t packed[8];
    sd_crc16_pack(crc, packed);
    for (unsigned i = 0; i < sizeof(packed); ++i)
        sc_write_data_byte(packed[i]);
    if (sc_data_nibble() != 15)
        return SD_FRAME;
    uint16_t start = sc_ticks();
    bool found = false;
    for (unsigned poll = 0; poll < 262144; ++poll) {
        if (!(sc_data_nibble() & 1)) {
            found = true;
            break;
        }
        if ((uint16_t)(sc_ticks() - start) >= 16384)
            return SD_TIMEOUT;
    }
    if (!found)
        return SD_TIMEOUT;
    unsigned token = 0;
    for (unsigned i = 0; i < 4; ++i)
        token = (token << 1) | (sc_data_nibble() & 1);
    SdError result = token == 5 ? SD_OK :
                     token == 11 ? SD_CRC : SD_CARD_ERROR;
    /* SD Physical Layer 4.4: supply at least 8 clocks after CRC status. */
    for (unsigned i = 0; i < 8; ++i)
        sc_wait_write_ready();
    start = sc_ticks();
    for (unsigned poll = 0; poll < 524288; ++poll) {
        if (sc_wait_write_ready())
            return result;
        if ((uint16_t)(sc_ticks() - start) >= 32768)
            return SD_TIMEOUT;
    }
    return SD_TIMEOUT;
}
