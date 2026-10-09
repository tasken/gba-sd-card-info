/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gba.h"
#include "ui.h"

/* GBATEK: lcdvrambitmapbgmodes, lcdvramoverview, lcdiodisplaycontrol. */
static unsigned draw_page = 1;
static const unsigned body_y = 35;
static const unsigned row_height = 14;
static const char hex[] = "0123456789ABCDEF";
enum { COLOR_WARNING = 2, COLOR_ERROR = 3, COLOR_INFO = 4 };
static volatile uint16_t *draw_buffer =
    (volatile uint16_t *)(uintptr_t)0x0600a000;

static void pixel(unsigned x, unsigned y, uint8_t color)
{
    unsigned index = y * 240 + x;
    unsigned shift = (index & 1) * 8;
    uint16_t pair = draw_buffer[index / 2];
    draw_buffer[index / 2] =
        (uint16_t)((pair & ~(0xffu << shift)) | ((unsigned)color << shift));
}

static void text_color(unsigned x, unsigned y, const char *s, uint8_t color)
{
    for (; *s && x + 5 < 240; ++s, x += 6) {
        for (unsigned row = 0; row < 7; ++row) {
            uint8_t bits = font_row((unsigned char)*s, row);
            for (unsigned col = 0; col < 5; ++col)
                if (bits & (16u >> col))
                    pixel(x + col, y + row, color);
        }
    }
}

static void text(unsigned x, unsigned y, const char *s)
{
    text_color(x, y, s, 1);
}

static void line(unsigned row, const char *s)
{
    text(6, body_y + row_height * row, s);
}

static void field(unsigned row, const char *label, const char *value)
{
    line(row, label);
    text(96, body_y + row_height * row, value);
}

static void note(unsigned lines_from_bottom, const char *s, uint8_t color)
{
    text_color(6, 119 - 14 * lines_from_bottom, s, color);
}

static void hex_field(unsigned row, const char *label, uint32_t value,
                      unsigned digits)
{
    char buffer[9];
    buffer[digits] = 0;
    for (unsigned i = digits; i; --i) {
        buffer[i - 1] = hex[value & 15];
        value >>= 4;
    }
    field(row, label, buffer);
}

static void decimal_text(char buffer[21], uint64_t value)
{
    char reverse[20];
    unsigned count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    unsigned length = count;
    for (unsigned i = 0; i < length; ++i)
        buffer[i] = reverse[--count];
    buffer[length] = 0;
}

static void decimal_field(unsigned row, const char *label, uint64_t value)
{
    char buffer[21];
    decimal_text(buffer, value);
    field(row, label, buffer);
}

static void capacity_field(unsigned row, uint64_t bytes)
{
    char buffer[21];
    uint64_t tenths = bytes / UINT64_C(100000000);
    decimal_text(buffer, tenths / 10);
    unsigned length = 0;
    while (buffer[length])
        ++length;
    buffer[length++] = '.';
    buffer[length++] = (char)('0' + tenths % 10);
    buffer[length++] = ' ';
    buffer[length++] = 'G';
    buffer[length++] = 'B';
    buffer[length] = 0;
    field(row, "Capacity:", buffer);
}

static void layout_begin(const char *title, const char *footer)
{
    for (unsigned i = 0; i < 240 * 160 / 2; ++i)
        draw_buffer[i] = 0;
    text(6, 4, title);
    for (unsigned x = 6; x < 234; ++x)
        pixel(x, 17, 1);
    if (footer[0]) {
        for (unsigned x = 6; x < 234; ++x)
            pixel(x, 143, 1);
        text(6, 149, footer);
    }
}

static void text_right(unsigned y, const char *s)
{
    unsigned length = 0;
    while (s[length])
        ++length;
    text(234 - 6 * length, y, s);
}

static void layout_end(void)
{
    gba_wait_frame();
    DISPCNT = (uint16_t)(0x0404 | (draw_page << 4));
    draw_page ^= 1;
    draw_buffer = (volatile uint16_t *)(uintptr_t)
        (0x06000000u + draw_page * 0xa000u);
}

