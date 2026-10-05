/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "report.h"
#include "fatfs/diskio.h"

static BYTE *image;
static unsigned sector_count, writes;
static bool enabled, fail_read, fail_write, fail_sync;
static bool fail_partition_read;
static SdError transport_error;

void storage_enable(bool value)
{
    enabled = value;
    if (value)
        transport_error = SD_OK;
}
void storage_expect_card(const SdReport *r) { (void)r; }
SdError storage_error(void) { return transport_error; }
DSTATUS disk_status(BYTE drive) { return drive == 0 && enabled ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE drive) { return disk_status(drive); }
DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    assert(disk_status(drive) == 0);
    assert(count && sector < sector_count && count <= sector_count - sector);
    if (fail_read || (fail_partition_read && sector == 1)) {
        transport_error = SD_CRC;
        return RES_ERROR;
    }
    memcpy(data, image + sector * 512u, count * 512u);
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    assert(disk_status(drive) == 0);
    assert(count && sector < sector_count && count <= sector_count - sector);
    ++writes;
    if (fail_write)
        return RES_ERROR;
    memcpy(image + sector * 512u, data, count * 512u);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE cmd, void *data)
{
    (void)data;
    assert(disk_status(drive) == 0 && cmd == CTRL_SYNC);
    return fail_sync ? RES_ERROR : RES_OK;
}

static void put16(BYTE *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void put32(BYTE *p, unsigned n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}

static void assert_field(const char *text, const char *label, const char *value)
{
    char key[64], expected[160];
    assert(text);
    snprintf(key, sizeof(key), "%s:", label);
    snprintf(expected, sizeof(expected), "%-32s%s\r\n", key, value);
    assert(strstr(text, expected));
}

static void make_volume(bool fat32)
{
    sector_count = fat32 ? 70000 : 16384;
    image = calloc(sector_count, 512);
    assert(image);
    image[0] = 0xeb; image[1] = 0x58; image[2] = 0x90;
    memcpy(image + 3, "MSDOS5.0", 8);
    put16(image + 11, 512);
    image[13] = 1;
    unsigned reserved = fat32 ? 32 : 1, fat_size = fat32 ? 600 : 64;
    put16(image + 14, reserved);
    image[16] = 2;
    image[21] = 0xf8;
    if (fat32) {
        put32(image + 32, sector_count);
        put32(image + 36, fat_size);
        put32(image + 44, 2);
        put16(image + 48, 1);
        put16(image + 50, 6);
        image[66] = 0x29;
        memcpy(image + 82, "FAT32   ", 8);
        put32(image + 512, 0x41615252);
        put32(image + 512 + 484, 0x61417272);
        put32(image + 512 + 488, UINT32_MAX);
        put32(image + 512 + 492, UINT32_MAX);
        put32(image + 512 + 508, 0xaa550000);
    } else {
        put16(image + 17, 512);
        put16(image + 19, sector_count);
        put16(image + 22, fat_size);
        image[38] = 0x29;
        memcpy(image + 54, "FAT16   ", 8);
    }
    put16(image + 510, 0xaa55);
    for (unsigned copy = 0; copy < 2; ++copy) {
        BYTE *fat = image + (reserved + copy * fat_size) * 512;
        if (fat32) {
            put32(fat, 0x0ffffff8);
            put32(fat + 4, 0x0fffffff);
            put32(fat + 8, 0x0fffffff);
        } else {
            put16(fat, 0xfff8);
            put16(fat + 2, 0xffff);
        }
    }
    writes = 0;
    fail_read = fail_write = fail_sync = false;
}

static void volume_cases(bool fat32)
{
    make_volume(fat32);
    FATFS fs;
    FIL file;
    UINT count;
    storage_enable(true);
    assert(f_mount(&fs, "", 1) == FR_OK);
    assert(fs.fs_type == (fat32 ? FS_FAT32 : FS_FAT16));
    assert(f_open(&file, "KEEP.SAV", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, "untouched", 9, &count) == FR_OK && count == 9);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, "", 0) == FR_OK);
    storage_enable(false);

    SdReport report = {.error = SD_TIMEOUT, .stage = ST_CSD,
                       .failed_command = 9, .cid_captured = true};
    report.cid[0] = 0x27; report.cid[1] = 'P'; report.cid[2] = 'H';
    char expected[4096], actual[4096];
    unsigned length;
    assert(report_format(&report, expected, sizeof(expected), &length));
    assert_field(expected, "MID (8-bit binary number)", "0x27");
    assert_field(expected, "OID ASCII", "PH");
    assert_field(expected, "CSD", "Not captured");
    assert(!strstr(expected, "[ CSD ]\r\n"));
    assert(report_save(&report) == FR_OK && !enabled);
    assert(writes > 0);
    /* Replacing a longer file must truncate it and preserve other files. */
    report = (SdReport){.error = SD_OK, .stage = ST_DONE};
    assert(report_format(&report, expected, sizeof(expected), &length));
    assert(report_save(&report) == FR_OK && !enabled);
    storage_enable(true);
    assert(f_mount(&fs, "", 1) == FR_OK);
    assert(f_open(&file, "SDINFO.TXT", FA_READ) == FR_OK);
    assert(f_size(&file) == length);
    assert(f_read(&file, actual, sizeof(actual), &count) == FR_OK && count == length);
    assert(memcmp(actual, expected, length) == 0);
    assert(f_close(&file) == FR_OK);
    assert(f_open(&file, "KEEP.SAV", FA_READ) == FR_OK);
    assert(f_read(&file, actual, sizeof(actual), &count) == FR_OK && count == 9);
    assert(memcmp(actual, "untouched", 9) == 0);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, "", 0) == FR_OK);
    storage_enable(false);
    fail_read = true;
    unsigned previous = writes;
    assert(report_save(&report) == FR_DISK_ERR && !enabled && writes == previous);
    fail_read = false; fail_write = true;
    assert(report_save(&report) == FR_DISK_ERR && !enabled);
    fail_write = false; fail_sync = true;
    assert(report_save(&report) == FR_DISK_ERR && !enabled);
    fail_sync = false;
    memset(image, 0, sector_count * 512u);
    previous = writes;
    assert(report_save(&report) == FR_NO_FILESYSTEM && !enabled && writes == previous);
    /* A failed first partition read followed by empty entries masks the I/O
     * error in FatFs. The exporter must still report a disk error, not no FAT.
     */
    put16(image + 510, 0xaa55);
    put32(image + 446 + 8, 1);
    fail_partition_read = true;
    assert(report_save(&report) == FR_DISK_ERR && !enabled && writes == previous);
    assert(storage_error() == SD_CRC);
    fail_partition_read = false;
    free(image);
}

