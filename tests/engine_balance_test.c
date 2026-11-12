/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff experimental engine balance: actual host DSP, dry factory-preset sweep.
 * run_engine_balance_tests.sh compares disabled and enabled builds. Each render
 * forks from untouched DSP state, as regress.c does. CSV includes untrimmed
 * envelope fingerprints and pre-master route energies, not just final peaks. */
#include <stdint.h>
static uint32_t balance_slots[3u * 0x14000u / 4u];
#define SMP_USER_XIP(k) ((const uint8_t *)balance_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

static int unit(void)
{
    /* Exhaust every input in the documented mixer range for every LEVEL.
     * The oracle does not repeat the optimized split/rounding expression. */
    for (uint32_t level=0; level<128; level++) {
        int32_t g=LEVEL_Q12[level];
        for (int32_t x=-(1<<20); x<=(1<<20); x++) {
            int64_t p=(int64_t)x*g+2048;
            int32_t w=(int32_t)(p>=0 ? p/4096 : -((-p+4095)/4096));
            w=w<-524287 ? -524287 : w>524287 ? 524287 : w;
            if (lvl_mul(x,g)!=w) return 10;
        }
    }
    /* Master Q22 arithmetic, all knob values at signed boundary/residue cases. */
    static const int32_t edge[]={-8388608,-1048577,-1048576,-524289,-524288,
        -129,-128,-127,-5,-4,-3,-2,-1,0,1,2,3,4,5,127,128,129,
        524287,524288,1048575,1048576,8388607};
    for (int32_t mg=0; mg<=MASTER_FULL; mg++)
        for (uint32_t i=0; i<NELEM(edge); i++) {
            int64_t p=(int64_t)edge[i]*mg;
            int32_t w=(int32_t)(p>=0 ? p/32 : -((-p+31)/32));
            if (master_q22(edge[i],mg)!=w) return 11;
        }
    static const int32_t mul_edge[]={INT32_MIN,INT32_MIN+1,-1048576,-65537,
        -65536,-65535,-1,0,1,65535,65536,65537,1048576,INT32_MAX-1,INT32_MAX};
    for (uint32_t g=0; g<=65536; g++)
        for (uint32_t i=0; i<NELEM(mul_edge); i++) {
            int32_t x=mul_edge[i];
            int64_t p=(int64_t)x*g;
            int32_t want=(int32_t)(p>=0 ? p/65536 : -((-p+65535)/65536));
            int32_t old=(x>>16)*(int32_t)g+(int32_t)(((uint32_t)(x&65535)*g)>>16);
            if (mulq16(x,g)!=want || old!=want) return 12;
        }
    track_t *t = &trk[0];
    for (uint32_t e=0; e<NENGINES; e++) {
        t->engine=e;
        int32_t g=eng_balance_gain(t);
        if (g<=0 || g>32768) return 1;
        for (int32_t level=0; level<128; level++) {
            t->p[P_LEVEL]=level;
            int32_t want=(int32_t)(((int64_t)LEVEL_Q12[level]*g+16384)/32768);
            if (eng_balance_level(t)!=want) return 2;
        }
#if FELUCCA_ENGINE_BALANCE
        for (int32_t x=-(1<<20); x<=(1<<20); x++) {
            int64_t wide=(int64_t)x*g+128;
            int32_t want=(int32_t)(wide>=0 ? wide/256 : -((-wide+255)/256));
            if (balance_q22(x,g)!=want) return 3;
            int32_t q=balance_q22(x,g);
            int32_t sum=q>>7, lo=q&127;
            if ((int64_t)sum*128+lo!=want || lo<0 || lo>127) return 4;
            if (g==32768 && (sum!=x || lo)) return 5;
            /* Opposite signals cancel to within one Q22 rounding unit. */
            q+=balance_q22(-x,g);
            if (q<0 || q>1) return 6;
            if (balance_send(x,g)!=((want+64)>>7)) return 7;
            /* All tracks at the same extreme still fit; the wide sum and
             * splitting into Q15 plus dac_lo must reconstruct every Q22 bit. */
            int32_t all=0;
            for (uint32_t k=0; k<NPART; k++) all+=balance_q22(x,g);
            if ((int64_t)all!=(int64_t)want*NPART ||
                (int64_t)(all>>7)*128+(all&127)!=(int64_t)want*NPART) return 8;
        }
#endif
    }
    printf("balance: flag=%d, coefficient and Q22 arithmetic match wide reference; unity exact\n", FELUCCA_ENGINE_BALANCE);
    return 0;
}

/* A private RAM sample fixture: one steady ADPCM sound, available on all keys.
 * No uploader, flash or device is involved. */