void ui_splash(void)
{
    for (unsigned i = 0; i < 240 * 160 / 2; ++i)
        draw_buffer[i] = 0;
    text(18, 23, "GBA SD Card Info");
    text(18, 47, "By Tasken");
    const char *release = BUILD_TAG[0] ? BUILD_TAG : "Development build";
    char tag[24];
    unsigned i = 0;
    unsigned source = 0;
    while (release[source] && i < sizeof(tag) - 1) {
        if ((unsigned char)release[source] == 0xc3 &&
            (unsigned char)release[source + 1] == 0xb3) {
            tag[i] = 'o';
            source += 2;
        } else {
            tag[i] = release[source++];
        }
        ++i;
    }
    if (release[source])
        tag[i - 1] = '.';
    tag[i] = 0;
    text_color(18, 89, tag, COLOR_INFO);
    text(18, 103, BUILD_COMMIT);
    text_color(18, 131, "Reading card...", COLOR_INFO);
    layout_end();
}

void ui_save_confirm(void)
{
    layout_begin("Save SDINFO.TXT?",
                 "A: save");
    text_right(149, "B: cancel");
    field(0, "Location:", "Card root");
    note(0, "Replaces SDINFO.TXT if it exists.", COLOR_WARNING);
    layout_end();
}

void ui_save_progress(void)
{
    layout_begin("Saving SDINFO.TXT", "");
    layout_end();
}

static const char *save_error_message(FRESULT result)
{
    switch (result) {
    case FR_DISK_ERR: return "Could not access the card.";
    case FR_NOT_READY: return "Could not connect to the card.";
    case FR_NO_FILESYSTEM: return "No FAT16/FAT32 volume was found.";
    case FR_DENIED: return "Card is full or file is blocked.";
    case FR_WRITE_PROTECTED: return "Card does not allow saving.";
    case FR_INT_ERR: return "Could not prepare or save the report.";
    case FR_INVALID_NAME: return "Could not use the report filename.";
    default: return "Could not save the report.";
    }
}

static const char *save_recovery(FRESULT result)
{
    switch (result) {
    case FR_NO_FILESYSTEM: return "Use a FAT16 or FAT32 card.";
    case FR_DENIED: return "Free space or check file access.";
    case FR_WRITE_PROTECTED: return "Use a writable card.";
    case FR_INT_ERR:
    case FR_INVALID_NAME: return "Try again. If it fails, save a photo.";
    default: return "Check the card with a card reader.";
    }
}

void ui_save_result(FRESULT result, SdError error)
{
    layout_begin(result == FR_OK ? "Report saved" : "Report not saved",
                 "A: return to SD info");
    if (result == FR_OK) {
        field(0, "File:", "SDINFO.TXT");
        field(1, "Location:", "Card root");
    } else {
        unsigned row = 0;
        if (error != SD_OK) {
            const StorageDebug *debug = storage_debug();
            static const char *const phases[] = {
                "Could not connect to the card.", "Card did not accept the request.",
                "Could not read the card.", "Could not write to the card."
            };
            text_color(6, body_y + row_height * row++, phases[debug->phase],
                       COLOR_ERROR);
            note(debug->write_attempted ? 2 : 1, "Power off before removing the card.",
                 COLOR_INFO);
            note(debug->write_attempted ? 1 : 0,
                 "Check the card with a card reader.", COLOR_INFO);
            if (debug->write_attempted)
                note(0, "Card files may have changed.", COLOR_WARNING);
            /* Keep short support codes while omitting raw protocol dumps. */
            decimal_field(row++, "Card error:", debug->driver_error);
            if (debug->sector_valid)
                decimal_field(row++, "Sector:", debug->sector);
        } else if (result == FR_NO_FILESYSTEM) {
            text_color(6, body_y + row_height * row++, save_error_message(result),
                       COLOR_ERROR);
            line(row++, "No files were changed.");
            note(2, "Power off before removing the card.", COLOR_INFO);
            note(1, "Check the format with a card reader.", COLOR_INFO);
            note(0, save_recovery(result), COLOR_INFO);
        } else {
            text_color(6, body_y + row_height * row++, save_error_message(result),
                       COLOR_ERROR);
            note(3, "Power off before removing the card.", COLOR_INFO);
            note(2, save_recovery(result), COLOR_INFO);
            note(1, "Report may be missing or incomplete.", COLOR_WARNING);
            note(0, "Old SDINFO.TXT may have been replaced.", COLOR_WARNING);
        }
        decimal_field(row, "File error:", result);
    }
    layout_end();
}

