/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/usb_trace.h"

int main(void)
{
    usb_trace_t t = {0};
    uint8_t setup[8] = {0xa2, 0x84, 0, 1, 0x84, 0, 3, 0};
    usb_trace_stall(&t);
    assert(t.count == 0 && t.total == 0);
    usb_trace_record(&t, setup);
    usb_trace_stall(&t);
    assert(t.entries[0].stalled == 1);
    assert(!memcmp(t.entries[0].setup, setup, 8));
    /* Fill and wrap: preserve the last 32 in order, with per-entry stalls. */
    for (unsigned i = 1; i < 70; ++i) {
        setup[0] = (uint8_t)i;
        usb_trace_record(&t, setup);
        if (i & 1u) usb_trace_stall(&t);
    }
    assert(t.count == 32 && t.total == 70);
    for (unsigned i = 0; i < 32; ++i) {
        const usb_trace_entry_t *e = &t.entries[(t.next + USB_TRACE_N - t.count + i) % USB_TRACE_N];
        unsigned value = 38 + i;
        assert(e->stalled == (value & 1u));
        assert(e->setup[0] == value);
    }
    t.total = UINT32_MAX;
    usb_trace_record(&t, setup);
    assert(t.total == 0 && t.count == 32);
    puts("USB trace: empty, wrap, stalls and counter rollover passed");
    return 0;
}
