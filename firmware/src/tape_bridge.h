/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after perform.c. Project metadata persists; transport and recorded audio remain transient. */
#define TAPE_STORE_NO_SYSTEM_HEADERS 1
#include "tape_store.h"
#undef TAPE_STORE_NO_SYSTEM_HEADERS
_Static_assert(TF_FILTER_DEFAULT==TAPE_STORE_FILTER_DEFAULT && TF_LFO_DEFAULT==TAPE_STORE_LFO_DEFAULT && TS_STEPS==TAPE_STORE_STEPS,"Tape store defaults");
static tape_sequence_t tape_seq;
static uint64_t tape_transport_samples;
static volatile uint32_t tape_steps[TS_STEPS];
static uint32_t tape_live=TAPE_STORE_LIVE_DEFAULT,tape_edit_gen;
static volatile uint32_t tape_command,tape_seq_on,tape_seq_length=16,tape_page_on;
static volatile uint32_t tape_status_step,tape_status_frames,tape_status_flags;
static void skiff_tape_restart(void){tape_seq.running=0;tape_seq.remain=tape_seq.index=0;tape_release(&tape_state);tape_state.clock=0;tape_state.was_playing=0;tape_transport_samples=0;tf_restart();}
static __attribute__((noinline)) void skiff_tape_begin(void)
{
    if(!tape_wanted && tape_state.enabled){
        tape_release(&tape_state);
        if(!tape_state.mix){tape_state.enabled=0;sl_lent=0;perf_drop_slicer();tape_clear(&tape_state);}
    }
    if(tape_wanted && !tape_state.enabled && !sl_lent && pf.mode==BM_NONE){
        tape_init(&tape_state,&sl_buf[0][0],sizeof sl_buf);
        tf_audio_reset();
        tape_state.enabled=1;sl_lent=1;perf_drop_slicer();
        tape_state.clock=(uint32_t)tape_transport_samples;tape_state.was_playing=song.playing;
        uint32_t base=beat_samples()/4u;if(!base)base=1;
        uint32_t delta=(uint32_t)((uint64_t)base*(uint32_t)tt_clip(song.g[G_SWING],0,100)*2u/500u);
        uint32_t phase=(uint32_t)(tape_transport_samples%(2u*base));
        uint32_t step=(uint32_t)((tape_transport_samples/(2u*base))*2u);
        uint32_t duration=base+delta;
        if(phase>=duration){phase-=duration;++step;duration=base-delta;}
        tape_seq.index=step+(phase?1u:0u);tape_seq.remain=phase?duration-phase:0;
        tape_seq.running=song.playing;
    }
    uint32_t command=tape_command;tape_command=0;
    if(command&0x80000000u){
        /* Live trigger waits for a 1/16. Sequence steps are already aligned. */
        if(tape_state.enabled){ts_apply(&tape_state,command&0x7FFFFFFFu,beat_samples());
            if((command&3u)==TS_TRIGGER)tape_state.quantum=beat_samples()/4u;}
    }
}
static __attribute__((noinline)) void skiff_tape_output(int32_t *out,uint32_t n)
{
    skiff_tape_begin(); /* after all parts/FX finish touching shared storage */
    if(song.playing)tape_transport_samples+=n;else tape_transport_samples=0;
    if(!tape_state.enabled){tape_status_flags=tape_wanted?4:0;tape_status_frames=0;return;}
    tf_prepare(beat_samples(),song.playing,song.playing?tape_transport_samples-n:0);
    for(uint32_t i=0;i<n;i++){mix_l[i]=out[2*i];mix_r[i]=out[2*i+1];}
    ts_process(&tape_seq,&tape_state,tape_seq_on&&!song.g[G_CLOCK]?tape_steps:0,tape_seq_length,mix_l,mix_r,n,
                             song.playing,beat_samples(),(uint32_t)tt_clip(song.g[G_SWING],0,100));
    for(uint32_t i=0;i<n;i++){out[2*i]=mix_l[i];out[2*i+1]=mix_r[i];}
    tape_fx_phase=tape_fx.phase;tape_fx_mod=tape_fx.mod;
    tape_status_step=tape_seq.index?tape_seq.index-1:0;
    tape_status_frames=tape_state.valid_blocks*TT_FRAMES;
    tape_status_flags=1u|(tape_state.held?2u:0u);
}

/* Called with audio interrupts excluded during project replacement. The RAM-only
 * tape lane must not silently carry an old project's captured audio into another. */
static void skiff_tape_project_reset(void)
{
    tf_project_reset();tape_live=TAPE_STORE_LIVE_DEFAULT;++tape_edit_gen;
    tape_wanted=tape_seq_on=tape_command=0;
    for(uint32_t k=0;k<6;k++)tape_send[k]=0;
    if(tape_state.enabled){sl_lent=0;perf_drop_slicer();}
    tape_state.enabled=0;tape_clear(&tape_state);skiff_tape_restart();
    for(uint32_t i=0;i<TS_STEPS;i++)tape_steps[i]=0;
    tape_seq_length=16;tape_status_frames=tape_status_step=tape_status_flags=0;
}

/* Main-loop project capture: no audio, running flags or LFO phase enter flash. */
static void skiff_tape_store(uint8_t *bytes)
{
    tape_store_state_t s;tape_store_defaults(&s);int valid=tape_seq_length<=64;
    for(unsigned i=0;i<TS_STEPS;i++)s.steps[i]=tape_steps[i];
    for(unsigned i=0;i<6;i++){uint32_t send=tape_send[i];valid&=send<=100;s.sends[i]=(uint8_t)send;}
    s.length=(uint8_t)tape_seq_length;s.filter=tape_filter;s.lfo=tape_lfo;s.live=tape_live;
    if(!valid||!tape_store_encode(bytes,&s)){
        memset(bytes,0,TAPE_STORE_BYTES);bytes[0]='T';bytes[1]='P';bytes[2]='1';bytes[TAPE_STORE_BYTES-1]=0xc0;
    }
}
/* Called after project validation, while its existing IRQ guard is held.
 * Playback/recording remain disarmed because the audio was not saved. */
static void skiff_tape_restore(const uint8_t *bytes)
{
    tape_store_state_t s;if(tape_store_decode(&s,bytes)<0)return;
    for(unsigned i=0;i<TS_STEPS;i++)tape_steps[i]=s.steps[i];
    for(unsigned i=0;i<6;i++)tape_send[i]=s.sends[i];
    tape_seq_length=s.length;tape_filter=s.filter;tape_lfo=s.lfo;tape_live=s.live;
    ++tape_edit_gen;
}
