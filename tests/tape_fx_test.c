/* SPDX-License-Identifier: GPL-3.0-only */
#define TAPE_FX_FIXTURE_ONLY 1
#include "tape_fx_fixture.inc"
#include <assert.h>
#include <math.h>
static uint32_t filter_word(unsigned hp,unsigned lp,unsigned res,unsigned mix){return hp|(lp<<7)|(res<<14)|(mix<<21);}
static uint32_t lfo_word(unsigned rate,unsigned shape,unsigned depth,unsigned dest){return rate|(shape<<8)|(depth<<11)|(dest<<18);}
static double energy(unsigned hp,unsigned lp,unsigned res,unsigned mix,double hz){
    tf_project_reset();tape_filter=filter_word(hp,lp,res,mix);tf_prepare(22050,1,0);double sum=0;
    for(unsigned i=0;i<16000;i++){
        int32_t x=(int32_t)(10000*sin(6.283185307179586*hz*i/44100)),l=x,r=0;
        tf_tick();tf_process(&l,&r);assert(!r);
        assert(l>=-32768&&l<=32767);
        if(!mix||(hp==0&&lp==127))assert(l==x);
        if(i>=8000)sum+=(double)l*l;
    }
    return sum;
}
static void capture_invariant(uint8_t *storage,unsigned effects){
    tape_t t;assert(tape_init(&t,storage,TT_BLOCK*5));t.enabled=1;tf_project_reset();
    if(effects){tape_filter=filter_word(80,30,100,100);tape_lfo=lfo_word(135,3,100,TF_SPEED);}
    for(unsigned b=0;b<40;b++){
        tf_prepare(22050,1,b*CTL);tape_input_begin(CTL);int32_t l[CTL],r[CTL];
        for(unsigned i=0;i<CTL;i++){tape_input[0].l[i]=(int32_t)((b*CTL+i)*13%20001)-10000;tape_input[0].r[i]=-tape_input[0].l[i];l[i]=21000;r[i]=17000;}
        tape_process(&t,l,r,CTL,1);
        for(unsigned i=0;i<CTL;i++)assert(l[i]==21000&&r[i]==17000);
    }
}
int main(void){
    assert(neutral_fixture()==0x3647cf6a27fcce62ull);
    double low=energy(0,127,0,100,100),high=energy(0,127,0,100,8000);
    assert(energy(0,57,0,100,100)>low*0.7);
    assert(energy(0,57,0,100,8000)<high*0.02);
    assert(energy(64,127,0,100,100)<low*0.02);
    assert(energy(64,127,0,100,8000)>high*0.7);
    energy(100,20,100,0,1000);
    for(unsigned hp=0;hp<=127;hp+=127)for(unsigned lp=0;lp<=127;lp+=127)energy(hp,lp,100,100,900);
    uint8_t a[TT_BLOCK*5]={0},b[TT_BLOCK*5]={0};capture_invariant(a,0);capture_invariant(b,1);assert(!memcmp(a,b,sizeof a));
    assert(tf_wave(0,0,0)==0&&tf_wave(0x40000000,0,0)>32700&&tf_wave(0xc0000000,0,0)<-32700);
    assert(tf_wave(0,1,0)==0&&tf_wave(0x40000000,1,0)==32768&&tf_wave(0xc0000000,1,0)==-32768);
    for(unsigned shape=0;shape<5;shape++)for(unsigned dest=0;dest<TF_DEST_N;dest++){
        tf_project_reset();tape_lfo=lfo_word(127,shape,0,dest);tf_prepare(22050,1,0);
        for(unsigned i=0;i<300;i++){tf_tick();int32_t l=12345,r=-4567;tf_process(&l,&r);assert(l==12345&&r==-4567&&tf_speed(65536)==65536);}
    }
    tf_project_reset();tape_lfo=lfo_word(132,0,100,TF_LP);tf_prepare(22050,1,11025);
    assert(tape_fx.phase==0x80000000u);uint32_t start=tape_fx.phase;
    for(unsigned i=0;i<22050;i++)tf_tick();
    assert((uint32_t)(start-tape_fx.phase)<22050u);
    tf_prepare(11025,1,11025);assert(tape_fx.inc==(uint32_t)((1ull<<32)/11025));
    tf_restart();tf_prepare(22050,0,0);for(unsigned i=0;i<100;i++)tf_tick();assert(!tape_fx.phase);
    tf_project_reset();tape_lfo=lfo_word(64,3,100,TF_SPEED);tf_prepare(22050,1,0);
    for(unsigned i=0;i<1000;i++)tf_tick();assert(tf_speed(65536)>130000&&tf_speed(-65536)<-130000&&tf_speed(0)==0&&tf_speed(262144)==262144);
    tape_lfo=lfo_word(64,3,100,TF_PITCH);tf_prepare(22050,1,0);assert(tf_speed(65536)>130000);
    tape_fx.phase=0xc0000000u;for(unsigned i=0;i<1000;i++)tf_tick();assert(tf_speed(65536)>=32768&&tf_speed(65536)<33000);
    tape_lfo=lfo_word(64,4,100,TF_LP);tf_prepare(22050,1,0);tape_fx.phase=0xfffffff0u;tf_tick();assert(tape_fx.held!=0);int held=tape_fx.held;tf_tick();assert(held==tape_fx.held);
    /* Resonance/coefficient extremes and malicious packed settings stay bounded. */
    uint32_t random=9;
    for(unsigned i=0;i<40000;i++){
        random=random*1664525u+1013904223u;
        if(!(i%32)){tape_filter=random;tape_lfo=random^0xa5a5a5a5u;tf_prepare(22050,1,i);}
        int32_t l=(int16_t)random,r=(int16_t)(random>>16);tf_tick();tf_process(&l,&r);
        assert(l>=-32768&&l<=32767&&r>=-32768&&r<=32767);
        int32_t speed=tf_speed((i&1)?262144:-262144);assert(speed>=-262144&&speed<=262144);
    }
    /* Wet USB receives playback FX too; live monitoring does not enter it. */
    tf_project_reset();tape_lfo=lfo_word(0,3,100,TF_LEVEL);tf_prepare(22050,1,0);tape_fx.phase=0xc0000000u;
    for(unsigned i=0;i<200;i++)tf_tick();assert(tape_fx.gain<=1);
    tape_t t;uint8_t store[TT_BLOCK*4]={0};tape_init(&t,store,sizeof store);t.enabled=1;
    for(unsigned i=0;i<768;i++)tt_capture(&t,14000,7000);
    uint8_t before[sizeof store];memcpy(before,store,sizeof store);tape_trigger(&t,0,200,0);t.mix=t.target_mix=32768;
    int32_t l[CTL],r[CTL];for(unsigned i=0;i<CTL;i++)l[i]=r[i]=19000;
    usb_route_begin(CTL);tape_process(&t,l,r,CTL,1);
    for(unsigned i=0;i<2*CTL;i++)assert(!usb_tape_return[i]);assert(!memcmp(before,store,sizeof store));
    tf_idle();assert(!tape_fx.wet&&!tape_fx.h1[0]&&!tape_fx.l1[1]);
    tf_project_reset();assert(tape_filter==TF_FILTER_DEFAULT&&tape_lfo==TF_LFO_DEFAULT&&!tape_fx_phase);
    puts("Tape playback FX: neutral audio, HP/LP response, stereo isolation, bounded resonance, capture/dry preservation, LFO shapes/sync/depth/rate bounds, USB wet and resets passed");return 0;
}
