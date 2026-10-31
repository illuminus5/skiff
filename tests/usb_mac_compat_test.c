/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HALF_FRAMES 128
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#ifndef T_CDC
#define T_CDC 1
#endif
#define FELUCCA_CDC T_CDC
#define FELUCCA_UAC 1
#define FELUCCA_UAC_MAC_COMPAT 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
static void inspect(const uint8_t *c, unsigned n, unsigned base)
{
    unsigned off=0, cur=255, cls=0, sub=0, ac=0, as=0, midi=0, fmt=0, ep=0;
    assert((unsigned)(c[2] | c[3]<<8) == n);
    while(off<n) {
        const uint8_t *d=c+off;
        assert(d[0]>=2 && off+d[0]<=n);
        if(d[1]==4) {
            cur=d[2]; cls=d[5]; sub=d[6];
            if(d[5]==1 && sub==1) { assert(cur==base); ac++; }
            if(d[5]==1 && sub==2) { assert(cur==base+1); as++; }
            if(d[5]==1 && sub==3) { assert(cur==base+2); midi++; }
        }
        if(d[1]==0x24 && cls==1 && sub==1 && d[2]==1) {
            assert(d[0]==9 && d[5]==30 && d[6]==0 && d[7]==1 && d[8]==base+1);
        }
        if(d[1]==0x24 && cls==1 && sub==2 && d[2]==2) {
            assert(d[0]==11 && d[4]==UAC_CHANNELS && d[5]==2 && d[6]==16 && d[7]==1);
            assert((d[8] | d[9]<<8 | d[10]<<16)==UAC_RATE); fmt++;
        }
        if(d[1]==5 && d[2]==0x84) { assert(cur==base+1 && d[3]==5 && (d[4]|d[5]<<8)==UAC_MAXP); ep++; }
        off+=d[0];
    }
    assert(ac==1 && as==2 && midi==1 && fmt==1 && ep==1);
}
int main(void)
{
    assert(!FELUCCA_UAC_48K);
    inspect(CFG_DESC_PLAIN,sizeof CFG_DESC_PLAIN,0);
#if FELUCCA_CDC
    inspect(CFG_DESC,sizeof CFG_DESC,AUD_IF_CDC);
    usb_cdc_on=1; assert(UAC_AS_IF==AUD_IF_CDC+1);
    usb_cdc_on=0;
#endif
    assert(UAC_AS_IF==1);
    assert(DEV_DESC_PLAIN[12] != 0x28); /* don't reuse the old descriptor identity */
    printf("USB profile: %u channels, %u Hz, %u-byte max packet, AC/AS/MIDI and AS-only collection passed\n", UAC_CHANNELS, UAC_RATE, UAC_MAXP);
    return 0;
}
