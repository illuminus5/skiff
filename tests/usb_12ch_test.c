/* SPDX-License-Identifier: GPL-3.0-only */
#define main profile_descriptor_main
#define FELUCCA_USB_12CH 1
#include "usb_mac_compat_test.c"
#undef main
#include <math.h>
#include <stdlib.h>
#define CTL 32u
#define NPART 6u
#include "../firmware/src/usb_routing.c"
int main(void)
{
    profile_descriptor_main();
    assert(UAC_RATE==22050 && UAC_MAXP==576 && UAC_WORDS==6);
    usb_route_begin(CTL);
    for(unsigned t=0;t<6;t++)
        for(unsigned i=0;i<CTL;i++) usb_route_track(usb_route_modes[t],i,1000+100*t,-1000-100*t);
    uac.feed=1;
    for(unsigned b=0;b<20;b++) uac_tap12(&usb_12_capture[0][0],CTL,CTL);
    assert(ua_w==256); /* whole-block overflow refused */
    for(unsigned p=0;p<6;p++) {
        uint32_t word=ua_ring[200*6+p];
        assert(abs((int16_t)word-(1000+100*(int)p))<=1);
        assert(abs((int16_t)(word>>16)-(-1000-100*(int)p))<=1);
    }
    uac.go=1; uac.fill_min=52;
    uint32_t packet[24*6+1];packet[144]=0xabcdef01;
    unsigned total=0;
    for(unsigned i=0;i<1000;i++) {
        unsigned n=uac_packet(packet);assert(n>=21 && n<=24);total+=n;
        assert(packet[144]==0xabcdef01);
    }
    assert(total==22050);
    /* An underrun repeats the entire last twelve-channel frame. */
    for(unsigned p=0;p<6;p++) assert(packet[p]==packet[6+p]);
    usb_route_begin(CTL);
    for(unsigned i=0;i<6*2*CTL;i++) assert((&usb_12_capture[0][0])[i]==0);
    /* Independent frequency response: 1 kHz passes, >= Nyquist is rejected. */
    for(unsigned hz=1000;hz<=21000;hz+=100) {
        double re=0,im=0;
        for(unsigned k=0;k<63;k++) { double a=2*3.141592653589793*hz*k/44100.;re+=usb12_fir[k]*cos(a)/32768.;im+=usb12_fir[k]*sin(a)/32768.; }
        double gain=sqrt(re*re+im*im);
        if(hz==1000) assert(gain>.99 && gain<1.01);
        if(hz>=11100) assert(gain<.002);
    }
    puts("12-channel: six ordered stereo taps, filtered decimation, overflow/underrun, exact packet cadence and guards passed");
    return 0;
}
