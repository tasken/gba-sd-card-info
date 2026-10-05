/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Adapted from SuperFW src/supercard_driver.c at 1abd49bc18570956369283cbb724fbcf13e9a803.
 * Copyright (C) 2024 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3, or (at your option) any later
 * version. This program is distributed WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. See LICENSE for the GNU General Public License.
 */
#include "transport.h"

#define COMMAND_TIMEOUT 16384u
#define INIT_TICKS 32768u
#define OCR_V30 UINT32_C(0x00040000)
#define OCR_READY UINT32_C(0x80000000)
#define OCR_CCS UINT32_C(0x40000000)
#define R1_ERRORS UINT32_C(0xfff98008)

static SdError command_response(SdReport *r, uint8_t cmd, uint32_t arg,
                                unsigned size, bool data_follows)
{
    uint8_t packet[6] = { (uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, 0 };
    packet[5] = sd_crc7(packet, 5);
    r->failed_command = cmd;
    r->response_size = 0;
    if (!wait_sdcard_idle(COMMAND_TIMEOUT))
        return SD_BUS_BUSY;
    send_sdcard_commandbuf(packet, 6);
    if (size == 0) {
        send_empty_clocks(4096);
        return SD_OK;
    }
    bool got_response = receive_sdcard_response(r->response, size, COMMAND_TIMEOUT);
    if (!data_follows)
        send_empty_clocks(32);
    if (!got_response)
        return SD_TIMEOUT;
    r->response_size = (uint8_t)size;
    SdError err = sd_check_response(cmd, r->response, size);
    if (err != SD_OK)
        return err;
    if (cmd == 55 || cmd == 7 || cmd == 6 || cmd == 16 || cmd == 13 || cmd == 51 ||
        cmd == 17 || cmd == 18 || cmd == 12 || cmd == 24) {
        r->card_status = sd_be32(r->response + 1);
        if (r->card_status & R1_ERRORS)
            return SD_CARD_ERROR;
        if (cmd == 55 && !(r->card_status & 0x20))
            return SD_CARD_ERROR;
    }
    return SD_OK;
}

static SdReport sector_response;

const SdReport *sd_sector_response(void) { return &sector_response; }

SdError sd_sector_command(uint8_t cmd, uint32_t arg, uint32_t *status)
{
    if (cmd != 17 && cmd != 18 && cmd != 12 && cmd != 24 && cmd != 13)
        return SD_BAD_FIELD;
    sector_response = (SdReport){0};
    SdError error = command_response(&sector_response, cmd, arg, 6,
                                     cmd == 17 || cmd == 18);
    *status = sector_response.card_status;
    return error;
}

static SdError command(SdReport *r, uint8_t cmd, uint32_t arg, unsigned size)
{
    return command_response(r, cmd, arg, size, false);
}

static void enter_stage(SdReport *r, SdStage stage)
{
    r->stage = stage;
}

void sd_probe_extra(SdReport *r)
{
    if (r->error != SD_OK || !r->bus4)
        return;
    /* Keep the successful base report and its last response intact. */
    SdReport transfer = {0};
    transfer.rca = r->rca;
    r->extras_attempted = true;
    r->scr_captured = r->status_captured = false;
    r->scr_error = r->status_error = SD_OK;
    r->extra_error = SD_OK;
    for (unsigned which = 0; which < 2; ++which) {
        SdStage stage = which == 0 ? ST_SCR : ST_SD_STATUS;
        enter_stage(&transfer, stage);
        transfer.error = command(&transfer, 55, (uint32_t)r->rca << 16, 6);
        if (transfer.error == SD_OK)
            transfer.error = command_response(&transfer, which == 0 ? 51 : 13,
                                               0, 6, true);
        if (transfer.error == SD_OK)
            transfer.error = sc_read_register(which == 0 ? r->scr : r->status,
                                              which == 0 ? 8 : 64,
                                              which == 0 ? r->scr_crc : r->status_crc,
                                              which == 0 ? &r->scr_captured :
                                                           &r->status_captured);
        send_empty_clocks(32);
        if (which == 0)
            r->scr_error = transfer.error;
        else
            r->status_error = transfer.error;
        if (transfer.error != SD_OK)
            break;
    }
    r->extra_error = transfer.error;
    r->extra_stage = transfer.stage;
    r->extra_failed_command = transfer.failed_command;
    r->extra_response_size = transfer.response_size;
    for (unsigned i = 0; i < transfer.response_size; ++i)
        r->extra_response[i] = transfer.response[i];
}

void sd_probe(SdReport *r)
{
    *r = (SdReport){0};
    sc_enable();
    sc_timer_start();
    enter_stage(r, ST_RESET);
    send_empty_clocks(4096);
    r->error = command(r, 0, 0, 0);
    if (r->error != SD_OK)
        return;

    enter_stage(r, ST_CMD8);
    r->error = command(r, 8, 0x1aa, 6);
    if (r->error == SD_TIMEOUT) {
        /* Legacy SDSC cards do not answer CMD8. No other failure is ignored. */
        r->cmd8_legacy = true;
        r->error = SD_OK;
    } else if (r->error != SD_OK) {
        return;
    } else {
        r->cmd8_response = sd_be32(r->response + 1);
        if (r->cmd8_response != 0x1aa) {
            r->error = SD_VOLTAGE;
            return;
        }
        r->cmd8_valid = true;
    }

    enter_stage(r, ST_READY);
    r->ocr_request = OCR_V30 | (r->cmd8_valid ? OCR_CCS : 0);
    uint16_t start = sc_ticks();
    for (;;) {
        ++r->attempts;
        r->error = command(r, 55, 0, 6);
        if (r->error != SD_OK)
            return;
        r->error = command(r, 41, r->ocr_request, 6);
        if (r->error != SD_OK)
            return;
        r->ocr = sd_be32(r->response + 1);
        r->ocr_valid = true;
        if (!(r->ocr & OCR_V30)) {
            r->error = SD_VOLTAGE;
            return;
        }
        if (r->ocr & OCR_READY)
            break;
        if ((uint16_t)(sc_ticks() - start) >= INIT_TICKS || r->attempts >= 4096) {
            r->error = SD_TIMEOUT;
            return;
        }
    }
    r->block_addressed = r->cmd8_valid && (r->ocr & OCR_CCS);
    if (!r->cmd8_valid && (r->ocr & OCR_CCS)) {
        r->error = SD_ADDRESS_MISMATCH;
        return;
    }

    enter_stage(r, ST_CID);
    r->error = command(r, 2, 0, 17);
    if (r->response_size == 17) {
        for (unsigned i = 0; i < 16; ++i)
            r->cid[i] = r->response[i + 1];
        r->cid_captured = true;
    }
    if (r->error != SD_OK)
        return;
    r->cid_error = sd_decode_cid(r->cid, &r->identity);
    r->cid_valid = r->cid_error == SD_OK;

    enter_stage(r, ST_RCA);
    r->error = command(r, 3, 0, 6);
    if (r->error != SD_OK)
        return;
    r->rca = (uint16_t)((uint16_t)r->response[1] << 8 | r->response[2]);
    r->card_status = (uint16_t)((uint16_t)r->response[3] << 8 | r->response[4]);
    unsigned state = (r->card_status >> 9) & 15;
    /* R6 reports the state at command reception, before the RCA transition. */
    if (!r->rca || (r->card_status & 0xe000) || (state != 2 && state != 3)) {
        r->error = SD_CARD_ERROR;
        return;
    }

    enter_stage(r, ST_CSD);
    r->error = command(r, 9, (uint32_t)r->rca << 16, 17);
    if (r->response_size == 17) {
        for (unsigned i = 0; i < 16; ++i)
            r->csd[i] = r->response[i + 1];
        r->csd_captured = true;
    }
    if (r->error != SD_OK)
        return;
    r->csd_error = sd_decode_csd(r->csd, &r->capacity);
    r->csd_valid = r->csd_error == SD_OK;
    if (r->csd_valid && (r->capacity.structure == 1) != r->block_addressed) {
        r->csd_valid = false;
        r->csd_error = SD_ADDRESS_MISMATCH;
    }

    enter_stage(r, ST_SELECT);
    r->error = command(r, 7, (uint32_t)r->rca << 16, 6);
    if (r->error != SD_OK)
        return;
    if (!sc_wait_data_idle()) {
        r->error = SD_TIMEOUT;
        return;
    }
    enter_stage(r, ST_BUS_WIDTH);
    r->error = command(r, 55, (uint32_t)r->rca << 16, 6);
    if (r->error != SD_OK)
        return;
    r->error = command(r, 6, 2, 6);
    if (r->error != SD_OK)
        return;
    r->bus4 = true;
    if (!r->block_addressed) {
        enter_stage(r, ST_BLOCK_LENGTH);
        r->error = command(r, 16, 512, 6);
        if (r->error != SD_OK)
            return;
    }
    enter_stage(r, ST_DECODE);
    r->error = !r->cid_valid ? r->cid_error : r->csd_error;
    if (r->error == SD_OK) {
        r->failed_command = 0;
        enter_stage(r, ST_DONE);
    }
}
