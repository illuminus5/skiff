/* SPDX-License-Identifier: GPL-3.0-only */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
int main(int argc,char**argv){assert(argc==2);fixture(argv[1]);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_E5]=127;t->p[P_VOICE]=V_LEGATO;t->p[P_LEVEL]=127;t->p[P_CHOR]=t->p[P_REV]=t->p[P_DLY]=0;
 const smp_zone_t*z=&usr_zone[0][0];vtrim_data d={0};d.region=(vocal_trim_region){{0,4096,8192,24000},z->n};voice_t dec={0};while(dec.ph[0]<4096)sample_next(z,&dec,0);d.state[1]=(uint16_t)dec.s[0]|((uint32_t)dec.s[1]<<16);vtrim_table[0][0][0]=vtrim_table[0][0][1]=d;vtrim_active[0][0]=0;
 if(getenv("H_FEATURES")){t->p[H_VOICING]=1;t->p[H_CHORD]=3;t->p[H_TUNE]=atoi(getenv("H_FEATURES"));}
 trk_note_on(t,60,127);int32_t out[CTL*2];unsigned peak=0;uint64_t start=instructions();unsigned blocks=FS*6/CTL;for(unsigned b=0;b<blocks;b++){mix_block(out,CTL);assert(voices_busy()==5);for(unsigned i=0;i<CTL*2;i++){int v=out[i]<0?-out[i]:out[i];if(v>(int)peak)peak=v;assert(v<32767);}}
 uint64_t count=instructions()-start;printf("instructions=%llu per_sample=%.3f peak=%u dropped_grains=%u workspace=%zu\\n",(unsigned long long)count,(double)count/(blocks*CTL),peak,harm(t)->source.dropped_grains,sizeof(harmony_part_t));assert(!harm(t)->source.dropped_grains);return 0;}
