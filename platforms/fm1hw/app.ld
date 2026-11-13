/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's FM-1 memory map: examples/hwtest/app.ld of felucca-bsp with the RAM widened to hold the UI canvas.
 * Memory map of a standalone FM-1 app: code and constants run in place from XIP
 * (app.bin offset 0 = 0x02000120); .data is copied to RAM by fm1_cstart, and so is .ram_text (code that runs
 * while the flash is busy: the flash driver) into RAMTEXT, the guard's RAM code window.
 * Stacks sit above the RAM with 256-byte guard bands; NOINIT survives resets.
 * 0x01C7FD50.. is reserved (boot info, mailbox, vector table). */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x6000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x6C000   /* 432 KiB, up to the user stack's guard band (0x01C74000) */
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*))
                  . = ALIGN(4); _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .;   /* the second core's loop */
                  _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    /* the outside plugins' .data and .bss (platforms/fm1hw/fm1_plugins.c): first in RAM, at an address that never moves, for the
     * images in flash are linked for it (an image whose RAM is not inside it is refused) */
    .plug_ram (NOLOAD) : { _plug_ram = .; KEEP(*(.plug_ram*)) . = ALIGN(8); _plug_ram_end = .; } > RAM
    ASSERT(_plug_ram == 0x01C08000, "app.ld: .plug_ram must start the RAM (fm1_plugins.c PL_RAM_AT)")
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
