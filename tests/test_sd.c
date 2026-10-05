/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sd.h"

static void seal(uint8_t raw[16])
{
    raw[15] = sd_crc7(raw, 15);
}

static void put_bits(uint8_t raw[16], unsigned low, unsigned width, uint32_t value)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned byte = 15 - (low + i) / 8;
        unsigned mask = 1u << ((low + i) % 8);
        raw[byte] = (uint8_t)((raw[byte] & ~mask) | ((value & 1) ? mask : 0));
        value >>= 1;
    }
    seal(raw);
}

static void test_crc(void)
{
    /* SD Physical Layer v6.00 section 4.5 published CRC examples. */
    const uint8_t cmd0[] = {0x40,0,0,0,0};
    const uint8_t cmd17[] = {0x51,0,0,0,0};
    const uint8_t response17[] = {0x11,0,0,9,0};
    assert(sd_crc7(cmd0, 5) == 0x95);
    assert(sd_crc7(cmd17, 5) == 0x55);
    assert(sd_crc7(response17, 5) == 0x67);
    uint8_t ones[2048];
    memset(ones, 0xff, sizeof(ones));
    uint16_t crc[4];
    sd_crc16_lanes(ones, sizeof(ones), crc);
    /* Each lane receives the published 4096-one-bit CRC16 example. */
    for (unsigned lane = 0; lane < 4; ++lane)
        assert(crc[lane] == 0x7fa1);
    const uint8_t mixed[] = {0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
    const uint16_t mixed_crc[] = {0xe615,0xadec,0xd383,0x3de0};
    sd_crc16_lanes(mixed, sizeof(mixed), crc);
    assert(memcmp(crc, mixed_crc, sizeof(crc)) == 0);
    uint8_t tail[8] = {0,0x0f,0xff,0x0f,0,0,0xff,0xff};
    assert(sd_check_data(ones, 8, tail) == SD_OK);
    for (unsigned lane = 0; lane < 4; ++lane) {
        tail[7] ^= (uint8_t)(1u << lane);
        assert(sd_check_data(ones, 8, tail) == SD_CRC);
        tail[7] ^= (uint8_t)(1u << lane);
    }
    const uint8_t bits[] = {0x12,0x34,0x56,0x78};
    assert(sd_bits(bits, 4, 0, 32) == 0x12345678);
    assert(sd_bits(bits, 4, 7, 17) == ((0x12345678u >> 7) & 0x1ffff));
}

static void test_cid(void)
{
    /* Sample from maxkueng/sdcard-cid-decode. Fields independently checked
     * against SD Physical Layer v6.00 section 5.2; this sample retains CRC.
     */
    uint8_t cid[16] = {0x27,0x50,0x48,0x53,0x44,0x33,0x32,0x47,
                       0x30,0x01,0xb4,0x4e,0xed,0x00,0xf2,0x21};
    SdCid out;
    assert(sd_decode_cid(cid, &out) == SD_OK);
    assert(out.manufacturer == 0x27 && out.oem == 0x5048);
    assert(strcmp(out.oem_text, "PH") == 0);
    assert(strcmp(out.product, "SD32G") == 0);
    assert(out.revision_major == 3 && out.revision_minor == 0);
    assert(out.serial == 28593901 && out.year == 2015 && out.month == 2);
    cid[3] ^= 1;
    assert(sd_decode_cid(cid, &out) == SD_CRC);
    cid[3] ^= 1;
    cid[15] &= 0xfe;
    assert(sd_decode_cid(cid, &out) == SD_FRAME);
    seal(cid);
    cid[14] = 0xf0;
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_BAD_FIELD);
    cid[14] = 0xfd;
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_BAD_FIELD);
    cid[13] = 0x0f;
    cid[14] = 0xfc;
    memset(cid + 9, 0xff, 4);
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_OK);
    assert(out.year == 2255 && out.month == 12 && out.serial == UINT32_MAX);
    cid[8] = 0xa0;
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_BAD_FIELD);
    cid[8] = 0x30;
    cid[3] = 0;
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_BAD_FIELD);
    cid[3] = 'S';
    cid[13] |= 0xf0;
    seal(cid);
    assert(sd_decode_cid(cid, &out) == SD_BAD_FIELD);

    /* Linux/sysfs-style sample with its trailing CRC absent is NOT accepted
     * as an intact on-wire response. Its fields alone are not CRC evidence.
     */
    uint8_t sysfs_cid[16] = {3,0x53,0x44,0x53,0x4c,0x31,0x36,0x47,
                            0x80,0x2b,0x2f,0x2a,0x77,1,0x33,0};
    assert(sd_decode_cid(sysfs_cid, &out) == SD_FRAME);
    seal(sysfs_cid);
    assert(sysfs_cid[15] == 0xf5);
    assert(sd_decode_cid(sysfs_cid, &out) == SD_OK);
    assert(out.year == 2019 && out.month == 3 && out.serial == 724511351);
}

