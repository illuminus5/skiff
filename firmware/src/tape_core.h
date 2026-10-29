/* SPDX-License-Identifier: GPL-3.0-only */
/* Native prototype: rolling stereo IMA tape, fixed caller-owned storage.
 * All control/process calls belong to the audio consumer. No allocation,
 * storage writes, locks, UI, or networking. Source/output: signed 16-bit 44.1k.
 * Capture is 22.05k stereo, 128 frames/block plus 8-byte restart headers.
 * One block stays unpublished while being written. Held playback freezes capture.
 */
#ifndef TAPE_NO_SYSTEM_HEADERS
#include <stdint.h>
#include <string.h>
#endif
#define TT_FRAMES 128u
#define TT_BLOCK (TT_FRAMES+8u)
static const int16_t tt_steps[89]={
7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,
1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,
6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,
29794,32767};
static const int8_t tt_index[8]={-1,-1,-1,-1,2,4,6,8};
typedef struct {int32_t pred,index;} tt_adpcm;
typedef struct {
    uint8_t *data;
    uint32_t blocks,write_block,write_frame,valid_blocks;
    tt_adpcm enc[2];
    int32_t half[2];uint8_t odd,enabled,held,pending,was_playing,clear_pending;
    uint32_t clock,quantum,slice,length,start,loop_frames,history_frames;
    int32_t speed,mix,target_mix;
    int64_t pos;
    int32_t last_wet[2],jump_from[2];uint32_t jump_left;
    uint32_t cache_block;int16_t cache[TT_FRAMES][2];
} tape_t;
static int32_t tt_clip(int32_t x,int32_t lo,int32_t hi){return x<lo?lo:x>hi?hi:x;}
static int32_t tt_decode(tt_adpcm *s,uint32_t code){
    int32_t step=tt_steps[s->index],d=step>>3;
    if(code&1)d+=step>>2;if(code&2)d+=step>>1;if(code&4)d+=step;
    s->pred=tt_clip(s->pred+((code&8)?-d:d),-32768,32767);
    s->index=tt_clip(s->index+tt_index[code&7],0,88);return s->pred;
}
static uint32_t tt_encode(tt_adpcm *s,int32_t x){
    int32_t d=x-s->pred,step=tt_steps[s->index];uint32_t c=0;
    if(d<0){c=8;d=-d;}if(d>=step){c|=4;d-=step;}step>>=1;
    if(d>=step){c|=2;d-=step;}step>>=1;if(d>=step)c|=1;
    tt_decode(s,c);return c;
}
static int tape_init(tape_t *t,void *storage,uint32_t bytes){
    if(!t||!storage||bytes/TT_BLOCK<3)return 0;
    memset(t,0,sizeof *t);t->data=storage;t->blocks=bytes/TT_BLOCK;
    t->cache_block=0xFFFFFFFFu;t->speed=65536;t->target_mix=32768;return 1;
}
static void tape_clear(tape_t *t){
    t->write_block=t->write_frame=t->valid_blocks=0;t->odd=t->held=t->pending=0;
    t->mix=0;t->clear_pending=0;t->jump_left=0;t->last_wet[0]=t->last_wet[1]=0;t->cache_block=0xFFFFFFFFu;t->enc[0]=(tt_adpcm){0,0};t->enc[1]=(tt_adpcm){0,0};
}
static uint32_t tape_capacity(const tape_t *t){return (t->blocks-1)*TT_FRAMES;}
static void tt_capture(tape_t *t,int32_t l,int32_t r){
    if(!t->odd){t->half[0]=l;t->half[1]=r;t->odd=1;return;}
    t->odd=0;uint8_t *p=t->data+t->write_block*TT_BLOCK;
    if(!t->write_frame)for(unsigned c=0;c<2;c++){
        uint16_t v=(uint16_t)t->enc[c].pred;p[c*4]=(uint8_t)v;p[c*4+1]=(uint8_t)(v>>8);
        p[c*4+2]=(uint8_t)t->enc[c].index;p[c*4+3]=0;
    }
    uint32_t a=tt_encode(&t->enc[0],(t->half[0]+l)/2),b=tt_encode(&t->enc[1],(t->half[1]+r)/2);
    p[8+t->write_frame]=(uint8_t)(a|(b<<4));
    if(++t->write_frame==TT_FRAMES){
        t->write_frame=0;t->write_block=(t->write_block+1)%t->blocks;
        if(t->valid_blocks<t->blocks-1)++t->valid_blocks;
    }
}
static void tt_at(tape_t *t,uint32_t frame,int32_t *l,int32_t *r){
    uint32_t oldest=(t->write_block+t->blocks-t->valid_blocks)%t->blocks;
    uint32_t block=(oldest+frame/TT_FRAMES)%t->blocks;
    if(block!=t->cache_block){
        const uint8_t *p=t->data+block*TT_BLOCK;tt_adpcm s[2];
        for(unsigned c=0;c<2;c++){s[c].pred=(int16_t)((uint32_t)p[c*4]|(uint32_t)p[c*4+1]<<8);s[c].index=p[c*4+2];}
        for(unsigned i=0;i<TT_FRAMES;i++){
            t->cache[i][0]=(int16_t)tt_decode(&s[0],p[8+i]&15);
            t->cache[i][1]=(int16_t)tt_decode(&s[1],p[8+i]>>4);
        }
        t->cache_block=block;
    }
    *l=t->cache[frame%TT_FRAMES][0];*r=t->cache[frame%TT_FRAMES][1];
}
/* Requested length is source frames at 22.05k; zero means all valid history.
 * quantum is output samples per 1/16; zero requests immediate triggering. */
