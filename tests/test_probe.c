/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "transport.h"

unsigned sc_read_sector_raw(uint8_t buffer[512], uint8_t crc[8], unsigned timeout)
{
    (void)buffer; (void)crc; (void)timeout;
    assert(!"Base/extra probes must not read filesystem sectors");
    return 1;
}

static uint8_t current_cmd;
static uint32_t current_arg;
static unsigned sent[64], sent_count, ready_polls;
static int timeout_cmd, corrupt_cmd, rejected_cmd, busy_cmd;
static bool legacy, sdsc, never_ready, dat_busy, bad_cid, wrong_ocr, bad_echo;
static bool bad_app, bad_rca, wrong_addressing, unsupported;
static uint16_t ticks;
static uint8_t data_stream[160];
static unsigned data_index, data_count;
static bool data_timeout, bad_data_crc, bad_data_end, bad_data_start;
static bool frozen_ticks;
static unsigned data_polls;

static void reset_mock(void)
{
    memset(sent, 0, sizeof(sent));
    sent_count = ready_polls = 0;
    timeout_cmd = corrupt_cmd = rejected_cmd = busy_cmd = -1;
    legacy = sdsc = never_ready = dat_busy = bad_cid = wrong_ocr = bad_echo = false;
    bad_app = bad_rca = wrong_addressing = unsupported = false;
    ticks = 0;
    data_index = data_count = 0;
    data_timeout = bad_data_crc = bad_data_end = bad_data_start = false;
    frozen_ticks = false;
    data_polls = 0;
}

void sc_enable(void) {}
void sc_write_data_byte(uint8_t value) { (void)value; assert(false); }
bool sc_wait_write_ready(void) { assert(false); return false; }
void sc_timer_start(void) {}
uint16_t sc_ticks(void)
{
    if (frozen_ticks)
        return ticks;
    ticks = (uint16_t)(ticks + ((current_cmd == 51 || current_cmd == 13) ?
                                256 : 8192));
    return ticks;
}
bool sc_wait_data_idle(void) { return !dat_busy; }
void send_empty_clocks(unsigned count)
{
    assert(count != 0);
    if (current_cmd == 51 || current_cmd == 13)
        assert(data_index == data_count || data_timeout || bad_data_start ||
               current_cmd == timeout_cmd || current_cmd == rejected_cmd ||
               current_cmd == corrupt_cmd);
}
bool wait_sdcard_idle(unsigned timeout)
{
    assert(timeout != 0);
    return busy_cmd < 0;
}

void send_sdcard_commandbuf(const uint8_t *buffer, unsigned size)
{
    assert(size == 6 && sd_crc7(buffer, 5) == buffer[5]);
    assert(sent_count < sizeof(sent) / sizeof(sent[0]));
    current_cmd = buffer[0] & 63;
    current_arg = sd_be32(buffer + 1);
    sent[sent_count++] = current_cmd;
    /* Executable command allowlist: no block data or erase operations. */
    assert(current_cmd == 0 || current_cmd == 8 || current_cmd == 55 ||
           current_cmd == 41 || current_cmd == 2 || current_cmd == 3 ||
           current_cmd == 9 || current_cmd == 7 || current_cmd == 6 ||
           current_cmd == 16 || current_cmd == 51 || current_cmd == 13);
    if (current_cmd == 41) {
        bool cmd8_timed_out = legacy || timeout_cmd == 8;
        assert(current_arg == (cmd8_timed_out ? 0x00040000u : 0x40040000u));
    }
    if (current_cmd == 9 || current_cmd == 7)
        assert(current_arg == 0x12340000u);
    if (current_cmd == 6)
        assert(current_arg == 2);
    if (current_cmd == 16)
        assert(sdsc && current_arg == 512);
    if (current_cmd == 51 || current_cmd == 13)
        assert(current_arg == 0 && sent_count >= 2 && sent[sent_count - 2] == 55);
}

static void prepare_data(void)
{
    unsigned size = current_cmd == 51 ? 8 : 64;
    /* Every lane carries 16 or 128 one bits; CRC16 reference values are
     * 0x1D0F and 0x0041 respectively, computed independently of our decoder.
     */
    unsigned crc = size == 8 ? 0x1d0f : 0x0041;
    data_index = data_count = 0;
    data_stream[data_count++] = 15;
    data_stream[data_count++] = bad_data_start ? 14 : 0;
    for (unsigned i = 0; i < size * 2; ++i)
        data_stream[data_count++] = 15;
    for (unsigned i = 16; i != 0; --i)
        data_stream[data_count++] = (crc & (1u << (i - 1))) ? 15 : 0;
    if (bad_data_crc)
        data_stream[data_count - 1] ^= 1;
    data_stream[data_count++] = bad_data_end ? 0 : 15;
}

