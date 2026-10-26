/* SPDX-License-Identifier: GPL-3.0-only */
/* Playback only. Coefficients/tables are Skiff's 44.1 kHz trapezoidal SVF.
 * UI publishes two aligned words; audio owns all filter/LFO state. */
#define TF_FILTER_DEFAULT ((127u<<7)|(100u<<21))
#define TF_LFO_DEFAULT (48u|(1u<<18))
enum {TF_OFF,TF_LP,TF_HP,TF_SPEED,TF_PITCH,TF_LEVEL,TF_DEST_N};
static volatile uint32_t tape_filter=TF_FILTER_DEFAULT,tape_lfo=TF_LFO_DEFAULT;
static volatile uint32_t tape_fx_phase;
static volatile int32_t tape_fx_mod;
typedef struct {
    tsvf_t hp,lp;
    int32_t h1[2],h2[2],l1[2],l2[2];
    int32_t hp_pos,lp_pos,res,hp_old,lp_old,res_old;
    int32_t hp_blend,lp_blend,mix,gain,mod,held;
    uint32_t filter,lfo,phase,inc,rng,ctl,playing,beat,rate,wet;
} tape_fx_t;
static tape_fx_t tape_fx={.lp_pos=127*256,.hp_old=-1,.lp_old=-1,.res_old=-1,.gain=32768,.rng=1};
static int32_t tf_clip(int32_t x,int32_t lo,int32_t hi){return x<lo?lo:x>hi?hi:x;}
static int32_t tf_ramp(int32_t x,int32_t y,int32_t step){return x+tf_clip(y-x,-step,step);}
static void tf_audio_reset(void){
    memset(&tape_fx,0,sizeof tape_fx);tape_fx.lp_pos=127*256;
    tape_fx.hp_old=tape_fx.lp_old=tape_fx.res_old=-1;tape_fx.gain=32768;tape_fx.rng=1;
    tape_fx_phase=0;tape_fx_mod=0;
}
static void tf_project_reset(void){tape_filter=TF_FILTER_DEFAULT;tape_lfo=TF_LFO_DEFAULT;tf_audio_reset();}
static void tf_restart(void){tape_fx.phase=0;tape_fx.playing=0;tape_fx.mod=0;tape_fx_phase=0;}
/* Sine uses one quarter with linear interpolation. The waveform is control
 * data, never an audio oscillator. SAW/SQUARE/S&H edges slew through mod. */
static int32_t tf_wave(uint32_t phase,uint32_t shape,int32_t held){
    static const uint16_t quarter[17]={0,3212,6393,9512,12539,15446,18204,20787,23170,25329,27245,28898,30273,31356,32137,32609,32767};
    if(shape==4)return held;
    if(shape==3)return phase<0x80000000u?32767:-32767;
    if(shape==2)return (int32_t)(phase>>16)-32768;
    uint32_t q=phase>>30,p=phase&0x3fffffffu;
    if(q&1u)p=0x40000000u-p;
    int32_t v;
    if(shape==1)v=(int32_t)(p>>15);
    else {
        uint32_t i=p>>26,f=(p>>10)&65535u;
        v=i>=16?32767:(int32_t)quarter[i]+(int32_t)((quarter[i+1]-quarter[i])*f/65536u);
    }
    return q>=2?-v:v;
}
static uint32_t tf_rng(tape_fx_t *f){uint32_t x=f->rng;x^=x<<13;x^=x>>17;x^=x<<5;return f->rng=x?x:1;}
static void tf_prepare(uint32_t beat,int playing,uint64_t transport){
    tape_fx_t *f=&tape_fx;
    f->filter=tape_filter;f->lfo=tape_lfo;
    uint32_t rate=f->lfo&255u;if(rate>135)rate=135;
    if(rate>=128){
        uint32_t s=rate-128;uint32_t period=s<5?beat*(16u>>s):beat>>(s-4);
        if(!period)period=1;
        if(f->beat!=beat||f->rate!=rate||(!f->playing&&playing))
            f->phase=(uint32_t)(((transport%period)<<32)/period);
        f->inc=(uint32_t)((1ull<<32)/period);
    }else f->inc=LFO_INC[rate]/CTL;
    if(!playing)f->phase=0;
    else if(!f->playing&&rate<128)f->phase=0;
    f->playing=!!playing;f->beat=beat;f->rate=rate;
}
/* Coefficients move every 16 samples and slew to new cutoff/resonance values.
 * Bypass stages crossfade separately, so OFF/OPEN do not reset a live signal. */
