/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#define CTL 32u
#define NPART 6u
#include "../firmware/src/usb_routing.c"
int main(void)
{
    int32_t master[2u * CTL] = {100, -40, INT32_MAX, INT32_MAX};
    const int32_t *o;
    assert(usb_routes_valid(0xffffu) == USB_ROUTES_DEFAULT);
    usb_route_begin(CTL);
    assert(usb_route_finish(master, CTL) == master);
    assert(usb_route_mono(INT32_MAX, INT32_MAX) == INT32_MAX);
    assert(usb_route_mono(INT32_MIN, INT32_MIN) == INT32_MIN);
    assert(usb_route_mono(INT32_MAX, INT32_MIN) == 0);
    usb_routes = 6u | 9u << 8;
    usb_route_begin(CTL);
    assert(usb_route_modes[0] == 3u && usb_route_modes[1] == 12u);
    usb_route_track(usb_route_modes[0], 0, 1000, -200);
    usb_route_track(usb_route_modes[1], 0, -600, 100);
    usb_routes = USB_ROUTES_DEFAULT;
    o = usb_route_finish(master, CTL);
    assert(o[0] == 400 && o[1] == -250 && o[2] == 0 && o[3] == 0);
    usb_routes = 6u | 6u << 8;
    usb_route_begin(CTL);
    usb_route_track(usb_route_modes[0], 0, 20, 40);
    o = usb_route_finish(master, CTL);
    assert(o[0] == 30 && o[1] == 30);
    usb_routes = 3u;
    usb_route_begin(CTL);
    o = usb_route_finish(master, CTL);
    assert(o[0] == 30 && o[1] == 0 && o[2] == INT32_MAX && o[3] == 0);
    usb_route_put(1, 21);
    assert(usb_route_get(0) == 3 && usb_route_get(1) == 21);
    usb_route_put(0, 255);
    assert(usb_route_get(0) == 3);
    usb_routes = 4u | 5u << 8;
    usb_route_begin(CTL);
    o = usb_route_finish(master, CTL);
    for (unsigned i = 0; i < 2u * CTL; ++i) assert(o[i] == 0);
    puts("USB routing: channel order, mono extremes, duplicates, silence and block snapshot passed");
}
