/* SPDX-License-Identifier: GPL-3.0-only */
#include "stream.h"
#include "stream_tables.h"
#include <limits.h>
#ifndef VS_METRICS
#define VS_METRICS 1
#endif
#if VS_METRICS
#define VS_STAT(x) do{x;}while(0)
#else
#define VS_STAT(x) ((void)0)
#endif
/* Match signed division truncation, using shifts on the audio path. */
static inline __attribute__((always_inline)) int32_t vs_shift0(int32_t x,unsigned bits){
 return (x+(int32_t)(((uint32_t)x>>31)*((1u<<bits)-1u)))>>bits;
}
static int16_t sample(const vs_state*s,int32_t i){
 if(i<0||(uint32_t)i>=s->samples||s->samples-(uint32_t)i>VS_RING)return 0;
 int32_t pos=(int32_t)s->ring_pos-(int32_t)(s->samples-(uint32_t)i);
 if(pos<0)pos+=VS_RING;return s->ring[pos];
}
/* Exact major/minor membership; negative remainders remain outside the scale. */
static __attribute__((noinline,minsize)) int allowed(int note,int root,int minor){int p=(note-root+120)%12;return p>=0&&(((minor?0x5ad:0xab5)>>p)&1);}
/* Q15 fraction with bounded 15-step remainder expansion, no 64-bit divide. */
static uint16_t fraction(uint32_t num,uint32_t den){
 if(!den)return 32767;uint32_t whole=num/den,rem=num%den,q=0;
 if(whole>=2)return 65535;
 for(unsigned k=0;k<15;k++){uint32_t carry=rem>>31;rem<<=1;q<<=1;if(carry||rem>=den){rem-=den;q|=1;}}
 return (uint16_t)((whole<<15)|q);
}
static void begin_analysis(vs_state*s){
 uint64_t energy=0;unsigned peak=0;int32_t base=(int32_t)s->samples-1024;
 for(int i=0;i<256;i++){int32_t a=vs_shift0(sample(s,base+4*i)+sample(s,base+4*i+1)+sample(s,base+4*i+2)+sample(s,base+4*i+3),2);s->decimated[i]=(int16_t)a;unsigned v=(unsigned)(a<0?-a:a);if(v>peak)peak=v;energy+=(int64_t)a*a;}
 unsigned shift=0;while(peak>127){peak>>=1;shift++;}
 for(int i=0;i<256;i++){int32_t a=s->decimated[i];s->decimated[i]=(int16_t)(a<0?-((-a)>>shift):a>>shift);}
 s->analysis_valid=energy>=256u*32u*32u;s->analysis_total=0;s->analysis_lag=1;s->analysis_at=s->samples-512;s->cm[0]=32767;
}
static uint32_t finish_period(vs_state*s){
 if(!s->analysis_valid)return 0;
 for(int lag=7;lag<100;lag++)if(s->cm[lag]<4915&&s->cm[lag]<=s->cm[lag+1]){
  int32_t den=(int32_t)s->cm[lag-1]-2*(int32_t)s->cm[lag]+s->cm[lag+1];
  int32_t frac=den?((int32_t)s->cm[lag-1]-s->cm[lag+1])*32768/den:0;
  if(frac>32768)frac=32768;if(frac< -32768)frac= -32768;
  return (uint32_t)(lag*262144+frac*4);
 }return 0;
}
static unsigned quantize(vs_state*s,uint32_t p){
 unsigned best=24;uint32_t errnum=UINT32_MAX,errden=1;
 for(unsigned n=24;n<=96;n++)if(allowed((int)n,s->root,s->minor)){
  uint32_t t=VS_PERIOD[n-24];uint32_t e=p>t?p-t:t-p;
  if((uint64_t)e*errden<(uint64_t)errnum*t){errnum=e;errden=t;best=n;}
 }
 if(s->held_note>=24&&s->held_note<=96){uint32_t t=VS_PERIOD[s->held_note-24];
  /* 2^(65 cents/1200), Q16, used symmetrically. */
  if((uint64_t)p*65536<(uint64_t)t*68044&&(uint64_t)t*65536<(uint64_t)p*68044)best=(unsigned)s->held_note;
 }s->held_note=(int32_t)best;return best;
}
static void publish_analysis(vs_state*s,uint32_t p){
 uint32_t centre=s->analysis_at;unsigned note=0;
 if(p)note=quantize(s,p);else s->held_note=-1;
 s->history[s->nh++%VS_HISTORY]=(vs_analysis){centre,p,(uint16_t)note,(uint16_t)(p!=0)};
 if(!p){s->next_epoch=centre;return;}
 unsigned period=(p+32768)>>16;if(period<20||period>401)return;
 if(!s->next_epoch||s->next_epoch+2*period<centre-VS_HOP)s->next_epoch=centre-VS_HOP;
 while(s->next_epoch<=centre){int32_t predicted=(int32_t)s->next_epoch,best=predicted,r=(int32_t)period/5;
  for(int32_t j=predicted-r;j<=predicted+r;j++)if(sample(s,j)>sample(s,best))best=j;
  vs_epoch last=s->ne?s->epochs[(s->ne-1)%VS_EPOCHS]:(vs_epoch){0,0,0};
  if(best>=0&&(!s->ne||(uint32_t)best>last.at))s->epochs[s->ne++%VS_EPOCHS]=(vs_epoch){(uint32_t)best,(uint16_t)period,0};
  s->next_epoch=(uint32_t)(best+period);
 }
}
/* Fourfold-decimated analysis: 25 x 128 differences per 64 native samples.
 * Scheduling uses sample positions, so caller block sizes cannot change it. */