static void identity_page(const SdReport *r)
{
    if (r->cid_captured) {
        char mid[] = "0x00";
        mid[2] = hex[r->cid[0] >> 4];
        mid[3] = hex[r->cid[0] & 15];
        field(5, "MID:", mid);
        char oid[] = "0x0000 (  )";
        for (unsigned i = 0; i < 2; ++i) {
            uint8_t byte = r->cid[i + 1];
            oid[2 + 2 * i] = hex[byte >> 4];
            oid[3 + 2 * i] = hex[byte & 15];
            oid[8 + i] = byte >= 32 && byte <= 126 ? (char)byte : '?';
        }
        field(6, "OID:", oid);
    } else {
        field(5, "MID:", "Unavailable");
        field(6, "OID:", "Unavailable");
    }
    if (!r->cid_valid) {
        field(0, "Product:", "Unavailable");
        field(1, "Serial:", "Unavailable");
        field(2, "Manufactured:", "Unavailable");
        field(3, "Revision:", "Unavailable");
        field(4, "Brand:", "Unknown");
        return;
    }
    field(0, "Product:", r->identity.product);
    hex_field(1, "Serial:", r->identity.serial, 8);
    char date[] = "0000-00";
    unsigned year = r->identity.year;
    for (unsigned i = 4; i; --i) {
        date[i - 1] = (char)('0' + year % 10);
        year /= 10;
    }
    date[5] = (char)('0' + r->identity.month / 10);
    date[6] = (char)('0' + r->identity.month % 10);
    field(2, "Manufactured:", date);
    char revision[21], minor[21];
    decimal_text(revision, r->identity.revision_major);
    decimal_text(minor, r->identity.revision_minor);
    unsigned length = 0;
    while (revision[length])
        ++length;
    revision[length++] = '.';
    for (unsigned i = 0; minor[i]; ++i)
        revision[length++] = minor[i];
    revision[length] = 0;
    field(3, "Revision:", revision);
    field(4, "Brand:", "Unknown");
}

static void capacity_page(const SdReport *r)
{
    if (!r->csd_valid) {
        field(0, "Capacity:", "Unavailable");
        field(1, "Card type:", "Unavailable");
        field(2, "Write lock:", "Unavailable");
    } else {
        capacity_field(0, r->capacity.bytes);
        field(1, "Card type:", r->capacity.structure == 0 ? "SD (SDSC)" :
              r->capacity.bytes > UINT64_C(34359738368) ? "SDXC" : "SDHC");
        field(2, "Write lock:", sd_bits(r->csd, 16, 12, 2) ? "Reported locked" :
                                                                    "Not reported");
    }
    unsigned row = 3;
    bool status_ok = r->status_captured && r->status_error == SD_OK;
    if (status_ok) {
        unsigned speed = sd_bits(r->status, 64, 440, 8);
        field(row++, "Speed class:", sd_speed_class_name(speed));
        unsigned uhs = sd_bits(r->status, 64, 396, 4);
        field(row++, "UHS class:", sd_uhs_class_name(uhs));
    } else {
        field(row++, "Speed class:", "Unavailable");
        field(row++, "UHS class:", "Unavailable");
    }
    if (!sd_report_complete(r)) {
        field(row, "Extra info:", !r->extras_attempted ? "Not read" : "Incomplete");
    }
}

static void failure_page(const SdReport *r)
{
    const char *message;
    switch (r->error) {
    case SD_TIMEOUT: message = "Card did not respond."; break;
    case SD_BUS_BUSY: message = "Could not connect to the card."; break;
    case SD_FRAME:
    case SD_CRC: message = "Card data could not be verified."; break;
    case SD_CARD_ERROR: message = "Card did not accept the request."; break;
    case SD_VOLTAGE: message = "Card could not be initialized."; break;
    case SD_UNSUPPORTED_CSD: message = "Card format is not supported."; break;
    case SD_BAD_FIELD: message = "Card reported invalid details."; break;
    case SD_ADDRESS_MISMATCH: message = "Card size could not be verified."; break;
    default: message = "Could not read the card."; break;
    }
    text_color(6, body_y, message, COLOR_ERROR);
    field(1, "Step:", sd_stage_name(r->stage));
    note(2, "Power off. Remove and reinsert card.", COLOR_INFO);
    note(1, "Launch the app again.", COLOR_INFO);
    note(0, "Still failing? Check with a reader.", COLOR_INFO);
}

void ui_report(const SdReport *r, unsigned page)
{
    static const char *const titles[] = {
        "Card identity", "Capacity & capabilities"
    };
    layout_begin(page == UI_ERROR_PAGE ? "Card read failed" : titles[page],
                 "L/R: pages");
    text_right(149, "SELECT: save report");
    char number[] = "01/02";
    number[1] = (char)('1' + page);
    number[4] = (char)('0' + UI_PAGE_COUNT(r));
    text_right(4, number);
    if (page == 0)
        identity_page(r);
    else if (page == 1)
        capacity_page(r);
    else
        failure_page(r);
    layout_end();
}
