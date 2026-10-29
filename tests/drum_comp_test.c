/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
static int dc_test_prog(uint32_t,const void *,uint32_t);
#define ST_TEST_PROG(off,src,n) dc_test_prog(off,src,n)
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
#include <sys/wait.h>
#include <unistd.h>
static int cut_after=-1;
static int dc_test_prog(uint32_t off,const void *src,uint32_t n){
 if(cut_after==0){memcpy(nor+off,src,n/2);return -9;}
 if(cut_after>0)--cut_after;memcpy(nor+off,src,n);return 0;
}
static void block(uint32_t lane,int32_t x,int32_t *b){
 for(unsigned i=0;i<CTL;i++)b[i]=x;drum_comp_state[0].clock+=CTL;dc_process(&trk[0],lane,b,CTL);
}
#define FRAMES (100u*CTL)
static void render(unsigned engine,unsigned enabled,unsigned level,int32_t *out){
 int fd[2];assert(!pipe(fd));pid_t pid=fork();assert(pid>=0);
 if(!pid){
  close(fd[0]);host_tracks_init();for(unsigned k=0;k<6;k++)dc_defaults(k);
  track_t *t=&trk[0];host_preset(t,engine==100?ENGI_DRUM:engine,0);
  if(engine==100){
   memset(host_samples[0],0,sizeof host_samples[0]);smp_user_hdr_t *h=(smp_user_hdr_t *)host_samples[0];
   h->magic=SMP_USER_MAGIC;h->version=1;h->nz=2;h->data_len=4096;
   for(unsigned z=0;z<2;z++){h->zone[z].off=z*2048;h->zone[z].n=4096;h->zone[z].le=4095;h->zone[z].rate=65536;h->zone[z].lo=h->zone[z].hi=(uint8_t)(z?42:36);h->zone[z].root16=(int16_t)((z?42:36)*16);h->zone[z].pred=10000;h->zone[z].idx=32;}
   for(unsigned i=0;i<4096;i++)host_samples[0][SMP_USER_DATA+i]=(uint8_t)((i%16<8)?0x77:0xff);
   smp_user_scan(0);assert(usr_nz[0]==2);t->p[P_E0]=DK_USR1;
  }
t->p[P_DIST]=t->p[P_CHOR]=t->p[P_REV]=t->p[P_DLY]=t->p[P_ITYPE]=0;
  t->p[P_E7]=0;t->p[P_LPAN0]=-64;t->p[P_LPAN3]=63;t->p[P_LN0]=(int16_t)level;
  usb_six_put(0,4);usb_six_put(1,5);tape_send[0]=100;tape_wanted=1;
  if(enabled){dc_set(0,0,0,32);dc_set(0,0,1,enabled==2?0:64);dc_set(0,0,3,127);}
  trk_note_on(t,36,110);trk_note_on(t,42,100);
  int32_t o[2*CTL],uout[4*FRAMES];
  for(unsigned b=0;b<100;b++){mix_block(o,CTL);const int32_t *u=usb_route_finish(o,CTL);
   for(unsigned i=0;i<CTL;i++){uout[4*(b*CTL+i)]=u[i*UAC_CHANNELS];uout[4*(b*CTL+i)+1]=u[i*UAC_CHANNELS+1];uout[4*(b*CTL+i)+2]=tape_input[0].l[i]+tape_input[1].l[i];uout[4*(b*CTL+i)+3]=tape_input[0].r[i]+tape_input[1].r[i];}}
  const uint8_t *p=(const uint8_t *)uout;size_t remain=sizeof uout;while(remain){ssize_t n=write(fd[1],p,remain);if(n<=0)_exit(2);p+=n;remain-=(size_t)n;}
  close(fd[1]);_exit(0);
 }
 close(fd[1]);uint8_t *p=(uint8_t *)out;size_t remain=sizeof(int32_t)*4*FRAMES;
 while(remain){ssize_t n=read(fd[0],p,remain);assert(n>0);p+=n;remain-=(size_t)n;}close(fd[0]);int status;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
static void outer_hash(project_store_t *p){uint32_t h=proj_hash(p->raw,PROJ_STORE5-4);memcpy(p->raw+PROJ_STORE5-4,&h,4);}
int main(void){
 reset();for(unsigned k=0;k<6;k++)dc_defaults(k);
 assert(dc_get(0,0,0)==64&&dc_get(0,0,1)==64&&dc_get(0,0,2)==24&&!dc_get(0,0,3));
 int32_t b[CTL];block(0,20000,b);assert(b[0]==20000&&!drum_comp_state[0].lane[0].wet);
 dc_set(0,0,3,127);for(unsigned i=0;i<100;i++)block(0,20000,b);assert(b[CTL-1]<6000&&b[CTL-1]>1000);
 block(1,20000,b);assert(b[0]==20000&&!drum_comp_state[0].lane[1].wet&&!drum_comp_state[1].lane[0].wet);
 int32_t old=drum_comp_state[0].lane[0].gain;drum_comp_state[0].clock+=44100*10;block(0,20000,b);assert(drum_comp_state[0].lane[0].gain>old);
 dc_set(0,0,3,0);for(unsigned i=0;i<10;i++)block(0,20000,b);assert(b[0]==20000&&!drum_comp_state[0].lane[0].wet);
 static int32_t dry[4*FRAMES],wet[4*FRAMES];
 const unsigned engines[]={ENGI_DRUM,ENGI_DRUMX,100};
 for(unsigned ei=0;ei<3;ei++){unsigned engine=engines[ei];
  render(engine,0,127,dry);render(engine,1,127,wet);int64_t dl=0,wl=0;unsigned right=0;
  for(unsigned i=0;i<FRAMES;i++){dl+=llabs(dry[4*i]);wl+=llabs(wet[4*i]);assert(abs(dry[4*i+1]-wet[4*i+1])<=8&&dry[4*i+3]==wet[4*i+3]);right|=dry[4*i+1]!=0;}
  if(!(dl>1000&&wl<dl*9/10&&right))fprintf(stderr,"engine %u dry %lld wet %lld right %u\n",engine,(long long)dl,(long long)wl,right);
  assert(dl>1000&&wl<dl*9/10&&right);
  render(engine,2,64,wet);render(engine,0,64,dry);
  int32_t maxdiff=0;for(unsigned i=0;i<4*FRAMES;i++){int32_t diff=abs(wet[i]-dry[i]);if(diff>maxdiff)maxdiff=diff;}assert(maxdiff<32);
 }
 reset();for(unsigned k=0;k<6;k++)dc_defaults(k);
 for(unsigned i=0xa0000;i<0xdc000;i++)nor[i]=(uint8_t)(i*37u+11u);
 uint32_t samples=st_crc32(nor+0xa0000,0x3c000);
 dc_set(0,0,0,37);dc_set(0,0,1,80);dc_set(0,0,2,77);dc_set(0,0,3,100);dc_set(5,7,3,127);
 tape_steps[63]=ts_pack(TS_TRIGGER,12,4,-175,81,-3);tape_send[0]=52;tape_filter=27u|(90u<<7)|(20u<<14)|(100u<<21);
 uint8_t expected[sizeof drum_comp];memcpy(expected,drum_comp,sizeof expected);uint32_t tape=tape_steps[63];
 assert(project_save(0)==0);project_store_t saved;assert(st_load(OBJ_PROJECT0,&saved,sizeof saved)==PROJ_STORE5&&saved.align==PROJ_MAGIC5);
 assert(st_crc32(nor+0xa0000,0x3c000)==samples);
 project_new();assert(!dc_get(0,0,3));project_load(0);assert(!memcmp(expected,drum_comp,sizeof expected)&&tape_steps[63]==tape&&tape_send[0]==52);
 memset(proj_slot,0,sizeof proj_slot);proj_fetch(0);project_load(0);assert(!memcmp(expected,drum_comp,sizeof expected));
 uint32_t len,crc;assert(!list(2,&len,&crc)&&len==PROJ_STORE5);uint8_t archive[PROJ_STORE5];assert(!get_all(2,archive,sizeof archive)&&!memcmp(archive,&saved,PROJ_STORE5));
 assert(!put_all(4,archive,sizeof archive,st_crc32(archive,sizeof archive)));project_new();project_load(2);assert(!memcmp(expected,drum_comp,sizeof expected));
 project_store_t malformed=saved;malformed.raw[8+PROJ_STORE4]=128;outer_hash(&malformed);unsigned before=erases;uint32_t sig=autosave_sig();
 assert(put_all(4,&malformed,PROJ_STORE5,st_crc32(&malformed,PROJ_STORE5))==2&&erases==before&&autosave_sig()==sig);
 malformed=saved;malformed.raw[20]^=1;outer_hash(&malformed);assert(put_all(4,&malformed,PROJ_STORE5,st_crc32(&malformed,PROJ_STORE5))==2&&erases==before);
 malformed=saved;malformed.raw[8+PROJ_STORE4+DC_TRACKS*DC_BYTES]=1;outer_hash(&malformed);
 assert(put_all(4,&malformed,PROJ_STORE5,st_crc32(&malformed,PROJ_STORE5))==2&&erases==before);
 malformed=saved;malformed.raw[PROJ_NAME_AT(&PROJ_SKF5)]^=1;outer_hash(&malformed);
 assert(put_all(4,&malformed,PROJ_STORE5,st_crc32(&malformed,PROJ_STORE5))==2&&erases==before);
 project_t p;project_capture(&p);project_store_t normalized;p.name[0]=1;p.name[1]=0;assert(proj_pack5(&normalized,&p)&&proj_import(&p,&normalized,PROJ_STORE5));project_store_t oldwire;assert(proj_pack4(&oldwire,&p)&&proj_import(&p,&oldwire,PROJ_STORE4)&&!project_restore_runtime(&p));assert(!dc_get(0,0,3)&&tape_steps[63]==tape);
 memcpy(drum_comp,expected,sizeof expected);assert(!up_store(0,"DRUM COMP"));dc_defaults(0);assert(!up_load(0)&&!memcmp(drum_comp[0],expected,DC_BYTES));
 up_rec_t legacy=*up_rec(0);memset(legacy.sid+DC_UP_AT,0,UP_SIDS-DC_UP_AT);assert(!up_put(1,&legacy));assert(!up_load(1)&&!dc_get(0,0,3));
 memset(up_bank,0,sizeof up_bank);up_boot();dc_defaults(0);assert(!up_load(0)&&!memcmp(drum_comp[0],expected,DC_BYTES));
 legacy=*up_rec(0);legacy.sid[DC_UP_AT]=128;assert(!up_valid(&legacy));
 memcpy(drum_comp,expected,sizeof expected);for(unsigned k=0;k<6;k++)for(unsigned v=0;v<NVOICE;v++)trk[k].v[v].active=0;
 autosave_boot(0);unsigned writes=as.writes;dc_set(0,1,3,99);for(unsigned i=0;i<100;i++){fm1_ms+=100;autosave_poll();}assert(as.writes==writes+1);
 uint32_t saved_sig=autosave_sig();block(0,20000,b);assert(autosave_sig()==saved_sig);dc_defaults(0);assert(autosave_boot(1)==1&&dc_get(0,1,3)==99);
 assert(!project_save(0));static uint8_t baseline[sizeof nor];memcpy(baseline,nor,sizeof nor);assert(st_load(OBJ_PROJECT0,&saved,sizeof saved)==PROJ_STORE5);
 dc_set(0,0,0,79);project_capture(&p);project_store_t next;assert(proj_pack5(&next,&p));
 for(unsigned page=0;page<=11;page++){
  memcpy(nor,baseline,sizeof nor);cut_after=(int)page;assert(st_save(OBJ_PROJECT0,&next,PROJ_STORE5)!=0);cut_after=-1;
  project_store_t got;assert(st_load(OBJ_PROJECT0,&got,sizeof got)==PROJ_STORE5&&!memcmp(&got,&saved,PROJ_STORE5));assert(proj_import(&p,&got,PROJ_STORE5));
 }
 assert(st_crc32(nor+0xa0000,0x3c000)==samples);
 puts("DRUM COMP: defaults/bypass, independent lane/track state, silent-gap release, DRUM/DRUM-X kick-only compression with unchanged hats, lane LEVEL law, SKF4 migration/SKF5 SAVE/reboot/backup/autosave, USER preset roundtrip, malformed rejection, sample preservation and 12 interrupted writes passed");return 0;
}