static void test_csd(void)
{
    /* Synthetic fixed-byte fixtures, derived from v6.00 section 5.3.
     * v1: C_SIZE=0x123, C_SIZE_MULT=5, READ_BL_LEN=11.
     * v2: C_SIZE=0x12345, READ_BL_LEN=9.
     */
    uint8_t v1[16] = {0,0,0,0,0,11,0,0x48,0xc0,2,0x80,0,0,0,0,0x93};
    uint8_t v2[16] = {0x40,0,0,0,0,9,0,1,0x23,0x45,0,0,0,0,0,0x59};
    SdCsd out;
    assert(sd_decode_csd(v1, &out) == SD_OK);
    assert(out.structure == 0 && out.bytes == UINT64_C(76546048));
    assert(out.sectors == UINT64_C(149504));
    assert(sd_decode_csd(v2, &out) == SD_OK);
    assert(out.structure == 1 && out.bytes == UINT64_C(39094059008));
    assert(out.sectors == UINT64_C(76355584));
    put_bits(v1, 62, 12, 0xfff);
    put_bits(v1, 47, 3, 7);
    assert(sd_decode_csd(v1, &out) == SD_OK);
    assert(out.bytes == UINT64_C(4294967296));
    put_bits(v1, 80, 4, 8);
    assert(sd_decode_csd(v1, &out) == SD_BAD_FIELD);
    put_bits(v2, 48, 22, 0x3fffff);
    assert(sd_decode_csd(v2, &out) == SD_OK);
    assert(out.bytes == UINT64_C(2199023255552));
    assert(out.sectors == UINT64_C(4294967296));
    put_bits(v2, 48, 22, 0);
    assert(sd_decode_csd(v2, &out) == SD_OK && out.bytes == 524288);
    put_bits(v2, 80, 4, 10);
    assert(sd_decode_csd(v2, &out) == SD_BAD_FIELD);
    put_bits(v2, 126, 2, 2);
    assert(sd_decode_csd(v2, &out) == SD_UNSUPPORTED_CSD);
    assert(out.bytes == 0 && out.sectors == 0);
    put_bits(v2, 126, 2, 3);
    assert(sd_decode_csd(v2, &out) == SD_UNSUPPORTED_CSD);
    v2[15] ^= 2;
    assert(sd_decode_csd(v2, &out) == SD_CRC);
}

static void test_responses(void)
{
    uint8_t short_response[6] = {55,0,0,0,0x20,0};
    short_response[5] = sd_crc7(short_response, 5);
    assert(sd_check_response(55, short_response, 6) == SD_OK);
    assert(sd_check_response(55, short_response, 5) == SD_FRAME);
    assert(sd_check_response(8, short_response, 6) == SD_FRAME);
    short_response[5] ^= 2;
    assert(sd_check_response(55, short_response, 6) == SD_CRC);
    uint8_t ocr[6] = {0x3f,0xc0,4,0,0,0xff};
    assert(sd_check_response(41, ocr, 6) == SD_OK);
    ocr[5] = 1;
    assert(sd_check_response(41, ocr, 6) == SD_FRAME);
    uint8_t r2[17] = {0x3f,0x40,0,0,0,0,9,0,1,0x23,0x45,0,0,0,0,0,0x59};
    assert(sd_check_response(9, r2, 17) == SD_OK);
    r2[0] = 9;
    assert(sd_check_response(9, r2, 17) == SD_FRAME);
    r2[0] = 0x3f;
    r2[1] ^= 1;
    assert(sd_check_response(9, r2, 17) == SD_CRC);
}

int main(void)
{
    test_crc();
    test_cid();
    test_csd();
    test_responses();
    puts("Register and response tests passed.");
    return 0;
}