static void tf_control(tape_fx_t *f){
    uint32_t dest=(f->lfo>>18)&7u;
    int32_t hp=(int32_t)(f->filter&127u)*256,lp=(int32_t)((f->filter>>7)&127u)*256;
    if(dest==TF_HP)hp+=f->mod*127/128;
    if(dest==TF_LP)lp+=f->mod*127/128;
    hp=tf_clip(hp,0,127*256);lp=tf_clip(lp,0,127*256);
    int32_t res=tf_clip((int32_t)((f->filter>>14)&127),0,100);
    f->hp_pos+= (hp-f->hp_pos)/4;f->lp_pos+=(lp-f->lp_pos)/4;
    if(hp-f->hp_pos>-4&&hp-f->hp_pos<4)f->hp_pos=hp;
    if(lp-f->lp_pos>-4&&lp-f->lp_pos<4)f->lp_pos=lp;
    f->res=tf_ramp(f->res,res,4);
    if(f->hp_old!=f->hp_pos||f->res_old!=f->res)tsvf_coef_k(&f->hp,f->hp_pos,8192-f->res*61);
    if(f->lp_old!=f->lp_pos||f->res_old!=f->res)tsvf_coef_k(&f->lp,f->lp_pos,8192-f->res*61);
    f->hp_old=f->hp_pos;f->lp_old=f->lp_pos;f->res_old=f->res;
}
static void tf_tick(void){
    tape_fx_t *f=&tape_fx;
    int32_t depth=tf_clip((int32_t)((f->lfo>>11)&127u),0,100);
    uint32_t shape=(f->lfo>>8)&7u,dest=(f->lfo>>18)&7u;
    int32_t target=dest&&dest<TF_DEST_N?tf_wave(f->phase,shape>4?0:shape,f->held)*depth/100:0;
    f->mod=tf_ramp(f->mod,target,1024);
    if(!(f->ctl++&15u))tf_control(f);
    int32_t hp=(int32_t)(f->filter&127u)*256,lp=(int32_t)((f->filter>>7)&127u)*256;
    if(dest==TF_HP)hp+=f->mod*127/128;if(dest==TF_LP)lp+=f->mod*127/128;
    f->hp_blend=tf_ramp(f->hp_blend,hp>0?32768:0,256);
    f->lp_blend=tf_ramp(f->lp_blend,lp<127*256?32768:0,256);
    f->mix=tf_ramp(f->mix,tf_clip((int32_t)((f->filter>>21)&127u),0,100)*32768/100,256);
    int32_t gain=dest==TF_LEVEL?32768-depth*16384/100+f->mod/2:32768;
    f->gain=tf_ramp(f->gain,tf_clip(gain,0,32768),256);
    if(f->playing){uint32_t old=f->phase;f->phase+=f->inc;if(f->phase<old)f->held=(int32_t)(tf_rng(f)>>16)-32768;}
}
/* Wide intermediates and bounded states protect resonant extremes. */
static int32_t tf_svf(const tsvf_t *c,int32_t in,int32_t *ic1,int32_t *ic2,int32_t damp,int high){
    int32_t v3=in-*ic2;
    int32_t v1=(int32_t)(((int64_t)c->a1*(*ic1)+(int64_t)c->a2*v3)/8192);
    int32_t v2=*ic2+(int32_t)(((int64_t)c->a2*(*ic1)+(int64_t)c->a3*v3)/8192);
    *ic1=tf_clip(2*v1-*ic1,-150000,150000);*ic2=tf_clip(2*v2-*ic2,-150000,150000);
    return high?in-(int32_t)((int64_t)damp*v1/4096)-v2:v2;
}
static void tf_process(int32_t *l,int32_t *r){
    tape_fx_t *f=&tape_fx;int32_t *out[2]={l,r};f->wet=1;
    for(unsigned ch=0;ch<2;ch++){
        int32_t dry=*out[ch],x=dry;
        if(f->mix){
            if(f->hp_blend){int32_t y=tf_svf(&f->hp,x,&f->h1[ch],&f->h2[ch],8192-f->res*61,1);
                x+=(int32_t)((int64_t)(tf_clip(y,-131072,131072)-x)*f->hp_blend/32768);}
            else f->h1[ch]=f->h2[ch]=0;
            if(f->lp_blend){int32_t y=tf_svf(&f->lp,x,&f->l1[ch],&f->l2[ch],8192-f->res*61,0);
                x+=(int32_t)((int64_t)(tf_clip(y,-131072,131072)-x)*f->lp_blend/32768);}
            else f->l1[ch]=f->l2[ch]=0;
            x=dry+(int32_t)((int64_t)(x-dry)*f->mix/32768);
        }else {f->h1[ch]=f->h2[ch]=f->l1[ch]=f->l2[ch]=0;}
        *out[ch]=tf_clip((int32_t)((int64_t)x*f->gain/32768),-32768,32767);
    }
}
/* Capture-only mode needs no audio filtering. Clear old integrators once,
 * retaining the running LFO so the next trigger uses its current phase. */
static void tf_idle(void){
    tape_fx_t *f=&tape_fx;
    if(!f->wet)return;
    memset(f->h1,0,sizeof f->h1);memset(f->h2,0,sizeof f->h2);
    memset(f->l1,0,sizeof f->l1);memset(f->l2,0,sizeof f->l2);f->wet=0;
}
static int32_t tf_speed(int32_t base){
    tape_fx_t *f=&tape_fx;uint32_t dest=(f->lfo>>18)&7u;int64_t rate=base;
    if(dest==TF_SPEED)rate=(int64_t)base*(32768+f->mod)/32768;
    if(dest==TF_PITCH){
        static const uint32_t pitch[25]={32768,34716,36781,38968,41285,43740,46341,49097,52016,55109,58386,61858,65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,131072};
        uint32_t p=(uint32_t)(tf_clip(f->mod,-32768,32767)+32768)*24u,i=p>>16;
        uint32_t g=pitch[i]+(uint32_t)((uint64_t)(pitch[i+1]-pitch[i])*(p&65535u)/65536);
        rate=(int64_t)base*g/65536;
    }
    return (int32_t)(rate < -262144?-262144:rate>262144?262144:rate);
}
#define TAPE_FX_TICK() tf_tick()
#define TAPE_PLAY_FX(l,r) tf_process(l,r)
#define TAPE_PLAY_SPEED(s) tf_speed(s)

#define TAPE_PLAY_FX_IDLE() tf_idle()
