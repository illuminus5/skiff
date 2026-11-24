/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of Skiff's update loader screen (firmware/loader/ldr_screen.c) through src/lcd.c, on a fake ST7789: the
 * window and RAMWR commands into a 240 x 240 frame. UPDATE MODE after boot, WRITING with the bar at 50 %, a second
 * write starting the bar over, UPDATE FAILED; every letter of their texts in the loader's font, nothing drawn outside
 * the panel. With a DIR, the three screens as PPM files. (#98) lcd.c's MENU > SCREEN OFF: dark, waking and on again
 * with PA2 low all along (the FM-1's backlight line is also the key matrix 595s' /OE).   tests/ldr_screen_test [DIR] */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint16_t fb[240][240];                   /* RGB565 as the panel stores it */
static uint32_t win[4], cx, cy, cmd, outside;
static const uint8_t *pend;
static uint32_t pend_n;
static uint8_t cmds[8];                         /* (#98) the commands since ncmd = 0 */
static uint32_t ncmd, pa2_high;                 /* (#98) PA2 driven high at any time: no key, button or knob reads */
static void fm1_lcd_hw_init(void) {}
static void fm1_lcd_baud(uint32_t b) { (void)b; }
static void fm1_lcd_backlight(uint32_t on) { pa2_high |= !on; }
static void fm1_lcd_deselect(void) {}
static void fm1_lcd_send_cmd(uint8_t c)
{
    if (ncmd < sizeof cmds)
        cmds[ncmd] = c;
    ncmd++;
    cmd = c;
    if (c == 0x2C) {
        cx = win[0];
        cy = win[2];
    }
}
static void fm1_lcd_send_data(const void *p, uint32_t n) { pend = p, pend_n = n; }
static void fm1_lcd_wait(void)                  /* the DMA's bytes land here, as the panel takes them */
{
    uint32_t i;
    if (!pend_n)
        return;                                 /* (a command's own byte) */
    if (cmd == 0x2A || cmd == 0x2B)
        for (i = 0; i < 2u; i++)
            win[(cmd == 0x2B) * 2u + i] = (uint32_t)pend[2u * i] << 8 | pend[2u * i + 1u];
    else if (cmd == 0x2C)
        for (i = 0; i + 1u < pend_n; i += 2u) {
            if (cx > 239u || cy > 239u || win[1] > 239u || win[3] > 239u)
                outside++;
            else
                fb[cy][cx] = (uint16_t)(pend[i] << 8 | pend[i + 1u]);
            if (++cx > win[1]) {
                cx = win[0];
                cy++;
            }
        }
    pend_n = 0;
}
#define fm1_irq_off() ((void)0)
#define fm1_irq_on() ((void)0)
#include "../firmware/src/lcd.c"
#include "../firmware/loader/ldr_screen.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("ldr_screen: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t count(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t c)   /* pixels of colour c */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += fb[y][x] == c;
    return n;
}
static void ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t x, y;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6 240 240 255\n");
    for (y = 0; y < 240u; y++)
        for (x = 0; x < 240u; x++) {
            uint16_t v = fb[y][x];
            fputc((v >> 11) * 255 / 31, f), fputc((v >> 5 & 63) * 255 / 63, f), fputc((v & 31) * 255 / 31, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    uint32_t total = 0x93000u - 0x4000u;          /* the app area, as ldr_core.c reports it */
    ldr_screen();
    ppm(dir, "ldr_update_mode");
    check("UPDATE MODE: the title in teal, three lines in white, nothing else lit",
          count(0, 70, 240, 91, LDR_TITLE) > 300u && count(0, 110, 240, 164, LDR_INK) > 900u &&
          count(0, 0, 240, 240, 0) + count(0, 0, 240, 240, LDR_TITLE) + count(0, 0, 240, 240, LDR_INK) == 240u * 240u);
    ldr_bar(total / 2u, total);
    ppm(dir, "ldr_writing");
    check("WRITING at 50 %: the bar's first 100 px teal, the rest its track",
          count(LDR_BAR_X, LDR_BAR_Y, LDR_BAR_X + 100u, LDR_BAR_Y + 8u, LDR_BAR) == 800u &&
          count(LDR_BAR_X + 100u, LDR_BAR_Y, LDR_BAR_X + 200u, LDR_BAR_Y + 8u, LDR_DIM) == 800u);
    ldr_bar(total, total);
    ldr_bar(total / 4u, total);
    check("a second write starts the bar over (25 %)",
          count(LDR_BAR_X, LDR_BAR_Y, LDR_BAR_X + 200u, LDR_BAR_Y + 8u, LDR_BAR) == 50u * 8u);
    ldr_failed();
    ppm(dir, "ldr_failed");
    check("UPDATE FAILED: the bar gone, the way out again",
          count(LDR_BAR_X, LDR_BAR_Y, LDR_BAR_X + 200u, LDR_BAR_Y + 8u, 0) == 1600u &&
          count(0, 110, 240, 164, LDR_INK) > 900u);
    check("every letter of the texts in the loader's font, nothing outside the panel", !ldr_missing && !outside);
    ncmd = pa2_high = 0;                          /* (#98) ui.c scr_frame / scr_shown: dark, SLPOUT, on */
    lcd_power(0);
    lcd_power(1);
    lcd_power(2);
    check("(#98) SCREEN OFF: DISPOFF + SLPIN, SLPOUT, DISPON; PA2 (595 /OE: the keys) never high",
          ncmd == 4u && cmds[0] == 0x28 && cmds[1] == 0x10 && cmds[2] == 0x11 && cmds[3] == 0x29 && !pa2_high);
    printf("%s\n", bad ? "LDR SCREEN TEST FAILED" : "ldr_screen test passed");
    return bad != 0;
}
