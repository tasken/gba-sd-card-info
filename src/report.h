/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REPORT_H
#define REPORT_H

#include "sd.h"
#include "fatfs/ff.h"

bool report_format(const SdReport *report, char *buffer, unsigned size,
                   unsigned *length);
FRESULT report_save(const SdReport *report);
void storage_enable(bool enabled);
void storage_expect_card(const SdReport *report);
SdError storage_error(void);
typedef enum {
    STORAGE_INIT, STORAGE_COMMAND, STORAGE_READ_DATA, STORAGE_WRITE_DATA
} StoragePhase;

typedef struct {
    StoragePhase phase;
    uint8_t command, response[17], response_size;
    bool sector_valid, data_captured, write_attempted;
    uint32_t sector;
    unsigned driver_error;
    uint8_t received_crc[8], calculated_crc[8], sample[8];
} StorageDebug;

const StorageDebug *storage_debug(void);
const char *report_result_name(FRESULT result);

#endif
