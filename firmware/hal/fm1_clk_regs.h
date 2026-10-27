/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff) The clock tree's registers as they are, for reports: SYS_DIV, CLK_CON0, CLK_CON2, PLL_CON, PLL2_CON0,
 * PLL2_CON1, SFC_BAUD, USB_CLK (fm1_clk320.h has their fields). The SMK update loader answers them (ota.c cmd 0x7C),
 * so a loader the boot stage started from a power-on can be set against one the app started.
 *
 * And the flash controller's divider as the app has it. A loader the boot stage starts from a power-on (the flash
 * record) finds SFC_BAUD at the boot ROM's 0xB9 (HSB / 186, about 1 MHz) where one the app started finds 1 (HSB / 2):
 * seen on the SMK-37 Pro, whose loader then lost its watchdog about 20 s into the write, every time, and only then.
 * The update loader sets it back to 1 at its start, with the clock tree the app runs on (SYS on PLL2's VCO x 2/3,
 * HSB = SYS / 2) and the controller idle; the flash runs at HSB / 2 under the app as well. */
#pragma once
#include <stdint.h>

#ifndef FM1_CLK_HOST                             /* (tests/ldr_test.c: fm1_sfc_baud_pick alone) */
static void fm1_clk_regs(uint32_t *o)            /* o: 8 words */
{
    static const uint32_t A[8] = {0x10008u, 0x1000Cu, 0x10014u, 0x119A0u, 0x119A8u, 0x119ACu, 0x40204u, 0x16A08u};
    uint32_t i;
    for (i = 0; i < 8u; i++)
        o[i] = *(const volatile uint32_t *)A[i];
}

#endif

/* the divider to set: 1 for a slower one under the app's clock tree, else 0 (keep it). No registers: tested on the host */
static uint32_t fm1_sfc_baud_pick(uint32_t baud, uint32_t clk_con2, uint32_t sys_div)
{
    return baud > 1u && (clk_con2 & 15u) == 6u && ((sys_div >> 16) & 3u) == 1u ? 1u : 0u;
}

#ifndef FM1_CLK_HOST
static uint32_t fm1_sfc_baud_app(void)           /* -> SFC_BAUD as found */
{
    volatile uint32_t *baud = (volatile uint32_t *)0x40204u, *sfc = (volatile uint32_t *)0x40200u;
    uint32_t was = *baud, to = fm1_sfc_baud_pick(was, *(volatile uint32_t *)0x10014u, *(volatile uint32_t *)0x10008u);
    if (to) {
        while ((int32_t)*sfc < 0)                 /* (the controller idle: bit 31 busy) */
            ;
        *baud = to;
        __asm__ volatile("csync" ::: "memory");
    }
    return was;
}
#endif
