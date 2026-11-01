/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "tape_store.h"
static uint32_t random_state=3;
static uint32_t random_word(void){return random_state=random_state*1664525u+1013904223u;}
static uint32_t event(unsigned action){return action|((random_word()%16)<<2)|((random_word()%5)<<6)|((random_word()%801)<<9)|((random_word()%101)<<19)|((random_word()%25)<<26);}
int main(void){
    struct {uint8_t pre[16],bytes[TAPE_STORE_BYTES],post[16];} guarded;
    memset(&guarded,0xA5,sizeof guarded);
    tape_store_state_t s,t;tape_store_defaults(&s);
    assert(tape_store_encode(guarded.bytes,&s));for(unsigned i=0;i<TAPE_STORE_BYTES;i++)assert(!guarded.bytes[i]);
    assert(tape_store_decode(&t,guarded.bytes)==0&&t.live==TAPE_STORE_LIVE_DEFAULT);
    for(unsigned iteration=0;iteration<10000;iteration++){
        tape_store_defaults(&s);s.length=(uint8_t)(16*(1+random_word()%4));
        for(unsigned i=0;i<6;i++)s.sends[i]=(uint8_t)(random_word()%101);
        s.filter=(random_word()%128)|((random_word()%128)<<7)|((random_word()%101)<<14)|((random_word()%101)<<21);
        s.lfo=(random_word()%136)|((random_word()%5)<<8)|((random_word()%101)<<11)|((random_word()%6)<<18);
        s.live=event(2)&~(15u<<2);
        for(unsigned i=0;i<64;i++)s.steps[i]=event(i%4);
        assert(tape_store_encode(guarded.bytes,&s));assert(tape_store_decode(&t,guarded.bytes)==1);
        assert(!memcmp(s.steps,t.steps,sizeof s.steps)&&s.live==t.live&&s.filter==t.filter&&s.lfo==t.lfo&&s.length==t.length&&!memcmp(s.sends,t.sends,6));
        for(unsigned i=0;i<16;i++)assert(guarded.pre[i]==0xA5&&guarded.post[i]==0xA5);
    }
    s.sends[0]=101;assert(!tape_store_encode(guarded.bytes,&s));s.sends[0]=0;
    s.steps[63]=0x80000000u;assert(!tape_store_encode(guarded.bytes,&s));s.steps[63]=0;
    s.length=17;assert(!tape_store_encode(guarded.bytes,&s));s.length=64;
    assert(tape_store_encode(guarded.bytes,&s));t=s;guarded.bytes[2]='2';assert(tape_store_decode(&t,guarded.bytes)<0&&!memcmp(&s,&t,sizeof s));
    guarded.bytes[2]='1';guarded.bytes[TAPE_STORE_BYTES-1]|=0xc0;assert(tape_store_decode(&t,guarded.bytes)<0);
    for(unsigned iteration=0;iteration<20000;iteration++){
        for(unsigned i=0;i<TAPE_STORE_BYTES;i++)guarded.bytes[i]=(uint8_t)(random_word()>>16);
        guarded.bytes[0]='T';guarded.bytes[1]='P';guarded.bytes[2]='1';
        int result=tape_store_decode(&t,guarded.bytes);assert(result<0||tape_store_state_ok(&t));
    }
    puts("TP1: exact 266-byte bounds, 64 lossless event words, controls/sends, 10000 round trips, malformed inputs and default compatibility passed");return 0;
}
