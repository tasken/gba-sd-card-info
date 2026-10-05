/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "report.h"

typedef struct {
    char *data;
    unsigned used, capacity;
    bool ok;
} Text;

static void append(Text *t, const char *s)
{
    while (*s) {
        if (t->used + 1 >= t->capacity) {
            t->ok = false;
            return;
        }
        t->data[t->used++] = *s++;
    }
    if (t->capacity)
        t->data[t->used] = 0;
}

static void number(Text *t, uint64_t value, unsigned base, unsigned digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[65];
    unsigned count = 0;
    do {
        buffer[count++] = hex[value % base];
        value /= base;
    } while (value || count < digits);
    while (count) {
        char s[] = {buffer[--count], 0};
        append(t, s);
    }
}

static void field_label(Text *t, const char *label)
{
    unsigned width = 1;
    for (const char *p = label; *p; ++p)
        ++width;
    append(t, label);
    append(t, ":");
    do {
        append(t, " ");
    } while (++width < 32);
}

static void value(Text *t, const char *label, uint64_t n, unsigned base,
                  unsigned digits)
{
    field_label(t, label);
    if (base == 16)
        append(t, "0x");
    number(t, n, base, digits);
    append(t, "\r\n");
}

static void message(Text *t, const char *label, const char *s)
{
    field_label(t, label);
    append(t, s);
    append(t, "\r\n");
}

static void section(Text *t, const char *title)
{
    append(t, "\r\n\r\n.:[ ");
    append(t, title);
    append(t, " ]:.\r\n\r\n");
}

static const char *read_result(SdError error)
{
    return error == SD_OK ? "Completed" : sd_error_name(error);
}

static void dump(Text *t, const char *label, const uint8_t *data, unsigned size,
                 bool captured)
{
    if (!captured)
        return;
    if (t->used < 4 || t->data[t->used - 4] != '\r' ||
        t->data[t->used - 3] != '\n' || t->data[t->used - 2] != '\r' ||
        t->data[t->used - 1] != '\n')
        append(t, "\r\n");
    append(t, "[ ");
    append(t, label);
    append(t, " ]\r\n");
    append(t, "\r\n");
    for (unsigned offset = 0; offset < size; offset += 16) {
        number(t, offset, 16, 2);
        append(t, ": ");
        unsigned end = size < offset + 16 ? size : offset + 16;
        for (unsigned i = offset; i < end; ++i) {
            if (i != offset)
                append(t, " ");
            number(t, data[i], 16, 2);
        }
        append(t, "\r\n");
    }
}