static void tape_trigger(tape_t *t,uint32_t slice,uint32_t length,uint32_t quantum){
    t->slice=slice&15u;t->length=length;t->quantum=quantum;t->pending=1;
}
static void tape_release(tape_t *t){t->pending=0;t->held=0;}
static void tt_begin(tape_t *t){
    if(t->held||t->mix){t->jump_left=64;t->jump_from[0]=t->last_wet[0];t->jump_from[1]=t->last_wet[1];}
    t->odd=0;t->pending=0;t->history_frames=t->valid_blocks*TT_FRAMES;
    if(!t->history_frames)return;
    t->start=t->history_frames*t->slice/16u;
    t->loop_frames=t->length&&t->length<t->history_frames?t->length:t->history_frames;
    if(t->loop_frames<16)t->loop_frames=16;
    t->pos=t->speed<0?((int64_t)t->loop_frames-1)*65536:0;
    t->held=1;t->cache_block=0xFFFFFFFFu;
}
static void tape_process(tape_t *t,int32_t *l,int32_t *r,uint32_t n,int playing){
    if(!playing){tape_release(t);t->clock=0;t->was_playing=0;}
    if(playing&&!t->was_playing){t->clock=0;t->was_playing=1;}
    for(uint32_t i=0;i<n;i++){
        int32_t dryl=l[i],dryr=r[i],wetl=0,wetr=0;
        int32_t recordl=dryl,recordr=dryr;
#ifdef TAPE_RECORD_INPUT
        TAPE_RECORD_INPUT(&recordl,&recordr);
#endif
#ifdef TAPE_FX_TICK
        TAPE_FX_TICK();
#endif
        if(t->enabled&&playing&&t->pending&&(!t->quantum||t->clock%t->quantum==0))tt_begin(t);
        if((t->held||t->mix)&&t->history_frames&&t->loop_frames){
            int32_t play_speed=t->speed;
#ifdef TAPE_PLAY_SPEED
            play_speed=TAPE_PLAY_SPEED(play_speed);
#endif
            uint32_t a=(uint32_t)(t->pos>>16),f=(uint32_t)t->pos&65535u;
            uint32_t b=a+1<t->loop_frames?a+1:0;
            int32_t al,ar,bl,br;tt_at(t,(t->start+a)%t->history_frames,&al,&ar);
            tt_at(t,(t->start+b)%t->history_frames,&bl,&br);
            wetl=al+(int32_t)(((int64_t)bl-al)*f/65536);wetr=ar+(int32_t)(((int64_t)br-ar)*f/65536);
            /* Short edge fades suppress discontinuities at loop wrap. */
            uint32_t edge=a<t->loop_frames-1-a?a:t->loop_frames-1-a;
            if(edge<32){wetl=wetl*(int32_t)edge/32;wetr=wetr*(int32_t)edge/32;}
            if(t->jump_left){
                wetl=(t->jump_from[0]*(int32_t)t->jump_left+wetl*(int32_t)(64-t->jump_left))/64;
                wetr=(t->jump_from[1]*(int32_t)t->jump_left+wetr*(int32_t)(64-t->jump_left))/64;
                --t->jump_left;
            }
            /* A stopped tape head must fade to silence, not emit a DC sample. */
            if(!play_speed){wetl=t->last_wet[0]+tt_clip(-t->last_wet[0],-256,256);
                wetr=t->last_wet[1]+tt_clip(-t->last_wet[1],-256,256);t->jump_left=0;}
            t->last_wet[0]=wetl;t->last_wet[1]=wetr;
            t->pos+=tt_clip(play_speed,-262144,262144)/2;
            int64_t period=(int64_t)t->loop_frames*65536;
            while(t->pos<0)t->pos+=period;while(t->pos>=period)t->pos-=period;
        }
#ifdef TAPE_PLAY_FX
        if(t->held||t->mix)TAPE_PLAY_FX(&wetl,&wetr);
#ifdef TAPE_PLAY_FX_IDLE
        else TAPE_PLAY_FX_IDLE();
#endif
#endif
        int32_t target=t->enabled&&t->held?tt_clip(t->target_mix,0,32768):0;
        t->mix+=tt_clip(target-t->mix,-256,256);
#ifdef TAPE_USB_CAPTURE
        /* Wet contribution only, including MIX and release/clear ramps. */
        TAPE_USB_CAPTURE((int32_t)((int64_t)wetl*t->mix/32768),
                         (int32_t)((int64_t)wetr*t->mix/32768));
#endif
        l[i]=dryl+(int32_t)(((int64_t)wetl-dryl)*t->mix/32768);
        r[i]=dryr+(int32_t)(((int64_t)wetr-dryr)*t->mix/32768);
        if(t->clear_pending&&!t->mix)tape_clear(t);
        if(t->enabled&&playing&&!t->held&&!t->mix)tt_capture(t,tt_clip(recordl,-32768,32767),tt_clip(recordr,-32768,32767));
        if(playing)++t->clock;
    }
}
