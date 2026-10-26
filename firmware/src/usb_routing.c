/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_routes.h"
#if FELUCCA_USB_6CH
#if FELUCCA_USB_6CH
static uint32_t usb_six_block,usb_extra_block;
static int32_t usb_fx_return[3][CTL];
#if FELUCCA_TAPE
static int32_t usb_tape_return[2u*CTL];
static uint32_t usb_tape_write;
static void usb_tape_capture(int32_t l,int32_t r)
{
    if(usb_tape_write<CTL){usb_tape_return[2u*usb_tape_write]=l;
        usb_tape_return[2u*usb_tape_write+1u]=r;++usb_tape_write;}
}
/* The codec publishes samples in order even when a step splits the block. */
#define TAPE_USB_CAPTURE(l,r) usb_tape_capture((l),(r))
#endif
#endif
static uint8_t usb_route_modes[NPART];
static int32_t usb_track_capture[NPART][2u * CTL] __attribute__((aligned(128)));
static void usb_route_begin(uint32_t n)
{
#if FELUCCA_USB_6CH
#if FELUCCA_TAPE
    usb_tape_write=0;
    for(uint32_t i=0;i<2u*n;i++)usb_tape_return[i]=0;
#endif
    usb_six_block = usb_six_routes;
    usb_extra_block=usb_extra_routes;
    for(uint32_t f=0;f<3;f++)for(uint32_t i=0;i<n;i++)usb_fx_return[f][i]=0;
#endif
    for (uint32_t k=0; k<NPART; k++) {
        usb_route_modes[k]=(uint8_t)(k+1u);
        for (uint32_t i=0; i<2u*n; i++) usb_track_capture[k][i]=0;
    }
}
static __attribute__((noinline)) void usb_route_track(uint32_t track, uint32_t i, int32_t l, int32_t r)
{
    usb_track_capture[track-1u][2u*i]=l;
    usb_track_capture[track-1u][2u*i+1u]=r;
}
#if FELUCCA_USB_6CH
static int32_t usb_six_output[UAC_CHANNELS * CTL];
static __attribute__((minsize,noinline)) const int32_t *usb_route_finish(const int32_t *master, uint32_t n)
{
    for (uint32_t slot=0;slot<UAC_CHANNELS;slot++) {
        uint32_t source=usb_map_get(usb_six_block,usb_extra_block,slot);
        for (uint32_t i=0;i<n;i++) {
            int32_t l=0,r=0;
            uint32_t mode=source;
            if (source>=4u && source<22u) {
                uint32_t track=(source-4u)/3u;
                mode=(source-4u)%3u+1u;
                l=usb_track_capture[track][2u*i];r=usb_track_capture[track][2u*i+1u];
            }
#if FELUCCA_TAPE
            else if(source>=USB_TAPE_L && source<=USB_TAPE_MONO){
                l=usb_tape_return[2u*i];r=usb_tape_return[2u*i+1u];mode=source-USB_TAPE_L+1u;
            }
#endif
            else if (source>=1u && source<=3u) { l=master[2u*i];r=master[2u*i+1u]; }
            int32_t value=mode==1u?l:mode==2u?r:mode==3u?usb_route_mono(l,r):0;
            if(source>=22u && source<=24u)value=usb_fx_return[source-22u][i]; /* REV DELAY CHOR */
            usb_six_output[UAC_CHANNELS*i+slot]=value;
        }
    }
    return usb_six_output;
}
#endif
#elif FELUCCA_USB_ROUTING
static uint32_t usb_route_block;
static uint8_t usb_route_modes[NPART];
/* Each slot is one writer's cache-aligned track block, even across CPU2.
 * Main core clears before starting the worker; consumes only after C1_WAIT. */
static int32_t usb_route_capture[2][CTL] __attribute__((aligned(128)));
static int32_t usb_route_output[2u * CTL];

static __attribute__((minsize,noinline)) void usb_route_begin(uint32_t n)
{
    uint32_t k;
    usb_route_block = usb_routes;
    for (k = 0; k < NPART; ++k) usb_route_modes[k] = 0;
    if (usb_route_block == USB_ROUTES_DEFAULT) return;
    for (k = 0; k < 2u; ++k) {
        uint32_t s = usb_route_block >> (8u * k) & 255u, i;
        for (i = 0; i < n; ++i) usb_route_capture[k][i] = 0;
        if (s >= 4u && s < USB_SOURCE_N)
            usb_route_modes[(s - 4u) / 3u] |= (uint8_t)(((s - 4u) % 3u + 1u) << (2u * k));
    }
}

static __attribute__((minsize,noinline)) void usb_route_track(uint32_t mask, uint32_t i, int32_t l, int32_t r)
{
    uint32_t k;
    for (k = 0; k < 2u; ++k, mask >>= 2) {
        uint32_t mode = mask & 3u;
        if (mode) usb_route_capture[k][i] = mode == 1u ? l : mode == 2u ? r : usb_route_mono(l, r);
    }
}

static __attribute__((minsize,noinline)) const int32_t *usb_route_finish(const int32_t *master, uint32_t n)
{
    uint32_t k, i;
    if (usb_route_block == USB_ROUTES_DEFAULT) return master;
    for (k = 0; k < 2u; ++k) {
        uint32_t s = usb_route_block >> (8u * k) & 255u;
        for (i = 0; i < n; ++i)
            usb_route_output[2u * i + k] = s == 1u ? master[2u * i] : s == 2u ? master[2u * i + 1u] :
                s == 3u ? usb_route_mono(master[2u * i], master[2u * i + 1u]) : usb_route_capture[k][i];
    }
    return usb_route_output;
}
#endif
