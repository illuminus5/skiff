/* SPDX-License-Identifier: GPL-3.0-only */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
int main(void){
    memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();
    host_preset(&trk[0],ENGI_DRUM,0);trk[0].p[P_LPAN0]=-64;
    usb_six_put(0,4);usb_six_put(1,5);
    trk_note_on(&trk[0],36,100);int32_t out[2*CTL];int64_t left=0,right=0;
    for(unsigned b=0;b<40;b++){
        mix_block(out,CTL);assert(trk[0].lpan_on);
        const int32_t *u=usb_route_finish(out,CTL);
        for(unsigned i=0;i<CTL;i++){left+=llabs(u[i*UAC_CHANNELS]);right+=llabs(u[i*UAC_CHANNELS+1]);}
    }
    assert(left>1000&&right==0);
    puts("V2 panned drum lane: isolated USB L/R passed");return 0;
}
