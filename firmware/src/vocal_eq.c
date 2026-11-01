/* SPDX-License-Identifier: GPL-3.0-only */
/* Track 1 owns this state on whichever audio core renders it. Linear mid/side
 * processing keeps STACK's width; flat settings are an exact bypass. */
static struct {int32_t s[3][4],cur[5];tsvf_t hp;uint32_t on,gen;} veq;
static int32_t veq_smooth(int32_t x,int32_t target){int32_t d=target-x;return x+(d>7||d< -7?d/8:d);}
static void vocal_eq_prepare(track_t*t,uint32_t voices){
 if(!vocal_family(t)||!voices){if(veq.on)memset(&veq,0,sizeof veq);return;}
 uint32_t word=vocal_options(t)&VEQ_MASK;if(!word&&!veq.on)return;
 if(veq.gen!=smp_user_gen){memset(&veq,0,sizeof veq);veq.gen=smp_user_gen;}
 veq.on=1;int hp=veq_value(word,0);veq.cur[0]=veq_smooth(veq.cur[0],hp?32768:0);
 if(hp)veq.cur[1]=veq_smooth(veq.cur[1],hp<<7);
 for(unsigned k=1;k<4;k++)veq.cur[k+1]=veq_smooth(veq.cur[k+1],(int32_t)LEVEL_Q12[112+veq_value(word,k)]-4096);
 if(!word&&!(veq.cur[0]|veq.cur[2]|veq.cur[3]|veq.cur[4])){memset(&veq,0,sizeof veq);return;}
 tsvf_coef_k(&veq.hp,veq.cur[1],5793); /* Butterworth high-pass, Q=1/sqrt(2). */
}
static __attribute__((noinline)) void veq_channel(int32_t*b,uint32_t n,int32_t*s){
 for(unsigned i=0;i<n;i++){
  int32_t original=b[i],dry=original>>2,x=dry; /* ample SVF/product headroom */
  if(veq.cur[0]){
   int32_t v3=x-s[1],v1=(veq.hp.a1*s[0]+veq.hp.a2*v3)>>13;
   int32_t v2=s[1]+((veq.hp.a2*s[0]+veq.hp.a3*v3)>>13);
   s[0]=clamp(2*v1-s[0],-150000,150000);s[1]=clamp(2*v2-s[1],-150000,150000);
   int32_t high=x-((5793*v1)>>12)-v2;x+=mulq15(high-x,veq.cur[0]);
  }
  int32_t vl=mulq15(x-s[2],573),low=vl+s[2];s[2]=low+vl;
  int32_t vh=mulq15(x-s[3],7424),high=vh+s[3];s[3]=high+vh;
  int32_t mid=high-low;mid+=mulq15(mid,4369); /* broad 1 kHz peak, unity centre */
  int32_t shaped=x+((low*veq.cur[2]+mid*veq.cur[3]+(x-high)*veq.cur[4])>>12);
  b[i]=original+(shaped-dry)*4;
 }
}
static void vocal_eq_apply(int32_t*b,int32_t*d,int32_t*e,uint32_t n){if(!veq.on)return;veq_channel(b,n,veq.s[0]);if(d)veq_channel(d,n,veq.s[1]);else memset(veq.s[1],0,sizeof veq.s[1]);if(e)veq_channel(e,n,veq.s[2]);else memset(veq.s[2],0,sizeof veq.s[2]);}
