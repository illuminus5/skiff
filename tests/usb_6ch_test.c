/* SPDX-License-Identifier: GPL-3.0-only */
#define main descriptor_main
#define FELUCCA_USB_6CH 1
#include "usb_mac_compat_test.c"
#undef main
#define CTL 32u
#define NPART 6u
#include "../firmware/src/usb_routing.c"
int main(void)
{
    descriptor_main();
    assert(UAC_RATE==44100 && UAC_CHANNELS==6 && UAC_MAXP==552);
    int32_t master[2u*CTL];
    for(unsigned i=0;i<CTL;i++){master[2*i]=100;master[2*i+1]=300;}
    usb_route_begin(CTL);
    for(unsigned t=0;t<6;t++)
        for(unsigned i=0;i<CTL;i++) usb_route_track(usb_route_modes[t],i,1000+100*t,2000+100*t);
    const int32_t *o=usb_route_finish(master,CTL);
    for(unsigned i=0;i<CTL;i++)for(unsigned t=0;t<6;t++) assert(o[6*i+t]==1500+100*t);
    uac.feed=1;uac_tap(o,CTL);assert(ua_w==CTL);
    for(unsigned t=0;t<6;t++) {
        uint32_t v=ua_ring[t/2];
        assert((int16_t)(v>>(16*(t&1)))==1500+100*t);
    }
    /* Six independent slots: OFF, master mono, T6 R, T1 L, T1 L, master R. */
    unsigned sources[6]={0,3,20,4,4,2};
    for(unsigned t=0;t<6;t++)usb_six_put(t,sources[t]);
    usb_route_begin(CTL);
    usb_six_put(0,1); /* next block only */
    usb_route_track(1,0,-1000,2000);usb_route_track(6,0,4000,-6000);
    o=usb_route_finish(master,CTL);
    assert(o[0]==0 && o[1]==200 && o[2]==-6000 && o[3]==-1000 && o[4]==-1000 && o[5]==300);
    assert(o[6+2]==0 && o[6+3]==0); /* silent tracks don't leak previous blocks */
    for(unsigned f=0;f<3;f++)for(unsigned i=0;i<CTL;i++)usb_fx_return[f][i]=100*(f+1);
    unsigned returns[6]={22,23,24,22,23,24};
    for(unsigned k=0;k<6;k++)usb_six_put(k,returns[k]);
    usb_six_block=usb_six_routes;o=usb_route_finish(master,CTL);
    for(unsigned k=0;k<6;k++)assert(o[k]==100*(1+k%3));
    for(unsigned k=0;k<6;k++)usb_six_routes=(usb_six_routes&~(31u<<(5*k)))|25u<<(5*k);
    assert(usb_six_valid(usb_six_routes)==0); /* removed click/CMP routes become OFF */
    usb_six_put(0,1);
    usb_six_put(6,1);usb_six_put(0,99);assert(usb_six_get(0)==1);
    assert(usb_six_valid(0xffffffffu)==0);
    uint32_t packet[46*3+1];packet[138]=0xabcd1234;
    uac.go=0;unsigned frames=0;
    for(unsigned t=0;t<1000;t++){unsigned n=uac_packet(packet);assert(n>=44&&n<=45);frames+=n;assert(packet[138]==0xabcd1234);}
    assert(frames==44100);
    usb.config=1;uac.alt=1;uac.flowing=1;uac.go=0;
    uac_render_start();assert(uac.feed && uac.go);
    uint32_t before=ua_w;
    int32_t encoded[6*CTL];
    for(unsigned i=0;i<CTL;i++)for(unsigned ch=0;ch<6;ch++)encoded[6*i+ch]=1000*ch+i;
    encoded[0]=INT32_MAX;encoded[1]=INT32_MIN;
    uac_tap(encoded,CTL);assert(ua_w==before+CTL);
    for(unsigned i=0;i<CTL;i++)for(unsigned ch=0;ch<6;ch++) {
        int32_t expected=encoded[6*i+ch];expected=expected>32767?32767:expected < -32768?-32768:expected;
        uint32_t word=ua_ring[((before+i)&(UA_N-1))*3+ch/2];
        assert((int16_t)(word>>(16*(ch&1)))==expected);
    }
    uac.go=0;uac_render_start(); /* newly primed region must be complete silence */
    for(unsigned i=0;i<UA_PRIME;i++)for(unsigned p=0;p<3;p++)assert(ua_ring[((ua_r+i)&(UA_N-1))*3+p]==0);
    puts("six-channel: default tracks, custom independent sources, duplicates, OFF, snapshot, packed order and packet cadence passed");
    return 0;
}
