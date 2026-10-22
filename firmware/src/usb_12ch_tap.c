/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed six stereo track pairs; low-pass before decimation by two.
 * Audio ISR owns history. Only complete frames are published to TIMER5. */
#include "usb_12ch_fir.h"
static struct { int16_t h[12][64]; uint32_t w, phase; } usb12;
static __attribute__((noinline)) void uac_tap12(const int32_t *tracks, uint32_t stride, uint32_t n)
{
    uint32_t w=ua_w, produced=0;
    if (!uac.feed) return;
    if (UA_N-(w-ua_r) < (n+1u)/2u) { uac.overruns++; return; }
    for (uint32_t i=0; i<n; i++) {
        uint32_t pos=usb12.w++ & 63u;
        for (uint32_t ch=0; ch<12u; ch++) {
            int32_t x=tracks[(ch/2u)*2u*stride+2u*i+(ch&1u)];
            usb12.h[ch][pos]=(int16_t)(x>32767?32767:x < -32768?-32768:x);
        }
        usb12.phase^=1u;
        if (usb12.phase) continue;
        for (uint32_t pair=0; pair<6u; pair++) {
            uint32_t packed=0;
            for (uint32_t side=0; side<2u; side++) {
                int64_t sum=0;
                for (uint32_t t=0; t<63u; t++)
                    sum+=(int32_t)usb12.h[2u*pair+side][(pos-t)&63u]*usb12_fir[t];
                int32_t y=(int32_t)((sum+16384)>>15);
                y=y>32767?32767:y < -32768?-32768:y;
                packed|=(uint32_t)(uint16_t)(int16_t)y<<(16u*side);
            }
            ua_ring[((w+produced)&(UA_N-1u))*6u+pair]=packed;
        }
        produced++;
    }
    RING_PUBLISH();
    ua_w=w+produced;
}
