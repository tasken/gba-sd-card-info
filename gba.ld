/* SPDX-License-Identifier: GPL-3.0-or-later
 * GBATEK: gbamemorymap, gbainterruptcontrol, gbacartridgeheader.
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY {
    ROM (rx) : ORIGIN = 0x08000000, LENGTH = 32M
    EWRAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
    IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 0x6000
}
SECTIONS {
    .boot : {
        KEEP(*(.boot))
        . = ALIGN(4);
    } > ROM
    .ram : {
        __ram_start = .;
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
        *(.iplt)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.igot.plt)
        *(.ARM.extab* .ARM.exidx*)
        . = ALIGN(4);
        __ram_end = .;
    } > EWRAM AT > ROM
    __ram_load = LOADADDR(.ram);
    .iwram : {
        __iwram_start = .;
        *(.iwram.text .iwram_code)
        . = ALIGN(4);
        __iwram_end = .;
    } > IWRAM AT > ROM
    __iwram_load = LOADADDR(.iwram);
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > EWRAM
    .ARM.attributes 0 : { *(.ARM.attributes) }
    .rel.iplt 0 : { *(.rel.iplt) }
    /DISCARD/ : { *(.comment .note.* .debug* .gnu.attributes) }
    ASSERT(SIZEOF(.rel.iplt) == 0, "Runtime relocations are not supported")
    ASSERT(__bss_end <= 0x02040000, "Application exceeds EWRAM")
    ASSERT(__iwram_end <= 0x03006000, "Driver overlaps reserved stack space")
}
