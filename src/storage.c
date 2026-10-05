/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "report.h"
#include "sfw_backend.h"
#include "fatfs/diskio.h"

static bool enabled, initialized, check_cid, write_attempted;
static uint64_t sectors;
static SdError first_error;
static uint8_t expected_cid[16];
static StorageDebug debug;

const StorageDebug *storage_debug(void) { return &debug; }
SdError storage_error(void) { return first_error; }

void storage_expect_card(const SdReport *report)
{
    check_cid = report->cid_captured;
    if (check_cid)
        memcpy(expected_cid, report->cid, sizeof(expected_cid));
}

void storage_enable(bool value)
{
    enabled = value;
    if (value) {
        first_error = SD_OK;
        debug = (StorageDebug){0};
        write_attempted = false;
    } else {
        initialized = false;
        sfw_finish();
    }
}

static DRESULT failed(SdError error, StoragePhase phase, unsigned command,
                      uint32_t sector, bool sector_valid, unsigned code)
{
    if (first_error == SD_OK) {
        first_error = error;
        debug = (StorageDebug){.phase = phase, .command = command,
            .sector = sector, .sector_valid = sector_valid,
            .write_attempted = write_attempted, .driver_error = code};
    }
    return RES_ERROR;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive != 0 || !enabled)
        return STA_NOINIT;
    initialized = false;
    SdReport card;
    sd_probe(&card);
    if (card.error != SD_OK ||
        (check_cid && memcmp(expected_cid, card.cid, sizeof(expected_cid))) ||
        sd_bits(card.csd, 16, 12, 2)) {
        failed(card.error != SD_OK ? card.error : SD_CARD_ERROR,
               STORAGE_INIT, card.failed_command, 0, false, 0);
        return STA_NOINIT;
    }
    uint32_t count;
    bool block_addressed;
    unsigned code = sfw_initialize(&count, &block_addressed);
    /* SuperFW v0.21 decodes CSD v1 C_SIZE/C_SIZE_MULT one byte late, so its
     * SDSC count is wrong; use the strict decoder's count for those cards. */
    if (!code && !block_addressed && !card.block_addressed &&
        card.capacity.structure == 0)
        count = (uint32_t)card.capacity.sectors;
    if (code || !count || count != card.capacity.sectors ||
        block_addressed != card.block_addressed) {
        failed(code ? SD_CARD_ERROR : SD_ADDRESS_MISMATCH,
               STORAGE_INIT, 0, 0, false, code);
        return STA_NOINIT;
    }
    sectors = count;
    initialized = true;
    return 0;
}

DSTATUS disk_status(BYTE drive)
{
    return drive == 0 && enabled && initialized ? 0 : STA_NOINIT;
}

static bool valid_range(LBA_t sector, UINT count)
{
    return count && (uint64_t)sector < sectors && count <= sectors - sector;
}

DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (!valid_range(sector, count))
        return RES_PARERR;
    unsigned code = sfw_read(buffer, sector, count);
    return code ? failed(SD_CARD_ERROR, STORAGE_READ_DATA, 18, sector, true, code) :
                  RES_OK;
}

DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (!valid_range(sector, count))
        return RES_PARERR;
    if (first_error != SD_OK)
        return RES_ERROR;
    write_attempted = true;
    unsigned code = sfw_write(buffer, sector, count);
    return code ? failed(SD_CARD_ERROR, STORAGE_WRITE_DATA, 25, sector, true, code) :
                  RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer)
{
    if (disk_status(drive))
        return RES_NOTRDY;
    if (command == CTRL_SYNC)
        return sfw_sync() ? RES_OK :
               failed(SD_TIMEOUT, STORAGE_WRITE_DATA, 0, 0, false, 0);
    if (command == GET_SECTOR_COUNT) {
        *(LBA_t *)buffer = (LBA_t)sectors;
        return RES_OK;
    }
    if (command == GET_SECTOR_SIZE) {
        *(WORD *)buffer = 512;
        return RES_OK;
    }
    if (command == GET_BLOCK_SIZE) {
        *(DWORD *)buffer = 1;
        return RES_OK;
    }
    return RES_PARERR;
}
