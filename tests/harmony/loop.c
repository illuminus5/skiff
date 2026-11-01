/* SPDX-License-Identifier: GPL-3.0-only */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
static int32_t out[CTL*2];
static void tick(track_t*t){mix_block(out,CTL);for(unsigned i=0;i<CTL*2;i++)assert(out[i]>=-32768&&out[i]<=32767);(void)t;}
static track_t*init(void){memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_VOICE]=V_LEGATO;t->p[P_REL]=0;t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=t->p[P_DIST]=0;
 const smp_zone_t*z=&usr_zone[0][0];assert(z->n>24000);vtrim_data d={0};d.region=(vocal_trim_region){{0,4096,8192,24000},z->n};voice_t decoder={0};while(decoder.ph[0]<4096)sample_next(z,&decoder,0);d.state[1]=(uint16_t)decoder.s[0]|((uint32_t)decoder.s[1]<<16);vtrim_table[0][0][0]=vtrim_table[0][0][1]=d;vtrim_active[0][0]=0;return t;}
static void scenario(unsigned position,unsigned loops){
 track_t*t=init();trk_note_on(t,60,100);harmony_part_t*h=harm(t);unsigned wraps=0,previous=0;
 for(unsigned b=0;b<200000;b++){tick(t);unsigned p=h->decoder.ph[0];if(p<previous)wraps++;previous=p;if(wraps>=loops&&p>=position)break;assert(b+1<200000);}
 assert(voices_busy()==5&&!h->done);uint64_t tail_energy=0;unsigned age=h->source.samples,p=h->decoder.ph[0];trk_note_off(t,60);assert(!t->nmono);
 for(unsigned k=0;k<5;k++)assert(t->v[k].gate==0&&t->v[k].stage!=3);
 while(!h->done){tick(t);assert(h->decoder.ph[0]>=p);p=h->decoder.ph[0];if(p>10000&&p<23000)for(unsigned i=0;i<CTL*2;i++)tail_energy+=(int64_t)out[i]*out[i];assert(voices_busy()==5);for(unsigned k=0;k<5;k++)assert(t->v[k].stage!=3);}
 assert(p==24000&&h->source.samples>age&&tail_energy>0);unsigned drain=h->done;assert(drain>=h->source.samples);
 for(unsigned b=0;b<10000&&voices_busy();b++)tick(t);assert(!voices_busy()&&!voice_kills);assert(h->source.samples>=drain);
 printf("outro release at %u after %u wraps: forward-only through SAMPLE END, REL=0 deferred, drain complete\n",position,wraps);
}
static int faded(const int16_t*pcm,unsigned pos,unsigned start,unsigned end){unsigned n=pos-start;if(end-pos-1<n)n=end-pos-1;return n<64?pcm[pos]*(int)n/64:pcm[pos];}
static int blend_reference(int a,int b,unsigned n,unsigned w){double t=0.5*(1.0-cos(3.14159265358979323846*(double)n/w));return (int)(a*(1.0-t)+b*t);}
static void seam(unsigned start){
 track_t*t=init();const smp_zone_t*z=&usr_zone[0][0];int16_t pcm[24000];voice_t d={0};for(unsigned i=0;i<24000;i++)pcm[i]=(int16_t)sample_next(z,&d,0);
 harmony_start(t,0,0);harmony_part_t*h=harm(t);voice_t seek={0};while(seek.ph[0]<start)sample_next(z,&seek,0);h->trim.region.edge[0]=start;h->trim.state[0]=(uint16_t)seek.s[0]|((uint32_t)seek.s[1]<<16);harmony_rewind(h);h->running=1;
 unsigned pos=start,blend=0,head=0,cycle_wraps=0,inner=0,whole=0,release=0;int previous=0,outro=0;
 for(unsigned i=0;i<150000;i++){
  if(!outro&&cycle_wraps>=3){outro=1;release=8192-pos+256;}
  int held=!release;if(release)release--;
  if(held&&pos>=8192&&blend==220){pos=4096+220;blend=0;inner++;cycle_wraps++;}
  int outer=held&&pos>=24000&&!blend;if(outer){pos=start;outro=0;cycle_wraps=0;whole++;assert(previous==0);}
  unsigned rawpos=pos;int raw=faded(pcm,rawpos,start,24000);pos++;
  if(rawpos>=8192-220&&rawpos<8192){if(!blend)head=0;blend++;}else if(blend)blend--;
  int expected=raw;if(blend){expected=blend_reference(raw,faded(pcm,4096+head,start,24000),blend,220);head++;}
  int actual=harmony_input(h,z,held,220);assert(abs(actual-expected)<=8&&h->decoder.ph[0]==pos);if(outer)assert(actual==0);assert(!h->done);h->source.samples++;previous=actual;
 }
 assert(inner>=9&&whole>=3);puts("PCM oracle: held sustain honors LOOP START/END repeatedly; release exits, new held key restarts only at SAMPLE END, next cycle honors sustain again.");
}
static void held_region(track_t*t){unsigned previous=0,wraps=0,entered=0;for(unsigned b=0;b<3000;b++){tick(t);harmony_part_t*h=harm(t);unsigned p=h->decoder.ph[0];assert(p<=h->trim.region.edge[2]&&!h->done&&voices_busy()==5);if(p<previous)wraps++;if(p>=h->trim.region.edge[1])entered=1;if(entered)assert(p>=h->trim.region.edge[1]);previous=p;}assert(wraps>=3);}
static void end_boundary(void){
 for(unsigned span=32;span<=4096;span*=8){unsigned w=span/2;if(w>H_LOOP_FADE)w=H_LOOP_FADE;
  unsigned points[]={0,w/2,w-1,w};
  for(unsigned c=0;c<4;c++){
   track_t*t=init();const smp_zone_t*z=&usr_zone[0][0];int16_t pcm[24000];voice_t d={0};for(unsigned i=0;i<24000;i++)pcm[i]=(int16_t)sample_next(z,&d,0);
   harmony_start(t,0,0);harmony_part_t*h=harm(t);h->running=1;unsigned end=4096+span;h->trim.region.edge[2]=h->trim.region.edge[3]=end;unsigned wraps=0;
   for(unsigned i=0;i<100000;i++){unsigned before=h->decoder.ph[0];harmony_input(h,z,1,w);h->source.samples++;if(h->decoder.ph[0]<before)wraps++;if(wraps>=3&&h->decoder.ph[0]>=end-w+points[c])break;assert(i+1<100000);}
   assert(h->decoder.ph[1]<=2*w);unsigned prev=h->decoder.ph[0];
   while(!h->done){unsigned pos=h->decoder.ph[0],j=h->decoder.ph[1];int weight=(pos>=h->trim.region.edge[2]-w&&pos<h->trim.region.edge[2])?h->decoder.s[7]+1:h->decoder.s[7]>0?h->decoder.s[7]-1:0,raw=0;
    if(pos<end){raw=pcm[pos];if(end-pos-1<64)raw=raw*(int)(end-pos-1)/64;}
    if(!h->decoder.s[7]&&weight)j=0;int expected=raw;if(weight){assert(j<2*w);unsigned q=4096+j;int head=pcm[q];if(end-q-1<64)head=head*(int)(end-q-1)/64;expected=blend_reference(raw,head,(unsigned)weight,w);}
    assert(abs(harmony_input(h,z,0,w)-expected)<=8);h->source.samples++;assert(h->decoder.ph[0]>=prev&&h->decoder.ph[0]<=end);prev=h->decoder.ph[0];assert(h->decoder.ph[1]<=2*w);
   }
   assert(h->decoder.ph[0]==end&&!h->decoder.s[7]);
  }
 }
 puts("LOOP END = SAMPLE END: short/long loops, all tested crossfade release phases match the smooth PCM oracle.");
}
static void cache_and_curve(void){
 track_t*t=init();harmony_start(t,0,0);harmony_part_t*h=harm(t);unsigned char mask[sizeof(*h)]={0};unsigned char*saved=malloc(sizeof(*h));assert(saved);memcpy(saved,h,sizeof(*h));
 for(unsigned i=0;i<2*H_LOOP_FADE;i++){
  unsigned char*p=harmony_loop_pcm(h,i);size_t off=(size_t)(p-(unsigned char*)h);assert(off+2<=sizeof(*h)&&!mask[off]&&!mask[off+1]);int owner=0;
  for(unsigned g=0;g<5;g++){uintptr_t base=(uintptr_t)&h->singer[g].grains[1][0],addr=(uintptr_t)p;unsigned used=g?3:8;if(addr>=base&&addr+2<=base+sizeof(h->singer[g].grains[1])){unsigned local=(unsigned)(addr-base);assert(local/16>=used||local%16>=4);owner=1;}}
  assert(owner);mask[off]=mask[off+1]=1;int16_t v=(int16_t)((int)i*37-8000);memcpy(p,&v,2);
 }
 for(unsigned i=0;i<2*H_LOOP_FADE;i++){int16_t v;memcpy(&v,harmony_loop_pcm(h,i),2);assert(v==(int16_t)((int)i*37-8000));}
 for(unsigned i=0;i<sizeof(*h);i++)if(!mask[i])assert(((unsigned char*)h)[i]==saved[i]);memcpy(h,saved,sizeof(*h));free(saved);
 int max_error=0;
 for(unsigned w=1;w<=H_LOOP_FADE;w++){int prev=0;for(unsigned n=0;n<=w;n++){int q=harmony_loop_weight(n,w);assert(q>=prev&&q>=0&&q<=32768);assert(n||q==0);if(n==w)assert(q==32768);int value=vs_shift0(-32768*(32768-q)+32767*q,15),ref=blend_reference(-32768,32767,n,w),err=abs(value-ref);if(err>max_error)max_error=err;assert(err<=8);prev=q;}}
 int peak=0,last=0;for(unsigned n=1;n<=H_LOOP_FADE;n++){int q=harmony_loop_weight(n,H_LOOP_FADE);if(q-last>peak)peak=q-last;last=q;}
 printf("440 cache samples preserve every live/filter field; raised-cosine max ideal error %d PCM units; first/last gain steps %d/%d, peak %d (old linear 256).\n",max_error,harmony_loop_weight(1,H_LOOP_FADE),32768-harmony_loop_weight(H_LOOP_FADE-1,H_LOOP_FADE),peak);
}
static void rearm(unsigned position,unsigned off_blocks,int tap){
 track_t*t=init();for(unsigned b=0;b<2;b++)vtrim_table[0][0][b].region.edge[2]=8193;t->p[P_E6]=0;trk_note_on(t,60,100);if(tap)trk_note_off(t,60);tick(t);harmony_part_t*h=harm(t);
 while(h->decoder.ph[0]<position)tick(t);if(!tap)trk_note_off(t,60);for(unsigned b=0;b<off_blocks;b++)tick(t);assert(h->decoder.ph[0]>=position&&h->decoder.ph[0]<8193);
 unsigned age=h->source.samples,pos=h->decoder.ph[0];trk_note_on(t,67,100);assert(h->source.samples==age&&h->decoder.ph[0]==pos);held_region(t);trk_note_off(t,67);while(voices_busy())tick(t);
 printf("Held key re-arms region: position %u, released blocks %u, initial tap %d.\n",position,off_blocks,tap);
}
static void continue_ending(unsigned position,unsigned wraps_wanted,int old_loop){
 track_t*t=init();t->p[P_E6]=old_loop;trk_note_on(t,60,100);harmony_part_t*h=harm(t);unsigned prev=0,wraps=0;
 for(unsigned b=0;b<100000;b++){tick(t);unsigned p=h->decoder.ph[0];if(p<prev)wraps++;prev=p;if(wraps>=wraps_wanted&&p>=position)break;assert(b<99999);}
 trk_note_off(t,60);tick(t);assert(!t->nmono);while(h->decoder.ph[0]<10000)tick(t);unsigned age=h->source.samples,pos=h->decoder.ph[0];vs_note saved[5];memcpy(saved,h->singer,sizeof saved);int env[5];for(unsigned k=0;k<5;k++)env[k]=t->v[k].env_out;
 trk_note_on(t,67,100);assert(h->source.samples==age&&h->decoder.ph[0]==pos&&!memcmp(saved,h->singer,sizeof saved));for(unsigned k=0;k<5;k++)assert(t->v[k].note==67&&t->v[k].env_out==env[k]);
 unsigned released=0;while(!h->done){tick(t);assert(h->decoder.ph[0]>=pos);pos=h->decoder.ph[0];assert(h->source.samples>age);age=h->source.samples;if(!released&&pos>10000){trk_note_off(t,67);released=1;}}
 assert(pos==24000);for(unsigned b=0;b<10000&&voices_busy();b++)tick(t);assert(!voices_busy());trk_note_off(t,67);
 trk_note_on(t,72,100);tick(t);assert(h->source.samples<1024&&h->decoder.ph[0]<1024&&voices_busy()==5);trk_note_off(t,72);for(unsigned b=0;b<10000&&voices_busy();b++)tick(t);assert(!voices_busy());
 printf("Ending continues at new pitch without a restart/wrap: release %u, prior wraps %u, legacy LOOP %d.\n",position,wraps_wanted,old_loop);
}
static void drain_and_retrig(void){
 track_t*t=init();trk_note_on(t,60,100);for(unsigned b=0;b<10;b++)tick(t);trk_note_off(t,60);harmony_part_t*h=harm(t);while(!h->done)tick(t);assert(harmony_live(t));unsigned age=h->source.samples,deadline=h->done;trk_note_on(t,67,100);assert(h->source.samples==age&&h->done==deadline);tick(t);assert(!h->done&&h->decoder.ph[0]<1024);held_region(t);trk_note_off(t,67);while(voices_busy())tick(t);
 t=init();t->p[P_VOICE]=V_MONO;trk_note_on(t,60,100);for(unsigned b=0;b<500;b++)tick(t);trk_note_off(t,60);for(unsigned b=0;b<10;b++)tick(t);assert(harm(t)->source.samples>1024);trk_note_on(t,67,100);tick(t);assert(harm(t)->source.samples<1024);trk_note_off(t,67);while(voices_busy())tick(t);
 t=init();trk_note_on(t,60,100);trk_note_off(t,60);tick(t);h=harm(t);assert(h->running);age=h->source.samples;trk_note_on(t,67,100);assert(h->source.samples==age);held_region(t);trk_note_off(t,67);while(voices_busy())tick(t);
 puts("Post-END DSP drain remains retunable; RETRIG restarts; a new held key during an ending restarts only at SAMPLE END.");
}
int main(int argc,char**argv){assert(argc==2);fixture(argv[1]);cache_and_curve();rearm(1000,1,0);rearm(5500,1,0);rearm(8064,1,0);rearm(8192,0,0);rearm(8192,0,1);for(int old=0;old<2;old++){continue_ending(1000,0,old);continue_ending(8130,2,old);}drain_and_retrig();seam(0);seam(1001);seam(4096);end_boundary();scenario(1000,0);scenario(5500,0);scenario(8130,0);scenario(8180,0);scenario(5500,4);
 /* Releasing a non-selected key keeps sustain; releasing selected falls back. */
 track_t*t=init();trk_note_on(t,60,100);trk_note_on(t,67,100);for(unsigned b=0;b<1500;b++)tick(t);unsigned age=harm(t)->source.samples;trk_note_off(t,60);for(unsigned b=0;b<1500;b++)tick(t);assert(t->nmono==1&&voices_busy()==5&&!harm(t)->done&&harm(t)->source.samples>age);trk_note_on(t,60,100);trk_note_off(t,60);assert(t->mono_note==67&&t->nmono==1);trk_note_off(t,67);for(unsigned b=0;b<10000&&voices_busy();b++)tick(t);assert(!voices_busy());puts("Legato held-key/fallback sustain and final-key outro passed.");
 return 0;}
