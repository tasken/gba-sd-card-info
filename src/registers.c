/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * CRC routine adapted from SuperFW src/crc.c.
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3, or (at your option) any later
 * version. This program is distributed WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See LICENSE for the GNU General Public License.
 */
#include "sd.h"

uint8_t sd_crc7(const uint8_t *data, unsigned size)
{
    uint8_t crc = 0;
    for (unsigned i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j) {
            uint8_t polynomial = (crc & 0x80) ? 0x09 : 0;
            crc = (uint8_t)((crc ^ polynomial) << 1);
        }
    }
    return (uint8_t)(crc | 1);
}

uint32_t sd_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

SdError sd_check_response(uint8_t command, const uint8_t *data, unsigned size)
{
    if (command == 2 || command == 9) {
        if (size != 17 || data[0] != 0x3f || !(data[16] & 1))
            return SD_FRAME;
        /* R2 protects the 120 register bits, not the framing byte. */
        return sd_crc7(data + 1, 15) == data[16] ? SD_OK : SD_CRC;
    }
    if (size != 6 || !(data[5] & 1))
        return SD_FRAME;
    if (command == 41)
        return data[0] == 0x3f && data[5] == 0xff ? SD_OK : SD_FRAME;
    if (data[0] != command)
        return SD_FRAME;
    return sd_crc7(data, 5) == data[5] ? SD_OK : SD_CRC;
}

uint32_t sd_bits(const uint8_t *raw, unsigned size, unsigned low, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = low + width; i > low; --i) {
        unsigned bit = i - 1;
        value = (value << 1) | ((raw[size - 1 - bit / 8] >> (bit % 8)) & 1);
    }

    return value;
}

void sd_crc16_lanes(const uint8_t *data, unsigned size, uint16_t crc[4])
{
    for (unsigned lane = 0; lane < 4; ++lane) {
        crc[lane] = 0;
        for (unsigned i = 0; i < size; ++i) {
            for (unsigned shift = 4;; shift = 0) {
                unsigned bit = (data[i] >> (shift + lane)) & 1;
                unsigned feedback = (crc[lane] >> 15) ^ bit;
                crc[lane] = (uint16_t)((crc[lane] << 1) ^
                                       (feedback ? 0x1021u : 0));
                if (shift == 0)
                    break;
            }
        }
    }
}

void sd_crc16_pack(const uint16_t lanes[4], uint8_t packed[8])
{
    for (unsigned i = 0; i < 8; ++i) {
        unsigned bit = 16 - 2 * i;
        unsigned value = 0;
        for (unsigned lane = 0; lane < 4; ++lane) {
            value |= ((lanes[lane] >> (bit - 1)) & 1u) << (lane + 4);
            value |= ((lanes[lane] >> (bit - 2)) & 1u) << lane;
        }
        packed[i] = (uint8_t)value;
    }
}

SdError sd_check_data(const uint8_t *data, unsigned size, const uint8_t tail[8])
{
    uint16_t expected[4], received[4] = {0};
    sd_crc16_lanes(data, size, expected);
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned shift = 4;; shift = 0) {
            for (unsigned lane = 0; lane < 4; ++lane)
                received[lane] = (uint16_t)((received[lane] << 1) |
                                            ((tail[i] >> (shift + lane)) & 1));
            if (shift == 0)
                break;
        }
    }
    for (unsigned lane = 0; lane < 4; ++lane)
        if (received[lane] != expected[lane])
            return SD_CRC;
    return SD_OK;
}

static bool ascii_text(const uint8_t *raw, char *out, unsigned count)
{
    bool valid = true;
    for (unsigned i = 0; i < count; ++i) {
        bool printable = raw[i] >= 32 && raw[i] <= 126;
        out[i] = printable ? (char)raw[i] : '?';
        valid = valid && printable;
    }
    out[count] = '\0';
    return valid;
}

SdError sd_decode_cid(const uint8_t raw[16], SdCid *out)
{
    *out = (SdCid){0};
    if (!(raw[15] & 1))
        return SD_FRAME;
    if (sd_crc7(raw, 15) != raw[15])
        return SD_CRC;
    out->manufacturer = raw[0];
    out->oem = (uint16_t)((uint16_t)raw[1] << 8 | raw[2]);
    bool oem_ok = ascii_text(raw + 1, out->oem_text, 2);
    bool product_ok = ascii_text(raw + 3, out->product, 5);
    out->revision_major = raw[8] >> 4;
    out->revision_minor = raw[8] & 15;
    out->serial = sd_be32(raw + 9);
    out->year = (uint16_t)(2000 + ((raw[13] & 15) << 4) + (raw[14] >> 4));
    out->month = raw[14] & 15;
    if (!oem_ok || !product_ok || (raw[13] & 0xf0) ||
        out->revision_major > 9 || out->revision_minor > 9 ||
        out->month == 0 || out->month > 12)
        return SD_BAD_FIELD;
    return SD_OK;
}

SdError sd_decode_csd(const uint8_t raw[16], SdCsd *out)
{
    *out = (SdCsd){0};
    if (!(raw[15] & 1))
        return SD_FRAME;
    if (sd_crc7(raw, 15) != raw[15])
        return SD_CRC;
    out->structure = (uint8_t)sd_bits(raw, 16, 126, 2);
    if (out->structure == 0) {
        unsigned block_bits = sd_bits(raw, 16, 80, 4);
        if (block_bits < 9 || block_bits > 11)
            return SD_BAD_FIELD;
        uint64_t blocks = (uint64_t)sd_bits(raw, 16, 62, 12) + 1;
        out->bytes = blocks << (sd_bits(raw, 16, 47, 3) + 2 + block_bits);
    } else if (out->structure == 1) {
        if (sd_bits(raw, 16, 80, 4) != 9)
            return SD_BAD_FIELD;
        out->bytes = ((uint64_t)sd_bits(raw, 16, 48, 22) + 1) << 19;
    } else {
        return SD_UNSUPPORTED_CSD;
    }
    out->sectors = out->bytes >> 9;
    return SD_OK;
}