uint8_t sc_data_nibble(void)
{
    ++data_polls;
    if (data_timeout)
        return 15;
    assert(data_index < data_count);
    return data_stream[data_index++];
}

static void short_response(uint8_t *buffer, uint32_t value)
{
    buffer[0] = current_cmd == 41 ? 0x3f : current_cmd;
    buffer[1] = (uint8_t)(value >> 24);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 8);
    buffer[4] = (uint8_t)value;
    buffer[5] = current_cmd == 41 ? 0xff : sd_crc7(buffer, 5);
}

bool receive_sdcard_response(uint8_t *buffer, unsigned size, unsigned timeout)
{
    assert(timeout != 0);
    if (current_cmd == timeout_cmd || (legacy && current_cmd == 8))
        return false;
    memset(buffer, 0, size);
    switch (current_cmd) {
    case 8: short_response(buffer, bad_echo ? 0x1ab : 0x1aa); break;
    case 55: short_response(buffer, bad_app ? 0 : 0x20); break;
    case 41: {
        ++ready_polls;
        uint32_t value = wrong_ocr ? 0x00100000u : 0x00040000u;
        if (!never_ready && ready_polls >= 2)
            value |= 0x80000000u;
        if (!sdsc)
            value |= 0x40000000u;
        short_response(buffer, value);
        break;
    }
    case 2: {
        assert(size == 17);
        static const uint8_t cid[16] = {0x27,0x50,0x48,0x53,0x44,0x33,0x32,0x47,
                                       0x30,0x01,0xb4,0x4e,0xed,0,0xf2,0x21};
        buffer[0] = 0x3f;
        memcpy(buffer + 1, cid, 16);
        if (bad_cid) {
            buffer[15] &= 0xf0;
            buffer[16] = sd_crc7(buffer + 1, 15);
        }
        break;
    }
    case 3: short_response(buffer, bad_rca ? 0x00000400u : 0x12340400u); break;
    case 9:
        assert(size == 17);
        buffer[0] = 0x3f;
        buffer[1] = unsupported ? 0x80 : (sdsc != wrong_addressing) ? 0 : 0x40;
        buffer[6] = 9;
        buffer[16] = sd_crc7(buffer + 1, 15);
        break;
    case 51:
    case 13:
        short_response(buffer, 0);
        prepare_data();
        break;
    default: short_response(buffer, 0); break;
    }
    if (current_cmd == rejected_cmd)
        short_response(buffer, 1u << 22);
    if (current_cmd == corrupt_cmd)
        buffer[size - 1] ^= 2;
    return true;
}

static void extra_cases(void)
{
    SdReport r;
    reset_mock();
    sd_probe(&r);
    SdReport original = r;
    sd_probe_extra(&r);
    assert(r.error == SD_OK && r.extra_error == SD_OK && r.extras_attempted);
    assert(r.scr_captured && r.status_captured);
    assert(r.scr_error == SD_OK && r.status_error == SD_OK);
    assert(memcmp(r.cid, original.cid, 16) == 0);
    assert(memcmp(r.csd, original.csd, 16) == 0);
    assert(memcmp(r.response, original.response, 17) == 0);
    const unsigned extra[] = {55,51,55,13};
    assert(memcmp(sent + sent_count - 4, extra, sizeof(extra)) == 0);
    assert(sd_bits(r.scr, 8, 0, 32) == UINT32_MAX);
    assert(sd_bits(r.status, 64, 448, 32) == UINT32_MAX);

    reset_mock(); sd_probe(&r); bad_data_crc = true;
    sd_probe_extra(&r);
    assert(r.error == SD_OK && r.extra_error == SD_CRC);
    assert(r.extra_stage == ST_SCR && r.scr_captured && !r.status_captured);
    reset_mock(); sd_probe(&r); bad_data_end = true;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_FRAME && r.scr_captured);
    reset_mock(); sd_probe(&r); bad_data_start = true;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_FRAME && !r.scr_captured);
    reset_mock(); sd_probe(&r); data_timeout = true;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_TIMEOUT && !r.scr_captured);
    assert(data_polls == 64);
    reset_mock(); sd_probe(&r); data_timeout = frozen_ticks = true;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_TIMEOUT && data_polls == 262144);
    reset_mock(); sd_probe(&r); corrupt_cmd = 51;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_CRC && !r.scr_captured && data_polls == 0);
    reset_mock(); sd_probe(&r); rejected_cmd = 51;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_CARD_ERROR && !r.scr_captured);
    reset_mock(); sd_probe(&r); timeout_cmd = 13;
    sd_probe_extra(&r);
    assert(r.extra_error == SD_TIMEOUT && r.scr_captured && !r.status_captured);
    assert(r.extra_stage == ST_SD_STATUS && r.extra_failed_command == 13);
    reset_mock(); bad_echo = true; sd_probe(&r);
    unsigned count = sent_count;
    sd_probe_extra(&r);
    assert(!r.extras_attempted && sent_count == count);
}

