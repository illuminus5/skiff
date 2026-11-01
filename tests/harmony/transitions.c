/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual controller/mixer: legato, fallback, retrigger and HUMAN=0 transitions. */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
static int32_t out[CTL*2];
static void blocks(unsigned n){while(n--)mix_block(out,CTL);}
int main(int argc,char**argv){assert(argc==2);fixture(argv[1]);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_VOICE]=V_LEGATO;t->p[P_GLIDE]=0;
 for(unsigned human=0;human<=64;human+=64){t->p[P_E5]=human;trk_note_on(t,60,100);blocks(600);harmony_part_t*h=harm(t);unsigned age=h->source.samples,cursor=h->decoder.ph[0];vs_note saved[5];memcpy(saved,h->singer,sizeof saved);trk_note_on(t,67,100);assert(h->source.samples==age&&h->decoder.ph[0]==cursor&&!memcmp(saved,h->singer,sizeof saved));
 for(unsigned k=0;k<5;k++){assert(t->v[k].s[5]==0);assert(t->v[k].pitch_cur==67*16);}blocks(1);assert(h->source.samples>age);for(unsigned k=0;k<5;k++)assert(t->v[k].s[5]==0);blocks(20);
 trk_note_off(t,67);for(unsigned k=0;k<5;k++){assert(t->v[k].s[5]==0);assert(t->v[k].pitch_cur==60*16);}blocks(20);
 /* Releasing a non-selected key must not rebuild a section already stolen. */
 trk_note_on(t,67,100);blocks(20);voice_kill(&t->v[2]);blocks(2);unsigned busy=voices_busy();assert(busy==4);trk_note_off(t,60);assert(voices_busy()==busy);for(unsigned k=0;k<5;k++)if(t->v[k].active)assert(t->v[k].note==67);
 trk_note_off(t,67);blocks(2);age=h->source.samples;cursor=h->decoder.ph[0];trk_note_on(t,72,100);assert(h->source.samples==age&&h->decoder.ph[0]==cursor);for(unsigned k=0;k<5;k++)assert(t->v[k].note==72&&t->v[k].stage!=4u);assert(voices_busy()==5);blocks(600);trk_note_off(t,72);blocks(6000);assert(!voices_busy());}
 puts("Legato/fallback preserve all existing grains and the source timeline without frozen-sample fades; immediate target pitch and retrigger release-tail fade passes at HUMAN 0/64.");return 0;}
