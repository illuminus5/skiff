/* SPDX-License-Identifier: GPL-3.0-only */
/* One bounded auxiliary lane. Packed steps can be published with one 32-bit
 * store on the FM-1. The audio side snapshots a word at each step boundary.
 * Step 0 CAPTURE releases frozen tape; TRIGGER restarts it; HOLD keeps it running.
 */
#define TS_STEPS 64u
enum { TS_CAPTURE,TS_TRIGGER,TS_HOLD,TS_CLEAR };
typedef struct {uint32_t remain,index;uint8_t running;} tape_sequence_t;
static const uint32_t ts_pitch[25]={32768,34716,36781,38968,41285,43740,46341,49097,52016,55109,58386,61858,65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
static uint32_t ts_pack(uint32_t action,uint32_t slice,uint32_t length,int32_t speed,uint32_t mix,int32_t pitch){
    return (action&3u)|((slice&15u)<<2)|((length>4?4:length)<<6)|
        ((uint32_t)(tt_clip(speed,-400,400)+400)<<9)|((mix>100?100:mix)<<19)|
        ((uint32_t)(tt_clip(pitch,-12,12)+12)<<26);
}
static void ts_apply(tape_t *t,uint32_t word,uint32_t beat){
    uint32_t action=word&3u;
    if(action==TS_CAPTURE){tape_release(t);return;}
    if(action==TS_CLEAR){tape_release(t);t->clear_pending=1;return;}
    uint32_t pitch=(word>>26)&31u;if(pitch>24)pitch=24;
    int32_t speed=(int32_t)((word>>9)&1023u)-400;
    int32_t previous=t->speed;
    t->speed=(int32_t)((int64_t)tt_clip(speed,-400,400)*ts_pitch[pitch]/100);
    if(!previous&&t->speed&&t->held){t->jump_left=64;t->jump_from[0]=t->last_wet[0];t->jump_from[1]=t->last_wet[1];}
    t->target_mix=tt_clip((int32_t)((word>>19)&127u),0,100)*32768/100;
    if(action==TS_TRIGGER){
        uint32_t len=(word>>6)&7u;if(len>4)len=4;
        uint32_t frames=(uint32_t)((uint64_t)beat*(1u<<len)/8u); /* 1/4, 1/2, 1, 2, 4 beats at half rate */
        tape_trigger(t,(word>>2)&15u,frames,0);
    }
}
static void ts_process(tape_sequence_t *s,tape_t *t,const volatile uint32_t *steps,uint32_t length,
                       int32_t *l,int32_t *r,uint32_t n,int playing,uint32_t beat,uint32_t swing){
    if(!playing){s->running=0;s->remain=s->index=0;tape_process(t,l,r,n,0);return;}
    if(!s->running){s->running=1;s->remain=s->index=0;}
    if(!length||length>TS_STEPS)length=16;
    uint32_t at=0;
    while(at<n){
        if(!s->remain){
            s->index%=length;if(steps)ts_apply(t,steps[s->index],beat);
            uint32_t base=beat/4u;if(!base)base=1;
            uint32_t d=(uint32_t)((uint64_t)base*(swing>100?100:swing)*2u/500u);
            s->remain=(s->index&1u)?base-d:base+d;
            if(!s->remain)s->remain=1;
            ++s->index;
        }
        uint32_t part=n-at<s->remain?n-at:s->remain;
        tape_process(t,l+at,r+at,part,1);s->remain-=part;at+=part;
    }
}
