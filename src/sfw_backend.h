/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SFW_BACKEND_H
#define SFW_BACKEND_H
#include <stdbool.h>
#include <stdint.h>

unsigned sfw_initialize(uint32_t *sectors, bool *block_addressed);
unsigned sfw_read(uint8_t *buffer, uint32_t sector, unsigned count);
unsigned sfw_write(const uint8_t *buffer, uint32_t sector, unsigned count);
bool sfw_sync(void);
void sfw_finish(void);
#endif
