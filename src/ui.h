/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UI_H
#define UI_H

#include "sd.h"
#include "report.h"

#define UI_DATA_PAGES 2u
#define UI_ERROR_PAGE UI_DATA_PAGES

#define UI_PAGE_COUNT(r) \
    (UI_DATA_PAGES + ((r)->error != SD_OK ? 1u : 0u))

void ui_splash(void);
void ui_report(const SdReport *report, unsigned page);
void ui_save_confirm(void);
void ui_save_progress(void);
void ui_save_result(FRESULT result, SdError error);
uint8_t font_row(unsigned char c, unsigned row);

#endif
