/* SPDX-License-Identifier: GPL-3.0-only */
#define FELUCCA_TAPE 1
#define FELUCCA_USB_6CH 1
#define FELUCCA_USB_CHANNELS 10
#define main hostsim_main
#include "hostsim.c"
#undef main
static uint64_t neutral_fixture(void){
#ifdef TAPE_FX_TICK
    tf_project_reset();tf_prepare(22050,1,0);
#endif
    uint8_t storage[TT_BLOCK*8]={0};tape_t t;tape_init(&t,storage,sizeof storage);t.enabled=1;
    for(unsigned i=0;i<1400;i++)tt_capture(&t,(int32_t)(i*139u%30001)-15000,(int32_t)(i*71u%21001)-10000);
    tape_trigger(&t,3,500,0);t.target_mix=23100;uint64_t h=1469598103934665603ull;
    for(unsigned block=0;block<80;block++){
        int32_t l[CTL],r[CTL];for(unsigned i=0;i<CTL;i++){l[i]=(int32_t)i*111-1600;r[i]=1200-(int32_t)i*47;}
        tape_process(&t,l,r,CTL,1);
        if(block==27)t.speed=-131072;if(block==49)tape_release(&t);
        for(unsigned i=0;i<CTL;i++){h^=(uint32_t)l[i];h*=1099511628211ull;h^=(uint32_t)r[i];h*=1099511628211ull;}
    }
    return h;
}
#ifndef TAPE_FX_FIXTURE_ONLY
int main(void){printf("%016llx\n",(unsigned long long)neutral_fixture());return 0;}
#endif