static void advance_analysis(vs_state*s,unsigned quantum){
 if(!s->analysis_lag)return;
 unsigned end=s->analysis_lag+quantum;if(end>101)end=101;
 if(s->analysis_valid){
  unsigned products=0,window=quantum==8u?64u:128u;
  for(unsigned lag=s->analysis_lag;lag<end;lag++){
   uint32_t d=0;for(unsigned i=0;i<window;i++){int32_t a=s->decimated[i]-s->decimated[i+lag];d+=(uint32_t)(a*a);}
   VS_STAT(s->products+=window;products+=window);s->analysis_total+=d;s->cm[lag]=fraction(d*lag,s->analysis_total);
  }
  VS_STAT(if(products>s->max_analysis_products)s->max_analysis_products=products);
 }
 s->analysis_lag=end;
 if(end==101){publish_analysis(s,finish_period(s));s->analysis_lag=0;}
}
static vs_analysis past(const vs_state*s,uint32_t at){
 vs_analysis a={0,0,0,0};unsigned count=s->nh<VS_HISTORY?s->nh:VS_HISTORY;
 for(unsigned k=0;k<count;k++){vs_analysis b=s->history[(s->nh-1-k)%VS_HISTORY];if(b.at<=at){a=b;break;}}
 return a;
}
static void launch(vs_state*s,vs_note*note,unsigned voice,unsigned p,unsigned delay,uint32_t target
#if FELUCCA_VOCAL_STRETCH
,int stretch
#endif
){
 int32_t centre=(int32_t)s->samples-1-(int32_t)VS_DELAY+(int32_t)p-(int32_t)delay;
 if(centre<0)return;
 unsigned count=s->ne<VS_EPOCHS?s->ne:VS_EPOCHS;
 if(!count){s->missing_epochs++;return;}
 /* Published epochs increase strictly. Find the first at/after centre in
  * chronological order, then compare its predecessor. On equal distances
  * keep the newer epoch, matching the former newest-first linear scan. */
 unsigned oldest=s->ne-count,lo=0,hi=count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;
  if(s->epochs[(oldest+mid)%VS_EPOCHS].at<(uint32_t)centre)lo=mid+1;else hi=mid;
 }
 unsigned best=(oldest+(lo<count?lo:count-1))%VS_EPOCHS;
 uint32_t at=s->epochs[best].at;
 uint32_t distance=at>(uint32_t)centre?at-(uint32_t)centre:(uint32_t)centre-at;
 if(lo&&lo<count){unsigned previous=(oldest+lo-1)%VS_EPOCHS;
  uint32_t d=(uint32_t)centre-s->epochs[previous].at;
  if(d<distance){best=previous;distance=d;}
 }
 if(distance>2*p){s->missing_epochs++;return;}
 vs_epoch e=s->epochs[best];p=e.period;
 uint32_t free_mask=((1u<<VS_GRAINS)-1u)&~(uint32_t)note->live[voice];
 if(free_mask){unsigned k=(unsigned)__builtin_ctz(free_mask);vs_grain*g=&note->grains[voice][k];note->live[voice]|=(uint16_t)(1u<<k);
  unsigned out=(target+32768)>>16;
  if(out>p
#if FELUCCA_VOCAL_STRETCH
     ||stretch
#endif
  ){
   /* Downward pitch (and raw LEAD stretch in either direction): resample
    * a source-period window across two output periods. This keeps the raw
    * transposed fundamental instead of cancelling upward octave sines.
    * High ten phase bits store input span; low 22 hold a Q14 window. */
   *g=(vs_grain){(int32_t)e.at-(int32_t)p,(2*p)<<22,0x80000000u|((256u<<14)/(2*out)),0,(uint16_t)(2*out)};
  }else *g=(vs_grain){(int32_t)e.at-(int32_t)p,0,(256u<<16)/(2*p),0,(uint16_t)(2*p)};
  return;
 }
 s->dropped_grains++;
}
static int32_t render(vs_state*s,vs_note*note,unsigned voice,int32_t dry,unsigned stride){
 int32_t acc=0;uint32_t weight=0;
 uint32_t live=note->live[voice];
 while(live){unsigned k=(unsigned)__builtin_ctz(live);live&=live-1;vs_grain*g=&note->grains[voice][k];
  unsigned phase=g->phase;int32_t input;
  if(g->step&0x80000000u){
   unsigned span=phase>>22;phase=(phase&0x3fffffu)<<2;
   uint32_t pos=(phase>>8)*span;int32_t at=g->source+(int32_t)(pos>>16);
   int32_t a=sample(s,at),b=sample(s,at+1);
   input=a+vs_shift0((b-a)*(int32_t)((pos&65535)>>1),15);
  }else input=sample(s,g->source+g->age);
  unsigned idx=phase>>16,frac=phase&65535;if(idx>255)idx=255;
  int32_t w=VS_HANN[idx]+vs_shift0((int32_t)(VS_HANN[idx+1]-VS_HANN[idx])*(int32_t)frac,16);
  acc+=vs_shift0(input*w,3);weight+=(uint32_t)w;g->age+=(uint16_t)stride;g->phase+=(g->step&0x7fffffffu)*stride;if(g->age>=g->length)note->live[voice]&=(uint16_t)~(1u<<k);VS_STAT(s->grain_samples++);
 }
 int32_t shifted=weight>1638?acc/(int32_t)(weight/8):dry;VS_STAT(if(weight>1638)s->normalizations++);
 /* Normalization bounds shifted below 33000; the convex wet mix fits int32. */
 return vs_shift0(shifted*note->wet+dry*(32768-note->wet),15);
}
void vs_init(vs_state*s,unsigned root,unsigned minor){unsigned char*p=(unsigned char*)s;for(size_t i=0;i<sizeof(*s);i++)p[i]=0;s->held_note=-1;s->render_bucket=UINT32_MAX;s->root=root%12;s->minor=minor!=0;}

