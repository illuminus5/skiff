/* SPDX-License-Identifier: GPL-3.0-only */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
int main(int argc,char**argv){assert(argc==4);fixture(argv[1]);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_E2]=0;t->p[P_VOICE]=V_LEGATO;t->p[P_LEVEL]=127;t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=0;
 assert(harmony_source(SMP_NSETS));assert(!harmony_source(0));assert(!harmony_source(SMP_NSETS+1));
 unsigned oldlo=usr_zone[0][0].lo,oldhi=usr_zone[0][0].hi;usr_zone[0][0].lo=usr_zone[0][0].hi=60;assert(!harmony_source(SMP_NSETS));usr_zone[0][0].lo=oldlo;usr_zone[0][0].hi=oldhi;
 t->p[H_VOICING]=1;
 for(unsigned key=0;key<24;key++)for(unsigned chord=0;chord<7;chord++){
 t->p[P_E2]=key;t->p[H_CHORD]=chord;int root=key%12,minor=key>=12,pc=root,tones=0;
 for(unsigned i=0;i<chord;i++)do{pc++;}while(!allowed(pc,root,minor));
 for(unsigned k=0;k<3;k++){tones|=1<<(pc%12);for(unsigned j=0;j<2;j++)do{pc++;}while(!allowed(pc,root,minor));}
 for(unsigned g=1;g<5;g++){int target=-1;for(int lead=48;lead<84;lead++){int n=lead+harmony_offset(t,lead,g);assert((tones>>(n%12))&1);if(target>=0)assert(n==target);target=n;}}
 }
 printf("All 24 keys / seven diatonic chords: section tones hold independently of lead. Source guards pass.\n");
 t->p[P_E2]=0;t->p[H_VOICING]=getenv("H_CHORD_TEST")?1:0;t->p[H_CHORD]=0;t->p[H_TUNE]=atoi(argv[3]);
 if(!getenv("H_CHORD_TEST"))for(unsigned g=1;g<5;g++)t->p[P_LDECAY0+g]=0;
 trk_note_on(t,60,110);FILE*f=fopen(argv[2],"wb");assert(f);unsigned blocks=FS*4/CTL;wav_hdr(f,blocks*CTL);int32_t out[CTL*2];unsigned maxvoices=0,peak=0;
 for(unsigned b=0;b<blocks;b++){if(b==FS*2/CTL){trk_note_on(t,64,110);trk_note_off(t,60);}mix_block(out,CTL);if(voices_busy()>maxvoices)maxvoices=voices_busy();for(unsigned i=0;i<CTL;i++){int l=out[2*i],r=out[2*i+1];assert(abs(l)<32767&&abs(r)<32767);if((unsigned)abs(l)>peak)peak=abs(l);wav_put(f,l,r);}}
 fclose(f);assert(maxvoices==(getenv("H_CHORD_TEST")?5:1));assert(!harm(t)->source.dropped_grains);printf("Mode %s: peak %u, voices %u, grains dropped 0.\n",argv[3],peak,maxvoices);return 0;}
