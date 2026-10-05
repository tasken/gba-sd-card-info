/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "sd.h"

void sc_enable(void);
void send_empty_clocks(unsigned count);
bool wait_sdcard_idle(unsigned timeout);
bool receive_sdcard_response(uint8_t *buffer, unsigned size, unsigned timeout);
void send_sdcard_commandbuf(const uint8_t *buffer, unsigned size);
bool sc_wait_data_idle(void);
void sc_timer_start(void);
uint16_t sc_ticks(void);
uint8_t sc_data_nibble(void);
SdError sc_read_register(uint8_t *buffer, unsigned size, uint8_t crc[8],
                         bool *captured);
void sc_write_data_byte(uint8_t value);
bool sc_wait_write_ready(void);
SdError sc_write_block(const uint8_t buffer[512]);
SdError sd_sector_command(uint8_t cmd, uint32_t arg, uint32_t *status);
const SdReport *sd_sector_response(void);
unsigned sc_read_sector_raw(uint8_t buffer[512], uint8_t crc[8], unsigned timeout);
SdError sc_read_sector(uint8_t buffer[512], uint8_t crc[8], bool *captured);

#endif
