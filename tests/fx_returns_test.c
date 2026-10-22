/* SPDX-License-Identifier: GPL-3.0-only */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static void reset_bus(void)
{
    memset(dly_buf,0,sizeof dly_buf);memset(cho_buf,0,sizeof cho_buf);
    memset(rev_comb,0,sizeof rev_comb);memset(&rev_u,0,sizeof rev_u);
    memset(&fx,0,sizeof fx);memset(&hl,0,sizeof hl);
}
int main(void)
{
    uint64_t h=1469598103934665603ull;
    /* Identical hashes in stereo and six-channel builds prove shared DSP unchanged. */
    for(unsigned model=0;model<3;model++)for(unsigned chorus=0;chorus<3;chorus++) {
        reset_bus();memset(song.g,0,sizeof song.g);
        song.g[G_BPM]=120;fx.rtype=(uint8_t)model;
        song.g[G_RTYPE]=model;song.g[G_CMODE]=chorus;song.g[G_CFDBK]=30;
        song.g[G_DMIX]=100;song.g[G_DFDBK]=64;song.g[G_DCOLOR]=64;
        song.g[G_RSIZE]=90;song.g[G_RDAMP]=50;song.g[G_CDEPTH]=60;song.g[G_CRATE]=30;
        for(unsigned b=0;b<700;b++) {
            for(unsigned i=0;i<CTL;i++)send_c[i]=send_d[i]=send_r[i]=(b==0 && i==0)?14000:0;
            fx_buses(send_c,send_d,send_r,wet,CTL);
            for(unsigned i=0;i<CTL;i++) {
#if FELUCCA_USB_6CH
                assert(usb_fx_return[0][i]+usb_fx_return[1][i]+usb_fx_return[2][i]==wet[i]);
#endif
                h^=(uint32_t)wet[i];h*=1099511628211ull;
                if(hl.side){h^=(uint32_t)rev_side[i];h*=1099511628211ull;}
            }
        }
    }

    assert(h==0xc5c723946bc887c4ull);
    printf("shared FX hash %016llx\n",(unsigned long long)h);
}
