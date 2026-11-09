/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C07F00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver; then, from _rt_hot, Skiff's .ram_hot: audio code kept out of the XIP cache, which may
 * call XIP code, since the audio ISR never runs with the flash busy (felucca.c AUDIO_RAM); both inside the PC
 * guard's RAM window, fm1_guard.h); (Skiff 1.3) then .c1_text, the second core's waiting loop (hal/fm1_cpu1.h: an
 * idle core 1 never fetches from the flash; the PC window covers it, it calls only its job); .ramx follows it there (Skiff: zero-initialised data,
 * the take log and the undo copy: the region's rest was unused). (2.5) RAMTEXT ends at 0x01C07F00, where crt0's STATUS
 * words are (it took the 0x01C06000.. gap kept for RAM-run debug, unused).
 * RAM  : .data/.bss (92 KiB; (2.5) 4 KiB of its spare went to the POOL)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands (Skiff: the main loop's 11.75 KiB, about 3x the deepest use measured in
 * the emulator, 4.1 KiB; its other 12 KiB went to the POOL; the interrupts' 7.75 KiB as they were: they carry USB
 * and updates too); NOINIT survives resets and UBOOT entry.
 * 0x01C7FD20..0x01C7FD4F: the SMK update loader's start record (loader/ldr_crumb.h), kept out of NOINIT.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 92K
    POOL   (rw)  : ORIGIN = 0x01C1F000, LENGTH = 0x58000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D20
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_hot = .; KEEP(*(.ram_hot*)) . = ALIGN(4);
                  KEEP(*(.c1_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .ramx (NOLOAD) : { . = ALIGN(4); _ramx_start = .; *(.ramx*) . = ALIGN(4); _ramx_end = .; } > RAMTEXT
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C77000; _ustack_lo = 0x01C77100; _ustack_top = 0x01C7A000;   /* (Skiff: 11.75 KiB, was 23.75) */
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