static void user_sample(void)
{
    uint8_t *b = (uint8_t *)balance_slots;
    smp_user_hdr_t h;
    memset(b, 0xFF, sizeof balance_slots);
    memset(&h, 0, sizeof h);
    h.magic=SMP_USER_MAGIC; h.version=1; h.nz=1; h.data_len=4096;
    memcpy(h.name, "BALANCE", 7);
    for (uint32_t i=0; i<h.data_len; i++) b[SMP_USER_DATA+i]=(i&2)?0xFF:0x77;
    h.zone[0].off=0; h.zone[0].n=2*h.data_len; h.zone[0].le=h.zone[0].n-1;
    h.zone[0].rate=65536; h.zone[0].root16=60*16; h.zone[0].lo=0; h.zone[0].hi=127;
    /* Same CRC as storage.c, without bringing storage hooks into this test. */
    uint32_t crc=~0u;
    for (uint32_t i=0;i<h.data_len;i++) {
        crc ^= b[SMP_USER_DATA+i];
        for (uint32_t j=0;j<8;j++) crc=(crc>>1) ^ (0xEDB88320u & (0u-(crc&1u)));
    }
    h.crc=~crc;
    memcpy(b,&h,sizeof h);
    smp_user_scan(0);
}

static int measure(uint32_t e, uint32_t p, uint32_t note, int mode, int route)
{
    track_t *t = &trk[0];
    host_tracks_init();
    host_preset(t, e, p);
    t->p[P_LEVEL]=112;
    t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=t->p[P_DIST]=t->p[P_ITYPE]=0;
    t->p[P_AMODE]=0; t->p[P_SPRD]=0;
    if (route) {
        t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=32;
        t->p[P_SPRD]=127;
        if (drum_engine(e)) t->p[P_LPAN0]=48;
        if (route==2) {
            user_sample();
            if (usr_nz[0]!=1) return 3;
            t->p[P_E0]=SMP_NSETS;
            usr_mix[0][SMX_LVL][0]=100;
            usr_mix[0][SMX_REV][0]=60;
            usr_mix[0][SMX_DLY][0]=60;
        }
    }
    trk_note_on(t,note,100);
    int32_t b[CTL],o[CTL*2];
    double sum=0,wsum=0,maxrms=0,el=0,er=0,ec=0,ed=0,ev=0;
    int peak=0;
    uint32_t win=FS/10/CTL*CTL,samples=FS/CTL*CTL,count=0;
    uint64_t envhash=0xCBF29CE484222325ull;
    for (uint32_t f=0;f<samples;f+=CTL) {
        if (mode) {
            mix_block(o,CTL);
            for (uint32_t i=0;i<CTL;i++) {
                b[i]=mode==2 ? mix_l[i] : o[2*i];
                el+=(double)mix_l[i]*mix_l[i]; er+=(double)mix_r[i]*mix_r[i];
                ec+=(double)send_c[i]*send_c[i]; ed+=(double)send_d[i]*send_d[i]; ev+=(double)send_r[i]*send_r[i];
                if (abs(o[2*i])>32767 || abs(o[2*i+1])>32767) return 4;
            }
        } else track_render(t,b,CTL);
        for (uint32_t i=0;i<NVOICE;i++) {
            envhash^=(uint32_t)t->v[i].env_out;
            envhash*=0x100000001B3ull;
        }
        for (uint32_t i=0;i<CTL;i++) {
            int a=abs(b[i]); if (a>peak) peak=a;
            double x=b[i]/32768.; sum+=x*x; wsum+=x*x;
            if (++count==win) {
                double rms=sqrt(wsum/win); if (rms>maxrms) maxrms=rms;
                count=0; wsum=0;
            }
        }
    }
    printf("%u,%s,%u,%s,%u,%d,%.6f,%.6f,%.6f,%016llx,%.0f,%.0f,%.0f,%.0f,%.0f\n",e,ENGINES[e]->name,p,
           ENGINES[e]->presets[p].name,note,route,20*log10(sqrt(sum/samples)+1e-15),
           20*log10(maxrms+1e-15),20*log10(peak/32768.+1e-15),(unsigned long long)envhash,el,er,ec,ed,ev);
    fflush(stdout);
    return 0;
}
static int child(uint32_t e,uint32_t p,uint32_t note,int mode,int route)
{
    pid_t pid=fork();
    if (!pid) _exit(measure(e,p,note,mode,route));
    int status;
    return pid<0 || waitpid(pid,&status,0)<0 || !WIFEXITED(status) || WEXITSTATUS(status);
}
int main(int argc,char **argv)
{
    if (argc<2 || !strcmp(argv[1],"unit")) return unit();
    int mode=!strcmp(argv[1],"raw") ? 0 : !strcmp(argv[1],"premaster") ? 2 : 1;
    int route=!strcmp(argv[1],"routes");
    puts("engine,name,preset,preset_name,note,route,rms_1s_dbfs,max_100ms_rms_dbfs,peak_dbfs,envhash,left_energy,right_energy,chorus_energy,delay_energy,reverb_energy");
    fflush(stdout);
    for (uint32_t e=0;e<NENGINES;e++) if (eng_ok(e)) {
        uint32_t notes[3]={48,60,72};
        if (drum_engine(e)) {notes[0]=36;notes[1]=38;notes[2]=42;}
        for (uint32_t p=0;p<(route?1:ENGINES[e]->npresets);p++)
            for (uint32_t n=0;n<(route?1:3);n++)
                if (child(e,p,notes[n],mode,route)) return 1;
    }
    if (route && child(ENGI_SAMPLE,0,60,1,2)) return 2;
    return 0;
}