static void vs_feed_scheduled(vs_state*s,int16_t input,unsigned mask,unsigned quantum){
 s->ring[s->ring_pos]=input;if(++s->ring_pos==VS_RING)s->ring_pos=0;s->samples++;
 if(s->samples>=1024&&!(s->samples&(VS_HOP-1)))begin_analysis(s);
 if(s->samples>=1024&&!(s->samples&mask))advance_analysis(s,quantum);
}
void vs_feed(vs_state*s,int16_t input){vs_feed_scheduled(s,input,63u,25u);}
#if FELUCCA_HARMONY
/* 12 x 8 lags + 4 final lags finish at hop+192, exactly as 4 x 25 did.
 * Smaller bursts fit the choir's real-time budget without changing analysis. */
static void vs_feed_harmony(vs_state*s,int16_t input){vs_feed_scheduled(s,input,15u,8u);}
#endif
void vs_note_init(vs_note*n){unsigned char*p=(unsigned char*)n;for(size_t i=0;i<sizeof(*n);i++)p[i]=0;n->phase[1]=0x60000000u;}
static uint32_t note_period(int32_t q8){
 if(q8<24*256)q8=24*256;if(q8>96*256)q8=96*256;
 unsigned i=(unsigned)q8/256-24,f=(unsigned)q8&255;uint32_t p=VS_PERIOD[i];
 return i<72?(uint32_t)((int32_t)p+((int32_t)VS_PERIOD[i+1]-(int32_t)p)*(int32_t)f/256):p;
}
static int32_t mod_cos(uint32_t phase){unsigned i=phase>>24,f=(phase>>8)&65535;int32_t w=VS_HANN[i]+((int32_t)(VS_HANN[i+1]-VS_HANN[i])*(int32_t)f)/65536;return 32767-2*w;}
#if FELUCCA_VOCAL_AUTO
static int vs_auto_note(vs_state*s,int note,unsigned part){
 int n=note;if(n<24)n=24;if(n>96)n=96;
 /* Major/natural-minor scales have no gap wider than two semitones. */
 if(!allowed(n,s->root,s->minor))n+=n>24&&allowed(n-1,s->root,s->minor)?-1:1;
 for(unsigned degree=0;degree<part*2;degree++){do{n++;}while(!allowed(n,s->root,s->minor));}
 if(n>96)n-=12;return n;
}
/* Only evaluated at pitch-analysis updates; keep its search out of the hot
 * renderer's register frame. No additional audio storage or pitch lanes. */
