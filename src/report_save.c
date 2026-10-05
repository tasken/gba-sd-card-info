/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "report.h"
#include <stddef.h>

FRESULT report_save(const SdReport *report)
{
    static char text[4096];
    static FATFS filesystem;
    static FIL file;
    unsigned length;
    storage_enable(true);
    if (!report_format(report, text, sizeof(text), &length)) {
        storage_enable(false);
        return FR_INT_ERR;
    }
    storage_expect_card(report);
    FRESULT result = f_mount(&filesystem, "", 1);
    /* Partition scanning can hide an earlier failed read. Never write then. */
    if (storage_error() != SD_OK &&
        (result == FR_OK || result == FR_NO_FILESYSTEM))
        result = FR_DISK_ERR;
    if (result == FR_OK && filesystem.fs_type != FS_FAT16 &&
        filesystem.fs_type != FS_FAT32)
        result = FR_NO_FILESYSTEM;
    if (result == FR_OK)
        result = f_open(&file, "SDINFO.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (result == FR_OK) {
        UINT written;
        result = f_write(&file, text, length, &written);
        if (result == FR_OK && written != length)
            result = FR_DENIED;
        if (result == FR_OK)
            result = f_sync(&file);
        FRESULT closed = f_close(&file);
        if (result == FR_OK)
            result = closed;
    }
    FRESULT unmounted = f_mount(NULL, "", 0);
    if (result == FR_OK)
        result = unmounted;
    storage_enable(false);
    return result;
}

const char *report_result_name(FRESULT result)
{
    switch (result) {
    case FR_OK: return "Report saved";
    case FR_DISK_ERR: return "Card read/write failed";
    case FR_NOT_READY: return "Card initialization failed";
    case FR_NO_FILESYSTEM: return "FAT16/FAT32 volume not found";
    case FR_DENIED: return "Card full or file access denied";
    case FR_WRITE_PROTECTED: return "Card is write protected";
    case FR_INT_ERR: return "Report/filesystem internal error";
    case FR_INVALID_NAME: return "Invalid report filename";
    default: return "Filesystem operation failed";
    }
}
