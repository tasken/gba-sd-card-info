/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SD_H
#define SD_H

#include <stdbool.h>
#include <stdint.h>

#define DRIVER_REVISION "1abd49bc"
#ifndef BUILD_COMMIT
#define BUILD_COMMIT ""
#endif
#ifndef BUILD_TAG
#define BUILD_TAG ""
#endif

typedef enum {
    SD_OK, SD_TIMEOUT, SD_BUS_BUSY, SD_FRAME, SD_CRC, SD_CARD_ERROR,
    SD_VOLTAGE, SD_UNSUPPORTED_CSD, SD_BAD_FIELD, SD_ADDRESS_MISMATCH
} SdError;

typedef enum {
    ST_RESET, ST_CMD8, ST_READY, ST_CID, ST_RCA, ST_CSD,
    ST_SELECT, ST_BUS_WIDTH, ST_BLOCK_LENGTH, ST_DECODE, ST_DONE,
    ST_SCR, ST_SD_STATUS
} SdStage;

typedef struct {
    uint8_t manufacturer;
    uint16_t oem;
    char oem_text[3];
    char product[6];
    uint8_t revision_major, revision_minor, month;
    uint16_t year;
    uint32_t serial;
} SdCid;

typedef struct {
    uint8_t structure;
    uint64_t bytes, sectors;
} SdCsd;

typedef struct {
    uint8_t cid[16], csd[16];
    bool cid_captured, csd_captured, cid_valid, csd_valid;
    bool ocr_valid, cmd8_valid, cmd8_legacy, block_addressed, bus4;
    uint32_t ocr, ocr_request, cmd8_response, card_status;
    uint16_t rca;
    unsigned attempts;
    SdCid identity;
    SdCsd capacity;
    SdStage stage;
    SdError error, cid_error, csd_error;
    uint8_t failed_command, response[17], response_size;
    uint8_t scr[8], status[64], scr_crc[8], status_crc[8];
    bool extras_attempted, scr_captured, status_captured;
    SdError scr_error, status_error;
    uint8_t extra_response[17], extra_response_size, extra_failed_command;
    SdStage extra_stage;
    SdError extra_error;
} SdReport;

uint8_t sd_crc7(const uint8_t *data, unsigned size);
uint32_t sd_be32(const uint8_t *data);
SdError sd_check_response(uint8_t command, const uint8_t *data, unsigned size);
SdError sd_decode_cid(const uint8_t raw[16], SdCid *out);
SdError sd_decode_csd(const uint8_t raw[16], SdCsd *out);
uint32_t sd_bits(const uint8_t *raw, unsigned size, unsigned low, unsigned width);
void sd_crc16_lanes(const uint8_t *data, unsigned size, uint16_t crc[4]);
void sd_crc16_pack(const uint16_t lanes[4], uint8_t packed[8]);
SdError sd_check_data(const uint8_t *data, unsigned size, const uint8_t crc[8]);
void sd_probe(SdReport *report);
void sd_probe_extra(SdReport *report);
const char *sd_stage_name(SdStage stage);
const char *sd_error_name(SdError error);
const char *sd_speed_class_name(unsigned code);
const char *sd_uhs_class_name(unsigned code);
bool sd_report_complete(const SdReport *report);

#endif
