/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual engine/mixer/allocator, simulated user flash; no direct DSP bypass. */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
int main(int argc,char**argv){assert(argc==3);
#ifdef VOCAL_C1_TEST
 c1h_off=getenv("C1_OFF")!=0;
#endif
 fixture(argv[1]);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_E2]=12;t->p[P_VOICE]=V_LEGATO;t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=t->p[P_DIST]=0;
 unsigned extra=getenv("H_ACCOMP")?(VOICE_BUDGET-5):0,expected=5+extra;
 if(extra){track_t*a=&trk[1];host_preset(a,ENGI_SAMPLE,0);a->p[P_E0]=SMP_NSETS;a->p[P_E1]=0;a->p[P_E2]=0;a->p[P_E3]=1;a->p[P_E4]=127;a->p[P_LEVEL]=30;a->p[P_VOICE]=V_POLY;a->p[P_REL]=24;a->p[P_CHOR]=a->p[P_DLY]=a->p[P_REV]=0;for(unsigned j=0;j<extra;j++)trk_note_on(a,48+j,80);}
 if(getenv("H_ONESHOT"))t->p[P_E6]=0;
 if(getenv("H_MISSING"))t->p[P_E0]=SMP_NSETS+2;
 assert(trk_nvoice(t)==5&&sizeof(harmony_part_t)<=sizeof(eng_mem_t));trk_note_on(t,60,100);assert(voices_busy()==expected);FILE*f=fopen(argv[2],"wb");assert(f);unsigned blocks=(FS*8+CTL-1)/CTL;wav_hdr(f,blocks*CTL);int32_t out[CTL*2];unsigned clips=0,min=expected;uint64_t energy=0;
 for(unsigned b=0;b<blocks;b++){if(!getenv("H_ONESHOT")){if(b==FS*2/CTL)trk_note_on(t,63,100);if(b==FS*3/CTL)trk_note_off(t,63);}if((getenv("H_ONESHOT")&&b==FS/20/CTL)||b==FS*5/CTL){trk_note_off(t,60);for(unsigned j=0;j<extra;j++)trk_note_off(&trk[1],48+j);}mix_block(out,CTL);if(!getenv("H_MISSING")&&b<(getenv("H_ONESHOT")?FS*2/CTL:FS*5/CTL)){unsigned busy=voices_busy();if(busy<min)min=busy;}
 for(unsigned i=0;i<CTL;i++){int l=out[2*i],r=out[2*i+1];if(l>32767||l< -32768||r>32767||r< -32768)clips++;energy+=(int64_t)l*l+(int64_t)r*r;wav_put(f,l,r);}}
 fclose(f);fprintf(stderr,"energy=%llu min=%u expected=%u clips=%u remaining=%u kills=%u\n",(unsigned long long)energy,min,expected,clips,voices_busy(),voice_kills);assert((getenv("H_MISSING")?!energy:(energy&&min==expected))&&!clips&&!voices_busy()&&!voice_kills);printf("HARMONY: five real section voices, legato/fallback/release, no clips/removals. state=%zu workspace=%zu\n",sizeof(harmony_part_t),sizeof(eng_mem_t));return 0;}