bool report_format(const SdReport *r, char *buffer, unsigned size,
                   unsigned *length)
{
    Text t = {buffer, 0, size, size != 0};
    append(&t, "..:::[ GBA SD CARD INFO REPORT ]:::..\r\n");
    section(&t, "Card identity");
    message(&t, "Brand", "Unknown");
    if (r->cid_valid) {
        message(&t, "Product", r->identity.product);
        value(&t, "Serial", r->identity.serial, 16, 8);
        field_label(&t, "Manufactured");
        number(&t, r->identity.year, 10, 4);
        append(&t, "-");
        number(&t, r->identity.month, 10, 2);
        append(&t, "\r\n");
        field_label(&t, "Revision");
        number(&t, r->identity.revision_major, 10, 1);
        append(&t, ".");
        number(&t, r->identity.revision_minor, 10, 1);
        append(&t, "\r\n");
    } else {
        message(&t, "Identity", "Unavailable");
    }
    section(&t, "Card capacity");
    if (r->csd_valid) {
        message(&t, "Card type", r->capacity.structure == 0 ? "SD (SDSC)" :
                r->capacity.bytes > UINT64_C(34359738368) ? "SDXC" : "SDHC");
        field_label(&t, "Capacity");
        uint64_t tenths = r->capacity.bytes / UINT64_C(100000000);
        number(&t, tenths / 10, 10, 1);
        append(&t, ".");
        number(&t, tenths % 10, 10, 1);
        append(&t, " GB\r\n");
        value(&t, "Capacity bytes", r->capacity.bytes, 10, 1);
        value(&t, "Capacity MiB (whole)", r->capacity.bytes >> 20, 10, 1);
        value(&t, "512-byte sectors", r->capacity.sectors, 10, 1);
        message(&t, "Write lock", sd_bits(r->csd, 16, 12, 2) ?
                "Reported locked" : "Not reported");
    } else {
        message(&t, "Capacity", "Unavailable");
    }
    section(&t, "Card capabilities");
    bool status_ok = r->status_captured && r->status_error == SD_OK;
    message(&t, "Speed class", status_ok ?
            sd_speed_class_name(sd_bits(r->status, 64, 440, 8)) : "Unavailable");
    message(&t, "UHS class", status_ok ?
            sd_uhs_class_name(sd_bits(r->status, 64, 396, 4)) : "Unavailable");
    section(&t, "Technical details");
    if (r->error != SD_OK)
        message(&t, "Base stage", sd_stage_name(r->stage));
    if (r->response_size == 6 && r->response[0] != 0x3f)
        value(&t, "Last base response command", r->response[0] & 0x3f, 10, 1);
    if (r->error != SD_OK)
        value(&t, "Failed base command", r->failed_command, 10, 1);
    if (r->extras_attempted) {
        if (r->extra_error != SD_OK)
            message(&t, "Extra stage", sd_stage_name(r->extra_stage));
        value(&t, "Last extra command", r->extra_failed_command, 10, 1);
    }
    value(&t, "Card status", r->card_status, 16, 8);
    if (r->cid_captured) {
        value(&t, "MID (8-bit binary number)", r->cid[0], 16, 2);
        value(&t, "OID (16-bit, two ASCII bytes)",
              ((unsigned)r->cid[1] << 8) | r->cid[2], 16, 4);
        char oid[3];
        for (unsigned i = 0; i < 2; ++i)
            oid[i] = r->cid[i+1] >= 32 && r->cid[i+1] <= 126 ?
                     (char)r->cid[i+1] : '?';
        oid[2] = 0;
        message(&t, "OID ASCII", oid);
    }
    if (r->csd_valid)
        value(&t, "CSD structure", r->capacity.structure, 10, 1);
    section(&t, "Initialization and protocol");
    message(&t, "Addressing", !r->ocr_valid ? "not established" :
            r->block_addressed ? "block (SDHC/SDXC)" : "byte (SDSC)");
    message(&t, "CMD8", r->cmd8_valid ? "echo verified" :
            r->cmd8_legacy ? "timeout, legacy path" : "not verified");
    value(&t, "CMD8 response", r->cmd8_response, 16, 8);
    message(&t, "OCR capture", r->ocr_valid ? "yes" : "no");
    value(&t, "OCR", r->ocr, 16, 8);
    value(&t, "OCR request", r->ocr_request, 16, 8);
    value(&t, "Initialization attempts", r->attempts, 10, 1);
    value(&t, "RCA", r->rca, 16, 4);
    message(&t, "4-bit bus request", r->bus4 ? "accepted" : "not completed");
    bool base_response_ok = r->response_size &&
        sd_check_response(r->response_size == 17 ? 9 :
                          r->response[0] == 0x3f ? 41 : r->response[0] & 0x3f,
                          r->response, r->response_size) == SD_OK;
    bool extra_response_ok = r->extra_response_size &&
        sd_check_response(r->extra_failed_command, r->extra_response,
                          r->extra_response_size) == SD_OK;
    section(&t, "Read and validation results");
    message(&t, "Card read", read_result(r->error));
    message(&t, "Extra details", !r->extras_attempted ? "Not read" :
            r->extra_error != SD_OK ? sd_error_name(r->extra_error) :
            r->scr_captured && r->scr_error == SD_OK && status_ok ?
            "Complete" : "Partial");
    message(&t, "CID", !r->cid_captured ? "Not captured" :
            r->cid_valid ? "Passed" : r->cid_error != SD_OK ?
            sd_error_name(r->cid_error) : r->error != SD_OK ?
            sd_error_name(r->error) : "Not validated");
    message(&t, "CSD", !r->csd_captured ? "Not captured" :
            r->csd_valid ? "Passed" : r->csd_error != SD_OK ?
            sd_error_name(r->csd_error) : r->error != SD_OK ?
            sd_error_name(r->error) : "Not validated");
    message(&t, "SCR", !r->scr_captured ? "Not captured" :
            r->scr_error == SD_OK ? "Passed" : sd_error_name(r->scr_error));
    message(&t, "SD Status", !r->status_captured ? "Not captured" :
            r->status_error == SD_OK ? "Passed" : sd_error_name(r->status_error));
    message(&t, "Last base response", !r->response_size ? "Not captured" :
            base_response_ok ? "Passed" : "CRC/framing check failed");
    message(&t, "Last extra response", !r->extra_response_size ? "Not captured" :
            extra_response_ok ? "Passed" : "CRC/framing check failed");
    if (!sd_report_complete(r) ||
        (r->response_size && !base_response_ok) ||
        (r->extra_response_size && !extra_response_ok)) {
        section(&t, "Raw data for troubleshooting");
        dump(&t, "CID", r->cid, 16, r->cid_captured);
        dump(&t, "CSD", r->csd, 16, r->csd_captured);
        dump(&t, "SCR", r->scr, 8, r->scr_captured);
        dump(&t, "SCR packed CRC16", r->scr_crc, 8, r->scr_captured);
        dump(&t, "SD Status", r->status, 64, r->status_captured);
        dump(&t, "SD Status packed CRC16", r->status_crc, 8, r->status_captured);
        dump(&t, "Last base response", r->response, r->response_size,
             r->response_size != 0);
        dump(&t, "Last extra response", r->extra_response, r->extra_response_size,
             r->extra_response_size != 0);
    }
    section(&t, "App information");
    message(&t, "Build commit", BUILD_COMMIT);
    if (BUILD_TAG[0])
        message(&t, "Release", BUILD_TAG);
    message(&t, "Driver", DRIVER_REVISION);
    message(&t, "Export driver", "SuperFW v0.21 (SUPERCHIS_IO)");
    *length = t.used;
    return t.ok;
}