static __attribute__((noinline)) uint32_t vs_auto_period(vs_state*s,int source,int transpose,unsigned part){
 int n=vs_auto_note(s,source+transpose/256,part);if(n>source+24)n-=12;if(n<source-24)n+=12;return note_period(n*256);
}
#endif
void vs_note_render(vs_state*s,vs_note*note,int32_t transpose_q8,unsigned mode,int16_t out[VS_LANES]){
 unsigned continuous=FELUCCA_HARMONY&&mode==7u,stride=continuous?2u:1u;if(continuous)mode=3;
 for(unsigned v=0;v<VS_LANES;v++)out[v]=0;
 if(s->samples<=VS_DELAY)return;
 if(transpose_q8< -24*256)transpose_q8=-24*256;if(transpose_q8>24*256)transpose_q8=24*256;
 uint32_t at=s->samples-1-VS_DELAY,bucket=at>>8;
 /* Analysis stamps are aligned to VS_HOP and published before their delayed
  * playback interval. All note lanes share one lookup and harmony choice. */
 if(s->render_bucket!=bucket){
  vs_analysis a=past(s,at);unsigned hn=a.note;
  if(a.voiced)for(unsigned j=hn+1,found=0;j<=96;j++)if(allowed((int)j,s->root,s->minor)&&++found==2){hn=j;break;}
  s->render_analysis=a;s->harmony_note=(uint16_t)hn;s->render_bucket=bucket;
 }
 vs_analysis a=s->render_analysis;
 int want=a.voiced?32768:0;note->wet+=want>note->wet?128*(int)stride:want<note->wet?-128*(int)stride:0;
 if(note->wet<0)note->wet=0;if(note->wet>32768)note->wet=32768;
 unsigned hn=s->harmony_note;
 unsigned mask=mode==0?1u:mode==1?2u:3u;
#if FELUCCA_VOCAL_STRETCH
 if(mode==3)mask=1u;
#endif
#if FELUCCA_VOCAL_AUTO
 if(mode>=4)mask=1u;
#endif
 int32_t detune[VS_LANES]={0,0};
 unsigned delays[VS_LANES]={0,0};
 for(unsigned v=0;v<VS_LANES;v++){
  if(!(mask&(1u<<v)))continue;
  int32_t dry=sample(s,(int32_t)at-(int32_t)delays[v]);
#if FELUCCA_VOCAL_STRETCH
  if(mode==3&&!transpose_q8&&!continuous){out[v]=(int16_t)dry;note->period[v]=note->countdown[v]=note->live[v]=0;continue;}
#endif
  if(a.voiced){
   if(!note->period[v]||!(s->samples&(VS_HOP-1))){
    uint32_t target;
#if FELUCCA_VOCAL_AUTO
    if(mode>=4)target=vs_auto_period(s,a.note,transpose_q8,mode-4);else
#endif
#if FELUCCA_VOCAL_STRETCH
    if(mode==3){uint32_t factor=((note_period(60*256+transpose_q8)>>6)*1024u)/(VS_PERIOD[36]>>12);target=(uint32_t)(((uint64_t)a.period*factor)>>16);note->period[v]=target;}else
#endif
    target=note_period((int32_t)(v==1?hn:a.note)*256+transpose_q8+detune[v]);
    if(!note->period[v])note->period[v]=target;
    if(
#if FELUCCA_VOCAL_STRETCH
       mode!=3&&
#endif
       !(s->samples&(VS_HOP-1)))note->period[v]=(uint32_t)((int32_t)note->period[v]+((int32_t)target-(int32_t)note->period[v])/4);
   }
   if(note->countdown[v]<=65536u*stride){launch(s,note,v,(a.period+32768)>>16,delays[v],note->period[v]
#if FELUCCA_VOCAL_STRETCH
,mode==3
#if FELUCCA_VOCAL_AUTO
||mode>=4
#endif
#endif
);note->countdown[v]+=note->period[v];}
   note->countdown[v]=note->countdown[v]>65536u*stride?note->countdown[v]-65536u*stride:0;
  }else{note->countdown[v]=note->period[v]=0;}
  int32_t x=render(s,note,v,dry,stride);out[v]=(int16_t)(x>32767?32767:x< -32768?-32768:x);
 }
 /* phase[] is retained in the state ABI; rendering never reads it. */
}
