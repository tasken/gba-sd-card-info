#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Remove only named build outputs, not arbitrary directories."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("main", "platform", "registers", "names", "probe", "data", "storage",
         "report_format", "report_save", "ui", "font", "runtime",
         "startup", "supercard_io", "ff", "sfw_driver", "sfw_io", "sfw_crc",
         "sfw_backend")
for name in NAMES:
    for suffix in (".o", ".d"):
        (ROOT / "build" / (name + suffix)).unlink(missing_ok=True)
for name in ("gba-sd-card-info.elf", "gba-sd-card-info.gba", "gba-sd-card-info.map",
             "superchis-sd-info.elf", "superchis-sd-info.gba", "superchis-sd-info.map",
             "build_commit.flag", "build_info.h",
             "test_sd", "test_probe", "test_report", "test_data", "test_storage"):
    (ROOT / "build" / name).unlink(missing_ok=True)
