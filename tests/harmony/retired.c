/* SPDX-License-Identifier: GPL-3.0-only */
#define VOCAL_TEST_NO_MAIN 1
#include "support.h"
int main(int argc,char**argv){assert(argc==2);fixture(argv[1]);memset(trk,0,sizeof trk);memset(&song,0,sizeof song);host_tracks_init();track_t*t=&trk[0];host_preset(t,ENGI_HARMONY,0);harmony_defaults(t->p);t->p[P_E0]=SMP_NSETS;t->p[P_VOICE]=V_LEGATO;t->p[P_CHOR]=t->p[P_REV]=t->p[P_DLY]=0;
 assert(ENGI_HARMONY==16&&ENGI_VOCAL==15&&NENGINES==17&&NENG_SHOWN==15);assert(!eng_ok(15)&&!eng_allowed(0,15));assert(ENGINES[15]->npresets==0&&ENGINES[15]->block==0);assert(!strcmp(ENGINES[5]->name,"VOICE"));
 t->engine=t->eng_req=15;for(unsigned i=24;i<=96;i+=12){trk_note_on(t,i,127);assert(!voices_busy()&&!t->nmono);trk_note_off(t,i);}int32_t out[CTL*2];for(unsigned b=0;b<100;b++){mix_block(out,CTL);for(unsigned i=0;i<CTL*2;i++)assert(!out[i]);}assert(!voices_busy());
 t->eng_req=16;trk_note_on(t,60,100);assert(t->xp_n==1&&!voices_busy());engine_block(t);assert(t->engine==16&&voices_busy()==5&&t->xp_n==0);for(unsigned b=0;b<100;b++)mix_block(out,CTL);assert(harm(t)->source.samples>0);puts("Retired ID silent with zero voices; pending-key transition into HARMONY works; VOICE and stable IDs preserved.");return 0;}
