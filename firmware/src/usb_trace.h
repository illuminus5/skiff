/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional EP0 diagnostics. One serialized USB poll writer; snapshot only with
 * interrupts disabled. Kept across USB bus resets, lost on power/restart.
 * Included after the firmware/host memcpy implementation. */
#pragma once
#include <stdint.h>
#define USB_TRACE_N 32u
typedef struct { uint8_t setup[8], stalled; } usb_trace_entry_t;
typedef struct {
    usb_trace_entry_t entries[USB_TRACE_N];
    uint32_t total;
    uint8_t next, count;
} usb_trace_t;

static void usb_trace_record(usb_trace_t *t, const uint8_t setup[8])
{
    usb_trace_entry_t *e = &t->entries[t->next];
    memcpy(e->setup, setup, 8u);
    e->stalled = 0;
    t->next = (uint8_t)((t->next + 1u) % USB_TRACE_N);
    if (t->count < USB_TRACE_N) ++t->count;
    ++t->total;
}

static void usb_trace_stall(usb_trace_t *t)
{
    if (t->count)
        t->entries[(t->next + USB_TRACE_N - 1u) % USB_TRACE_N].stalled = 1;
}
