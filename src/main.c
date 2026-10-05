/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gba.h"
#include "ui.h"

static unsigned wait_action(unsigned mask)
{
    while (((unsigned)(~KEYINPUT) & (KEY_A | KEY_B)) != 0)
        gba_wait_frame();
    for (;;) {
        gba_wait_frame();
        unsigned keys = (unsigned)(~KEYINPUT) & mask;
        if (keys)
            return keys;
    }
}

int main(void)
{
    gba_init();
    ui_splash();
    /* GBATEK: gbatimers. Keep Timer 0 free for the SD driver's timeouts. */
    REG16(0x04000106) = 0;
    REG16(0x0400010a) = 0;
    REG16(0x04000104) = 0;
    REG16(0x04000108) = 0;
    REG16(0x0400010a) = 0x0084;
    REG16(0x04000106) = 0x0083;
    SdReport report;
    sd_probe(&report);
    sd_probe_extra(&report);
    while (REG16(0x04000108) == 0 && REG16(0x04000104) < 32768)
        gba_wait_frame();
    REG16(0x04000106) = 0;
    REG16(0x0400010a) = 0;
    unsigned page = report.error == SD_OK ? 0 : UI_ERROR_PAGE;
    unsigned previous = (unsigned)(~KEYINPUT) & 0x03ff;
    ui_report(&report, page);
    for (;;) {
        gba_wait_frame();
        unsigned held = (unsigned)(~KEYINPUT) & 0x03ff;
        unsigned pressed = held & ~previous;
        previous = held;
        if (!pressed)
            continue;
        unsigned pages = UI_PAGE_COUNT(&report);
        if (pressed & KEY_SELECT) {
            ui_save_confirm();
            if (!(wait_action(KEY_A | KEY_B) & KEY_B)) {
                ui_save_progress();
                FRESULT result = report_save(&report);
                ui_save_result(result, storage_error());
                wait_action(KEY_A);
            }
            previous = (unsigned)(~KEYINPUT) & 0x03ff;
        } else if (pressed & (KEY_RIGHT | KEY_R)) {
            page = (page + 1) % pages;
        } else if (pressed & (KEY_LEFT | KEY_L)) {
            page = (page + pages - 1) % pages;
        } else {
            continue;
        }
        ui_report(&report, page);
    }
}
