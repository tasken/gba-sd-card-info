# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import check_rom


def fixture():
    boot = bytearray(256)
    struct.pack_into("<I", boot, 0, 0xEA00002E)
    boot[160:172] = b"GBASDCARDINF"
    boot[172:176] = b"SCIE"
    boot[176:178] = b"00"
    boot[178] = 0x96
    boot[189] = (-sum(boot[160:189]) - 0x19) & 255
    ram = bytes(range(32))
    names = b"\0.boot\0.ram\0.bss\0.shstrtab\0"
    offsets = (0, 1, 7, 12, 17)
    elf = bytearray(512)
    struct.pack_into("<16sHHIIIIIHHHHHH", elf, 0,
                     b"\x7fELF\x01\x01\x01" + bytes(9), 2, 40, 1,
                     0x08000000, 0, 52, 0, 52, 0, 0, 40, 5, 4)
    # The test logo is synthetic; the production fingerprint is never changed.
    for i, args in enumerate([
        (offsets[0], 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (offsets[1], 1, 6, 0x08000000, 256, 256, 0, 0, 4, 0),
        (offsets[2], 1, 7, 0x02000000, 512, 32, 0, 0, 4, 0),
        (offsets[3], 8, 3, 0x02000020, 544, 16, 0, 0, 4, 0),
        (offsets[4], 3, 0, 0, 544, len(names), 0, 0, 1, 0),
    ]):
        struct.pack_into("<10I", elf, 52 + 40 * i, *args)
    elf[256:] = boot + ram + names
    return bytearray(boot + ram), elf


class RomChecks(unittest.TestCase):
    def test_synthetic_header(self):
        rom, _ = fixture()
        with patch.object(check_rom, "LOGO_SHA256", hashlib.sha256(bytes(156)).hexdigest()):
            check_rom.check_header(rom)
            rom[160] ^= 1
            with self.assertRaises(ValueError):
                check_rom.check_header(rom)

    def test_missing_real_logo(self):
        rom, _ = fixture()
        with self.assertRaisesRegex(ValueError, "logo"):
            check_rom.check_header(rom)

    def test_bad_checksum(self):
        rom, _ = fixture()
        rom[189] ^= 1
        with patch.object(check_rom, "LOGO_SHA256", hashlib.sha256(bytes(156)).hexdigest()):
            with self.assertRaisesRegex(ValueError, "checksum"):
                check_rom.check_header(rom)

    def test_bad_entry(self):
        rom, _ = fixture()
        rom[:4] = bytes(4)
        with self.assertRaisesRegex(ValueError, "branch"):
            check_rom.check_header(rom)

    def test_ram_payload(self):
        rom, elf = fixture()
        check_rom.check_elf(elf, rom)
        rom[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "payload"):
            check_rom.check_elf(elf, rom)

    def test_rom_resident_app(self):
        rom, elf = fixture()
        struct.pack_into("<I", elf, 52 + 40 * 2 + 12, 0x08000100)
        with self.assertRaisesRegex(ValueError, "EWRAM"):
            check_rom.check_elf(elf, rom)

    def test_bootstrap_payload(self):
        rom, elf = fixture()
        rom[192] ^= 1
        with self.assertRaisesRegex(ValueError, "bootstrap"):
            check_rom.check_elf(elf, rom)

    def test_entry_into_ram_payload(self):
        rom, elf = fixture()
        struct.pack_into("<I", rom, 0, 0xEA00003E)
        struct.pack_into("<I", elf, 256, 0xEA00003E)
        with self.assertRaisesRegex(ValueError, "branch into"):
            check_rom.check_elf(elf, rom)

    def test_unknown_allocated_section(self):
        rom, elf = fixture()
        struct.pack_into("<I", elf, 52 + 40 * 4 + 8, 2)
        with self.assertRaisesRegex(ValueError, "allocated section"):
            check_rom.check_elf(elf, rom)

    def test_bad_elf(self):
        with self.assertRaises(ValueError):
            check_rom.check_elf(b"", b"")

    def test_iwram_driver(self):
        rom, elf = fixture()
        sections = [struct.unpack_from("<10I", elf, 52 + 40 * i)
                    for i in range(5)]
        names = b"\0.boot\0.ram\0.bss\0.shstrtab\0.iwram\0"
        names_offset = len(elf)
        elf.extend(names)
        names_section = list(sections[4])
        names_section[4:6] = [names_offset, len(names)]
        sections[4] = tuple(names_section)
        payload = b"\x01\x02\x03\x04" * 4
        payload_offset = len(elf)
        elf.extend(payload)
        sections.append((names.index(b".iwram"), 1, 6, 0x03000000,
                         payload_offset, len(payload), 0, 0, 4, 0))
        shoff = len(elf)
        for section in sections:
            elf.extend(struct.pack("<10I", *section))
        struct.pack_into("<I", elf, 32, shoff)
        struct.pack_into("<H", elf, 48, 6)
        rom.extend(payload)
        check_rom.check_elf(elf, rom)
        rom[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "IWRAM payload"):
            check_rom.check_elf(elf, rom)
        rom[-1] ^= 1
        struct.pack_into("<I", elf, shoff + 40 * 5 + 12, 0x03006000)
        with self.assertRaisesRegex(ValueError, "reserved IWRAM"):
            check_rom.check_elf(elf, rom)


if __name__ == "__main__":
    unittest.main()