static void success_cases(void)
{
    SdReport report;
    reset_mock();
    sd_probe(&report);
    assert(report.error == SD_OK && report.stage == ST_DONE);
    assert(report.cid_valid && report.csd_valid && report.ocr_valid);
    assert(report.block_addressed && report.bus4 && report.rca == 0x1234);
    assert(report.attempts == 2);
    const unsigned expected[] = {0,8,55,41,55,41,2,3,9,7,55,6};
    assert(sent_count == sizeof(expected) / sizeof(expected[0]));
    assert(memcmp(sent, expected, sizeof(expected)) == 0);
    reset_mock();
    legacy = sdsc = true;
    sd_probe(&report);
    assert(report.error == SD_OK && report.cmd8_legacy);
    assert(!report.cmd8_valid && !report.block_addressed);
    assert(sent[sent_count - 1] == 16);
    reset_mock();
    sdsc = true;
    sd_probe(&report);
    assert(report.error == SD_OK && report.cmd8_valid && !report.block_addressed);
}

static void failure_cases(void)
{
    SdReport r;
    reset_mock(); never_ready = true;
    sd_probe(&r);
    assert(r.error == SD_TIMEOUT && r.stage == ST_READY && ready_polls == 4);
    assert(!r.cid_captured && r.ocr_valid);
    reset_mock(); corrupt_cmd = 2;
    sd_probe(&r);
    assert(r.error == SD_CRC && r.stage == ST_CID && r.cid_captured && !r.cid_valid);
    reset_mock(); timeout_cmd = 9;
    sd_probe(&r);
    assert(r.error == SD_TIMEOUT && r.stage == ST_CSD && r.cid_valid && !r.csd_captured);
    reset_mock(); rejected_cmd = 6;
    sd_probe(&r);
    assert(r.error == SD_CARD_ERROR && r.stage == ST_BUS_WIDTH && !r.bus4);
    reset_mock(); dat_busy = true;
    sd_probe(&r);
    assert(r.error == SD_TIMEOUT && r.stage == ST_SELECT);
    reset_mock(); bad_echo = true;
    sd_probe(&r);
    assert(r.error == SD_VOLTAGE && r.stage == ST_CMD8);
    reset_mock(); wrong_ocr = true;
    sd_probe(&r);
    assert(r.error == SD_VOLTAGE && r.stage == ST_READY);
    reset_mock(); bad_app = true;
    sd_probe(&r);
    assert(r.error == SD_CARD_ERROR && r.failed_command == 55);
    reset_mock(); bad_rca = true;
    sd_probe(&r);
    assert(r.error == SD_CARD_ERROR && r.stage == ST_RCA);
    reset_mock(); bad_cid = true;
    sd_probe(&r);
    assert(r.error == SD_BAD_FIELD && r.stage == ST_DECODE);
    assert(r.cid_captured && !r.cid_valid && r.csd_valid);
    reset_mock(); wrong_addressing = true;
    sd_probe(&r);
    assert(r.error == SD_ADDRESS_MISMATCH && !r.csd_valid);
    reset_mock(); unsupported = true;
    sd_probe(&r);
    assert(r.error == SD_UNSUPPORTED_CSD && r.csd_captured && !r.csd_valid);
    reset_mock(); busy_cmd = 0;
    sd_probe(&r);
    assert(r.error == SD_BUS_BUSY && sent_count == 0);
    /* Retrying must erase stale data and flags from the previous attempt. */
    reset_mock();
    sd_probe(&r);
    assert(r.error == SD_OK && r.cid_valid && r.csd_valid && r.bus4);
    reset_mock(); timeout_cmd = 8;
    sd_probe(&r);
    assert(r.error == SD_ADDRESS_MISMATCH && !r.cid_captured && !r.csd_valid);
    assert(r.cmd8_legacy && !r.cmd8_valid && !r.bus4);
}

int main(void)
{
    success_cases();
    failure_cases();
    extra_cases();
    puts("Mocked driver tests passed; no hardware behavior is implied.");
    return 0;
}
