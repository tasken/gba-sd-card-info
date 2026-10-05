#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the GBA header and the ELF's RAM-only application placement."""
import hashlib
from pathlib import Path
import struct
import sys

LOGO_SHA256 = "08a0153cfd6b0ea54b938f7d209933fa849da0d56f5a34c481060c9ff2fad818"


def check_header(rom: bytes) -> None:
    if not 192 < len(rom) <= 32 * 1024 * 1024:
        raise ValueError("Invalid GBA ROM size")
    opcode = int.from_bytes(rom[:4], "little")
    if opcode & 0xFF000000 != 0xEA000000:
        raise ValueError("ROM entry is not an unconditional ARM branch")
    offset = opcode & 0xFFFFFF
    if offset & 0x800000:
        offset -= 1 << 24
    target = 8 + offset * 4
    if not 192 <= target < len(rom):
        raise ValueError("ROM entry branches outside the bootstrap")
    if hashlib.sha256(rom[4:160]).hexdigest() != LOGO_SHA256:
        raise ValueError("Standard GBA boot logo is missing or modified")
    if rom[160:172] != b"GBASDCARDINF" or rom[172:176] != b"SCIE":
        raise ValueError("Unexpected application title/game code")
    if rom[176:178] != b"00" or rom[178] != 0x96:
        raise ValueError("Invalid maker code/fixed byte")
    if any(rom[179:189]) or any(rom[190:192]):
        raise ValueError("Unexpected unit/device/reserved/version bytes")
    if (sum(rom[160:190]) + 0x19) & 0xFF:
        raise ValueError("GBA header checksum failed")


def check_elf(data: bytes, rom: bytes) -> None:
    if data[:7] != b"\x7fELF\x01\x01\x01" or len(data) < 52:
        raise ValueError("Expected a little-endian ELF32 executable")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    _, kind, machine, _, entry, _, shoff, _, _, _, _, shsize, count, names_idx = header
    if kind != 2 or machine != 40 or entry != 0x08000000 or shsize != 40:
        raise ValueError("Unexpected ELF architecture or entry")
    if not 0 < names_idx < count or shoff + count * shsize > len(data):
        raise ValueError("Invalid ELF section table")
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(count)]
    names_section = sections[names_idx]
    names = data[names_section[4]:names_section[4] + names_section[5]]
    found = {}
    for section in sections:
        name_offset, stype, flags, address, offset, size, *_ = section
        name = names[name_offset:].split(b"\0", 1)[0].decode("ascii")
        if not flags & 2:
            continue
        if name not in {".boot", ".ram", ".bss", ".iwram"}:
            raise ValueError(f"Unexpected allocated section: {name}")
        if name == ".boot":
            if address != 0x08000000 or not 192 < size < len(rom):
                raise ValueError("Invalid ROM bootstrap placement")
        elif name == ".iwram":
            if not 0x03000000 <= address <= address + size <= 0x03006000:
                raise ValueError("Driver is outside reserved IWRAM")
        elif not 0x02000000 <= address <= address + size <= 0x02040000:
            raise ValueError(f"{name} is not wholly inside EWRAM")
        if stype != 8 and offset + size > len(data):
            raise ValueError(f"Truncated section: {name}")
        found[name] = section
    if ".boot" not in found or ".ram" not in found:
        raise ValueError("Missing bootstrap or RAM application")
    ram = found[".ram"]
    boot = found[".boot"]
    if boot[1] != 1 or ram[1] != 1 or not boot[2] & 4 or not ram[2] & 4:
        raise ValueError("Bootstrap/RAM image must contain executable code")
    if rom[:4] != data[boot[4]:boot[4] + 4] or (
        rom[192:boot[5]] != data[boot[4] + 192:boot[4] + boot[5]]
    ):
        raise ValueError("ROM bootstrap differs from the ELF")
    opcode = int.from_bytes(rom[:4], "little")
    offset = opcode & 0xFFFFFF
    if offset & 0x800000:
        offset -= 1 << 24
    if not 192 <= 8 + offset * 4 < boot[5]:
        raise ValueError("ROM entry does not branch into the bootstrap")
    if ram[3] != 0x02000000 or ram[5] == 0 or ram[5] % 4:
        raise ValueError("Invalid RAM image size/base")
    if rom[boot[5]:boot[5] + ram[5]] != data[ram[4]:ram[4] + ram[5]]:
        raise ValueError("ROM RAM payload differs from the ELF")
    if ".bss" in found and found[".bss"][3] < ram[3] + ram[5]:
        raise ValueError("BSS overlaps the RAM payload")
    if ".bss" in found and (found[".bss"][3] % 4 or found[".bss"][5] % 4):
        raise ValueError("BSS is not word-aligned")
    if ".iwram" in found:
        driver = found[".iwram"]
        if driver[1] != 1 or driver[3] % 4 or driver[5] % 4:
            raise ValueError("Invalid IWRAM driver image")
        start = boot[5] + ram[5]
        if rom[start:start + driver[5]] != data[driver[4]:driver[4] + driver[5]]:
            raise ValueError("ROM IWRAM payload differs from the ELF")


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: check_rom.py app.gba app.elf", file=sys.stderr)
        return 2
    try:
        rom = Path(sys.argv[1]).read_bytes()
        check_header(rom)
        check_elf(Path(sys.argv[2]).read_bytes(), rom)
    except (OSError, ValueError, struct.error, UnicodeError) as error:
        print(f"ROM check failed: {error}", file=sys.stderr)
        return 1
    print("GBA header and RAM payload placement checked; hardware test still required.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