int main(void)
{
    char buffer[4096];
    unsigned length;
    SdReport report = {.stage = ST_DONE, .error = SD_OK, .cid_valid = true,
                       .csd_valid = true, .ocr_valid = true, .extras_attempted = true,
                       .scr_captured = true, .status_captured = true,
                       .cid_captured = true, .csd_captured = true,
                       .response_size = 17, .extra_response_size = 17,
                       .extra_stage = ST_SD_STATUS};
    report.capacity.bytes = UINT64_C(2199023255552);
    report.capacity.sectors = UINT64_C(4294967296);
    report.status[8] = 4;
    report.status[14] = 0x30;
    strcpy(report.identity.product, "TEST");
    assert(sd_report_complete(&report));
    assert(strcmp(sd_uhs_class_name(0), "Not specified") == 0);
    assert(report_format(&report, buffer, sizeof(buffer), &length));
    assert(strncmp(buffer, "..:::[ ", 7) == 0);
    assert(strstr(buffer, ".:[ Card identity ]:.\r\n"));
    static const char *const sections[] = {
        "Card identity", "Card capacity", "Card capabilities",
        "Technical details",
        "Initialization and protocol",
        "Read and validation results", "Raw data for troubleshooting", "App information"
    };
    const char *cursor = buffer;
    for (unsigned i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        char heading[64];
        snprintf(heading, sizeof(heading), "\r\n\r\n.:[ %s ]:.\r\n\r\n", sections[i]);
        const char *found = strstr(cursor, heading);
        assert(found);
        cursor = found + strlen(heading);
    }
    assert(!strstr(buffer, ".:[ Notes ]:."));
    assert(strstr(buffer, ".:[ Raw data for troubleshooting ]:.\r\n\r\n[ CID ]"));
    assert_field(buffer, "SCR", "Passed");
    assert_field(buffer, "SD Status", "Passed");
    assert_field(buffer, "Last base response", "CRC/framing check failed");
    report.response_size = 6;
    memcpy(report.response, "\x06\x00\x00\x09\x20\xb9", 6);
    report.extra_response_size = 6;
    report.extra_failed_command = 13;
    memcpy(report.extra_response, "\x0d\x00\x00\x09\x20\x5b", 6);
    assert(report_format(&report, buffer, sizeof(buffer), &length));
    assert_field(buffer, "Last base response command", "6");
    assert_field(buffer, "Last base response", "Passed");
    assert_field(buffer, "Last extra response", "Passed");
    assert(!strstr(buffer, ".:[ Raw data for troubleshooting ]:."));
    assert(!strstr(buffer, "\r\n00: "));
    report.response[5] ^= 2;
    assert(report_format(&report, buffer, sizeof(buffer), &length));
    const char *response_dump = strstr(buffer, "[ Last base response ]\r\n");
    assert(response_dump);
    const char *response_row = strstr(response_dump, "\r\n00: ");
    static const char response_hex[] = "\r\n00: 06 00 00 09 20 BB\r\n";
    assert(response_row &&
           strncmp(response_row, response_hex, sizeof(response_hex) - 1) == 0);
    assert(!strchr(buffer, '|'));
    assert_field(buffer, "Card read", "Completed");
    assert_field(buffer, "Build commit", BUILD_COMMIT);
    assert(!strstr(buffer, "Base stage") && !strstr(buffer, "Extra stage"));
    assert(!strstr(buffer, "decode") && !strstr(buffer, "Extras"));
    assert_field(buffer, "Extra details", "Complete");
    assert_field(buffer, "Manufactured", "0000-00");
    assert_field(buffer, "Revision", "0.0");
    assert_field(buffer, "Speed class", "Class 10");
    assert_field(buffer, "UHS class", "U3");
    unsigned dump_count = 0;
    for (const char *row = buffer; (row = strstr(row, "\r\n00: ")) != NULL;
         row += 6) {
        assert(row >= buffer + 2 && row[-2] == '\r' && row[-1] == '\n');
        ++dump_count;
    }
    assert(dump_count == 8);
    assert_field(buffer, "Last base response", "CRC/framing check failed");
    assert(length < sizeof(buffer) && strlen(buffer) == length);
    assert_field(buffer, "Capacity bytes", "2199023255552");
    assert_field(buffer, "512-byte sectors", "4294967296");
    assert_field(buffer, "SD Status", "Passed");
    assert(!report_format(&report, buffer, 8, &length));
    assert(!report_format(&report, buffer, 0, &length));
    report.scr_error = SD_CRC;
    assert(!sd_report_complete(&report));
    report.extra_error = SD_CRC;
    report.status_captured = false;
    assert(report_format(&report, buffer, sizeof(buffer), &length));
    assert_field(buffer, "Extra details", "Card transfer checksum failed");
    assert_field(buffer, "Speed class", "Unavailable");
    assert_field(buffer, "UHS class", "Unavailable");
    assert_field(buffer, "SCR", "Card transfer checksum failed");
    assert_field(buffer, "Extra stage", "Read SD Status");
    report.extras_attempted = false;
    report.error = SD_TIMEOUT;
    report.failed_command = 9;
    report.cid_valid = report.csd_valid = false;
    assert(report_format(&report, buffer, sizeof(buffer), &length));
    assert_field(buffer, "Card read", "Card response timed out");
    assert_field(buffer, "Extra details", "Not read");
    assert_field(buffer, "Identity", "Unavailable");
    assert_field(buffer, "Capacity", "Unavailable");
    assert_field(buffer, "Failed base command", "9");
    assert(strstr(buffer, "\r\nBase stage"));
    assert(!strstr(buffer, "Extra stage"));
    volume_cases(false);
    volume_cases(true);
    puts("Report formatting and FAT16/FAT32 export tests passed.");
    return 0;
}
