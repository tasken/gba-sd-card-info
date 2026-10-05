/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sd.h"

bool sd_report_complete(const SdReport *report)
{
    return report->error == SD_OK && report->cid_valid && report->csd_valid &&
           report->extras_attempted && report->extra_error == SD_OK &&
           report->scr_captured && report->scr_error == SD_OK &&
           report->status_captured && report->status_error == SD_OK;
}

const char *sd_speed_class_name(unsigned code)
{
    static const char *const names[] = {
        "Class 0", "Class 2", "Class 4", "Class 6", "Class 10"
    };
    return code < sizeof(names) / sizeof(names[0]) ? names[code] : "Unknown code";
}

const char *sd_uhs_class_name(unsigned code)
{
    return code == 0 ? "Not specified" : code == 1 ? "U1" :
           code == 3 ? "U3" : "Unknown code";
}

const char *sd_stage_name(SdStage stage)
{
    static const char *const names[] = {
        "Reset card", "Check interface", "Wait for ready", "Read CID",
        "Get card address", "Read CSD", "Select card", "Set 4-bit bus",
        "Set block length", "Decode registers", "Complete",
        "Read SCR", "Read SD Status"
    };
    return names[stage];
}

const char *sd_error_name(SdError error)
{
    static const char *const names[] = {
        "No error", "Card response timed out", "Command line stayed busy",
        "Invalid response framing", "Card transfer checksum failed",
        "Card rejected command/state", "Voltage/echo mismatch",
        "Unsupported CSD version", "Invalid register field",
        "CSD/OCR addressing mismatch"
    };
    return names[error];
}
