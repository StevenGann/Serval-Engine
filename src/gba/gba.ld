/*
 * Linker script for Game Boy Advance cartridge ROMs.
 *
 * Code and read-only data stay in ROM. Initialized IWRAM/EWRAM contents are
 * stored in ROM and copied to RAM by crt0.s; .bss and .sbss are zeroed.
 *
 * Section conventions (see include/serval/platform.h):
 *   .iwram*  code or data placed in IWRAM (fast, 32-bit bus)
 *   .ewram*  initialized data placed in EWRAM
 *   .sbss*   zero-initialized data placed in EWRAM (libtonc's EWRAM_BSS)
 */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
    ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
    iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

/* The top of IWRAM holds the stacks and BIOS variables: the user/system
 * stack grows down from 0x03007F00. Reserve room for it. */
__sp_usr = 0x03007F00;
__sp_irq = 0x03007FA0;
__stack_reserve = 2K;

SECTIONS {
    .crt0 : {
        KEEP(*(.crt0))
    } > rom

    .text : {
        *(.text .text.*)
        *(.glue_7 .glue_7t)     /* ARM/Thumb interworking veneers */
    } > rom

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom

    .ARM.exidx : {
        *(.ARM.exidx*)
    } > rom

    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > rom

    .iwram : ALIGN(4) {
        __iwram_start = .;
        *(.iwram .iwram.*)
        *(.data .data.*)
        . = ALIGN(4);
        __iwram_end = .;
    } > iwram AT> rom
    __iwram_lma = LOADADDR(.iwram);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > iwram

    .ewram : ALIGN(4) {
        __ewram_start = .;
        *(.ewram .ewram.*)
        . = ALIGN(4);
        __ewram_end = .;
    } > ewram AT> rom
    __ewram_lma = LOADADDR(.ewram);

    .sbss (NOLOAD) : ALIGN(4) {
        __sbss_start = .;
        *(.sbss .sbss.*)
        . = ALIGN(4);
        __sbss_end = .;
    } > ewram

    __rom_end = __ewram_lma + SIZEOF(.ewram);
}

ASSERT(__bss_end <= __sp_usr - __stack_reserve,
       "IWRAM overflow: code and data collide with the stack reserve")
