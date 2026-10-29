/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "compressor_math.h"
/* Encoded controls XOR defaults: zero memory is a bypassed lane with sensible
 * controls. One cache-line-aligned state row per track, owned by its audio core. */
#define DC_BYTES 32u
#define DC_TRACKS 6u
#define DC_LANES 8u
_Static_assert(DC_TRACKS==NTRK && DC_LANES==NLANE && DC_BYTES==DC_LANES*4u,"Drum COMP dimensions");
static uint8_t drum_comp[DC_TRACKS][DC_LANES][4] __attribute__((section(".pool")));
typedef struct {int32_t gain,peak,wet;uint32_t last;} dc_lane_t;
typedef struct {dc_lane_t lane[DC_LANES];uint32_t clock;} __attribute__((aligned(128))) dc_track_t;
static dc_track_t drum_comp_state[DC_TRACKS] __attribute__((section(".pool")));
static uint32_t dc_get(uint32_t track,uint32_t lane,uint32_t field)
{
    static const uint8_t defaults[4]={64,64,24,0};
    return drum_comp[track%DC_TRACKS][lane%DC_LANES][field%4]^defaults[field%4];
}
static void dc_set(uint32_t track,uint32_t lane,uint32_t field,uint32_t value)
{
    static const uint8_t defaults[4]={64,64,24,0};
    drum_comp[track%DC_TRACKS][lane%DC_LANES][field%4]=(uint8_t)(value>127?127:value)^defaults[field%4];
}
static int dc_valid(const void *bytes,uint32_t n)
{
    const uint8_t *p=bytes;for(uint32_t i=0;i<n;i++)if(p[i]>127)return 0;return 1;
}
static void dc_reset_audio(uint32_t track){memset(&drum_comp_state[track%DC_TRACKS],0,sizeof(dc_track_t));}
static void dc_defaults(uint32_t track){memset(drum_comp[track%DC_TRACKS],0,DC_BYTES);dc_reset_audio(track);}
static __attribute__((noinline)) int dc_on(const track_t *t,uint32_t lane)
{
    uint32_t k=(uint32_t)(t-trk);
    return drum_comp[k][lane%DC_LANES][3]||drum_comp_state[k].lane[lane%DC_LANES].wet;
}
/* Recover across silent gaps lazily, using exponentiation by squaring instead
 * of running an idle audio loop. Q24 one-pole decay for exactly gap samples. */
static uint32_t dc_decay(uint32_t release,uint32_t gap)
{
    uint32_t a=16777216u-release,b=16777216u;
    while(gap){if(gap&1u)b=(uint32_t)((uint64_t)a*b>>24);a=(uint32_t)((uint64_t)a*a>>24);gap>>=1;}
    return b;
}
/* Called on a lane's isolated rendered signal before lane LEVEL and PAN.
 * No heap, no shared detector, no processing loop when bypassed. */
static __attribute__((noinline)) void dc_process(track_t *t,uint32_t lane,int32_t *b,uint32_t n)
{
    uint32_t k=(uint32_t)(t-trk),peak=0;
    dc_lane_t *s=&drum_comp_state[k].lane[lane%DC_LANES];
    uint32_t th=dc_get(k,lane,0),ratio=dc_get(k,lane,1),rel=dc_get(k,lane,2),mix=dc_get(k,lane,3);
    int32_t wet=mix==127?32768:(int32_t)mix*258;
    if(!wet&&!s->wet){s->gain=s->peak=0;return;}
    if(!s->gain)s->gain=16777216;
    for(uint32_t i=0;i<n;i++){uint32_t a=cmp_abs(b[i]);if(a>peak)peak=a;}
    uint32_t release=(uint32_t)(16777216000ull/(44100u*cmp_release_ms(rel)));
    uint32_t now=drum_comp_state[k].clock,gap=now-s->last;
    if(s->last&&gap>n){
        uint32_t decay=dc_decay(release,gap-n);
        s->peak=(int32_t)((int64_t)s->peak*decay/16777216);
        s->gain=16777216+(int32_t)((int64_t)(s->gain-16777216)*decay/16777216);
    }
    s->last=now;
    if(peak>(uint32_t)s->peak)s->peak=(int32_t)peak;
    else s->peak+=(int32_t)(((int64_t)((int32_t)peak-s->peak)*(release*n))/16777216);
    int32_t target=(int32_t)cmp_target((uint32_t)s->peak,th,ratio);
    for(uint32_t i=0;i<n;i++){
        int32_t error=target-s->gain,x=b[i];
        s->gain+=(int32_t)(((int64_t)error*(error<0?126332u:release))/16777216);
        s->wet+=clamp(wet-s->wet,-256,256);
        int32_t y=(int32_t)((int64_t)x*s->gain/16777216);
        b[i]=x+(int32_t)((int64_t)(y-x)*s->wet/32768);
    }
}
