/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "report.h"
#include "sfw_backend.h"
#include "fatfs/diskio.h"

static SdReport card;
static unsigned probes, initializations, reads, writes, finishes;
static unsigned init_error, read_error, write_error;
static uint32_t last_sector;
static unsigned last_count;
static bool sync_ready = true;
static uint32_t driver_sectors;

void sd_probe(SdReport *report)
{
    ++probes;
    *report = card;
}

unsigned sfw_initialize(uint32_t *sectors, bool *block_addressed)
{
    ++initializations;
    *sectors = driver_sectors ? driver_sectors : (uint32_t)card.capacity.sectors;
    *block_addressed = card.block_addressed;
    return init_error;
}

unsigned sfw_read(uint8_t *buffer, uint32_t sector, unsigned count)
{
    ++reads;
    last_sector = sector;
    last_count = count;
    memset(buffer, 0x5a, count * 512);
    return read_error;
}

unsigned sfw_write(const uint8_t *buffer, uint32_t sector, unsigned count)
{
    (void)buffer;
    ++writes;
    last_sector = sector;
    last_count = count;
    return write_error;
}

bool sfw_sync(void) { return sync_ready; }
void sfw_finish(void) { ++finishes; }

int main(void)
{
    uint8_t buffer[1024];
    card = (SdReport){.error = SD_OK, .cid_captured = true,
                       .block_addressed = true};
    card.capacity.sectors = 100;
    storage_enable(false);
    assert(disk_initialize(0) == STA_NOINIT && probes == 0);
    assert(disk_write(0, buffer, 0, 1) == RES_NOTRDY && writes == 0);
    storage_expect_card(&card);
    storage_enable(true);
    assert(disk_initialize(0) == 0 && initializations == 1);
    assert(disk_read(0, buffer, 5, 2) == RES_OK);
    assert(last_sector == 5 && last_count == 2 && buffer[0] == 0x5a);
    assert(disk_write(0, buffer, 3, 2) == RES_OK && writes == 1);
    assert(last_sector == 3 && last_count == 2);
    assert(disk_write(0, buffer, 99, 2) == RES_PARERR && writes == 1);
    assert(disk_read(0, buffer, 0, 0) == RES_PARERR);
    storage_enable(false);
    storage_enable(true);
    assert(disk_initialize(0) == 0);
    read_error = 8;
    assert(disk_read(0, buffer, 8, 1) == RES_ERROR);
    assert(storage_error() == SD_CARD_ERROR);
    assert(storage_debug()->command == 18 && storage_debug()->sector == 8);
    assert(storage_debug()->driver_error == 8);
    assert(!storage_debug()->write_attempted);
    assert(disk_write(0, buffer, 0, 1) == RES_ERROR && writes == 1);
    read_error = 0;
    storage_enable(false);
    assert(storage_debug()->driver_error == 8);
    storage_enable(true);
    card.block_addressed = false;
    assert(disk_initialize(0) == 0);
    assert(disk_read(0, buffer, 7, 1) == RES_OK && last_sector == 7);
    write_error = 9;
    assert(disk_write(0, buffer, 2, 1) == RES_ERROR);
    assert(storage_debug()->command == 25 && storage_debug()->write_attempted);
    write_error = 0;
    storage_enable(false);
    /* SDSC: the vendor CSD v1 count is ignored in favour of the strict one. */
    driver_sectors = 12304;
    card.capacity.sectors = 149504;
    storage_enable(true);
    assert(disk_initialize(0) == 0);
    LBA_t total = 0;
    assert(disk_ioctl(0, GET_SECTOR_COUNT, &total) == RES_OK && total == 149504);
    assert(disk_write(0, buffer, 149503, 1) == RES_OK);
    assert(disk_write(0, buffer, 149504, 1) == RES_PARERR);
    storage_enable(false);
    /* SDHC counts must still match exactly. */
    card.block_addressed = true;
    card.capacity.structure = 1;
    storage_enable(true);
    assert(disk_initialize(0) == STA_NOINIT);
    assert(storage_error() == SD_ADDRESS_MISMATCH);
    storage_enable(false);
    driver_sectors = 0;
    card.capacity.sectors = 100;
    card.block_addressed = false;
    card.capacity.structure = 0;
    storage_enable(true);
    assert(disk_initialize(0) == 0);
    sync_ready = false;
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);
    assert(storage_error() == SD_TIMEOUT);
    sync_ready = true;
    card.cid[0] = 1;
    storage_enable(true);
    unsigned previous = initializations;
    assert(disk_initialize(0) == STA_NOINIT && initializations == previous);
    storage_expect_card(&card);
    card.csd[14] = 0x10;
    storage_enable(true);
    assert(disk_initialize(0) == STA_NOINIT && initializations == previous);
    card.csd[14] = 0;
    init_error = 3;
    storage_enable(true);
    assert(disk_initialize(0) == STA_NOINIT);
    assert(storage_debug()->driver_error == 3);
    assert(finishes != 0 && reads != 0);
    puts("SuperFW storage adapter authorization, bounds and failures passed.");
    return 0;
}
