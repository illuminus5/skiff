/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
static int tape_test_prog(uint32_t,const void *,uint32_t);
#define ST_TEST_PROG(off,src,n) tape_test_prog(off,src,n)
#define BACKUP_TEST_NO_MAIN 1
#include "backup_test.c"
static int cut_after=-1;
static int tape_test_prog(uint32_t off,const void *src,uint32_t n){
    if(cut_after==0){memcpy(nor+off,src,n/2);return -9;}
    if(cut_after>0)--cut_after;memcpy(nor+off,src,n);return 0;
}
static void configure(unsigned seed){
    tape_seq_length=64;
    for(unsigned i=0;i<64;i++)tape_steps[i]=ts_pack(i%4,i%16,i%5,(int32_t)((i*13+seed)%801)-400,(i*7)%101,(int32_t)(i%25)-12);
    for(unsigned i=0;i<6;i++)tape_send[i]=(seed+i*17)%101;
    tape_filter=23u|(80u<<7)|(55u<<14)|(89u<<21);tape_lfo=132u|(1u<<8)|(60u<<11)|(TF_LP<<18);
    tape_live=ts_pack(TS_HOLD,5,4,-175,81,-3);
}
static void matches(const uint8_t *expected){uint8_t got[TAPE_STORE_BYTES];skiff_tape_store(got);assert(!memcmp(got,expected,sizeof got));}
static void rehash(project_store_t *p){uint32_t h=proj_hash(p->raw,PROJ_STORE4-4);memcpy(p->raw+PROJ_STORE4-4,&h,4);}
int main(void){
    reset();skiff_tape_project_reset();
    for(unsigned i=0x9f000;i<0xe0000;i++)nor[i]=(uint8_t)(i*37u+11u);
    uint32_t protected_crc=st_crc32(nor+0x9f000,0x41000);
    configure(7);uint8_t expected[TAPE_STORE_BYTES];skiff_tape_store(expected);
    assert(tape_init(&tape_state,&sl_buf[0][0],sizeof sl_buf));tape_state.enabled=1;sl_lent=1;tape_wanted=tape_seq_on=1;
    for(unsigned i=0;i<2000;i++)tt_capture(&tape_state,1000,-1000);
    assert(project_save(0)==0);project_store_t saved;
    assert(st_load(OBJ_PROJECT0,&saved,sizeof saved)==PROJ_STORE4&&PROJ_STORE4==2560);
    assert(!memcmp(saved.raw+PROJ_TAPE_AT,expected,sizeof expected));
    assert(st_crc32(nor+0x9f000,0x41000)==protected_crc);
    configure(31);project_new();project_load(0);matches(expected);
    assert(!tape_wanted&&!tape_seq_on&&!tape_state.enabled&&!tape_state.valid_blocks&&!sl_lent);
    assert(tape_live==ts_pack(TS_HOLD,0,4,-175,81,-3));
    memset(proj_slot,0,sizeof proj_slot);skiff_tape_project_reset();proj_fetch(0);project_load(0);matches(expected);
    uint32_t len=0,crc=0;assert(list(2,&len,&crc)==0&&len==PROJ_STORE4);
    uint8_t archive[PROJ_STORE4];assert(get_all(2,archive,sizeof archive)==0&&!memcmp(archive,&saved,PROJ_STORE4));
    assert(put_all(4,archive,sizeof archive,st_crc32(archive,sizeof archive))==0);project_new();project_load(2);matches(expected);
    project_store_t malformed=saved;malformed.raw[PROJ_TAPE_AT+2]='2';rehash(&malformed);
    unsigned before=erases;uint32_t sig=autosave_sig();
    assert(put_all(4,&malformed,PROJ_STORE4,st_crc32(&malformed,PROJ_STORE4))==2&&erases==before&&autosave_sig()==sig);
    project_t decoded;project_store_t old=saved;memset(old.raw+PROJ_TAPE_AT,0,TAPE_STORE_BYTES);rehash(&old);
    assert(proj_import(&decoded,&old,sizeof old)&&project_restore_runtime(&decoded)==0);
    assert(!tape_steps[0]&&!tape_send[5]&&tape_filter==TF_FILTER_DEFAULT&&tape_lfo==TF_LFO_DEFAULT&&tape_live==TAPE_STORE_LIVE_DEFAULT);
    for(unsigned k=0;k<NTRK;k++)for(unsigned v=0;v<NVOICE;v++)trk[k].v[v].active=0;
    (void)autosave_boot(0);unsigned writes=as.writes;tape_filter=47u|(90u<<7)|(20u<<14)|(100u<<21);
    for(unsigned i=0;i<100;i++){fm1_ms+=100;autosave_poll();}
    assert(as.writes==writes+1);skiff_tape_store(expected);sig=autosave_sig();
    tape_live^=1u<<2;assert(autosave_sig()==sig);tape_live^=1u<<2;
    for(unsigned i=0;i<1000;i++){tf_prepare(22050,1,i*CTL);for(unsigned j=0;j<CTL;j++)tf_tick();tape_fx_phase=tape_fx.phase;assert(autosave_sig()==sig);}
    for(unsigned i=0;i<300;i++){fm1_ms+=100;autosave_poll();}assert(as.writes==writes+1);
    skiff_tape_project_reset();assert(autosave_boot(1)==1);matches(expected);assert(!tape_wanted&&!tape_seq_on);
    assert(st_crc32(nor+0x9f000,0x41000)==protected_crc);
    configure(7);assert(project_save(0)==0);assert(st_load(OBJ_PROJECT0,&saved,sizeof saved)==PROJ_STORE4);
    static uint8_t baseline[sizeof nor];memcpy(baseline,nor,sizeof nor);
    configure(55);project_capture(&decoded);project_store_t next;assert(proj_pack4(&next,&decoded));
    for(unsigned page=0;page<=10;page++){
        memcpy(nor,baseline,sizeof nor);cut_after=(int)page;assert(st_save(OBJ_PROJECT0,&next,PROJ_STORE4)!=0);cut_after=-1;
        project_store_t got;assert(st_load(OBJ_PROJECT0,&got,sizeof got)==PROJ_STORE4&&!memcmp(&got,&saved,PROJ_STORE4));
        assert(proj_import(&decoded,&got,sizeof got)&&st_crc32(nor+0x9f000,0x41000)==protected_crc);
    }
    memcpy(nor,baseline,sizeof nor);assert(st_save(OBJ_PROJECT0,&next,PROJ_STORE4)==0);
    assert(st_load(OBJ_PROJECT0,&saved,sizeof saved)==PROJ_STORE4&&!memcmp(&next,&saved,PROJ_STORE4));
    puts("Tape projects: SAVE/load/reboot/backup/autosave, malformed rejection, disarmed restore, sample/preset preservation and all 11 torn-write points passed");return 0;
}
