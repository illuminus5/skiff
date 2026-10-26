/* SPDX-License-Identifier: GPL-3.0-only */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
int main(void){
    memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();
    host_preset(&trk[0],ENGI_DRUM,0);trk[0].p[P_LPAN0]=-64;
    if(getenv("COMP_TEST")){trk[0].p[P_ITYPE]=IT_COMP;trk[0].p[P_IA]=64;trk[0].p[P_IB]=64;trk[0].p[P_IC]=24;trk[0].p[P_IMIX]=127;}
    usb_six_put(0,4);usb_six_put(1,5);
#if FELUCCA_TAPE
    tape_wanted=1;tape_send[0]=50;
#endif
    trk_note_on(&trk[0],36,100);int32_t out[2*CTL];int64_t left=0,right=0;
    for(unsigned b=0;b<40;b++){
        mix_block(out,CTL);assert(trk[0].lpan_on);
        const int32_t *u=usb_route_finish(out,CTL);
        for(unsigned i=0;i<CTL;i++){left+=llabs(u[i*UAC_CHANNELS]);right+=llabs(u[i*UAC_CHANNELS+1]);}
    }
    assert(left>1000&&right==0);
#if FELUCCA_TAPE
    trk[0].p[P_LEVEL]=0;trk_note_on(&trk[0],36,100);left=right=0;
    for(unsigned b=0;b<20;b++){
        mix_block(out,CTL);
        for(unsigned i=0;i<CTL;i++){left+=llabs(tape_input[0].l[i]+tape_input[1].l[i]);right+=llabs(tape_input[0].r[i]+tape_input[1].r[i]);}
    }
    assert(left>1000&&right==0);
#endif
    puts("V2 panned drum lane: isolated USB L/R and prefader TAPE send passed");return 0;
}
