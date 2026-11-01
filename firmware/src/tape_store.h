/* SPDX-License-Identifier: GPL-3.0-only */
/* TP1 fits the existing SKF4 tail: three-byte tag, packed controls and 64
 * lossless 31-bit event words. Audio/transport/cursor are deliberately absent. */
#pragma once
#ifndef TAPE_STORE_NO_SYSTEM_HEADERS
#include <stdint.h>
#include <string.h>
#endif
#define TAPE_STORE_BYTES 266u
#define TAPE_STORE_STEPS 64u
#define TAPE_STORE_FILTER_DEFAULT ((127u<<7)|(100u<<21))
#define TAPE_STORE_LFO_DEFAULT (48u|(1u<<18))
#define TAPE_STORE_LIVE_DEFAULT (2u|(2u<<6)|(500u<<9)|(100u<<19)|(12u<<26))
#define TAPE_STORE_LIVE_MASK 0x7fffffc0u
typedef struct {
    uint32_t steps[TAPE_STORE_STEPS],filter,lfo,live;
    uint8_t sends[6],length;
} tape_store_state_t;
_Static_assert(24u+2u+42u+28u+21u+25u+31u*TAPE_STORE_STEPS<=TAPE_STORE_BYTES*8u,"TP1 size");
static void tape_store_defaults(tape_store_state_t *s){
    memset(s,0,sizeof *s);s->length=16;s->filter=TAPE_STORE_FILTER_DEFAULT;
    s->lfo=TAPE_STORE_LFO_DEFAULT;s->live=TAPE_STORE_LIVE_DEFAULT;
}
static int tape_store_word_ok(uint32_t w){
    return !(w>>31)&&((w>>6)&7u)<=4u&&((w>>9)&1023u)<=800u&&
        ((w>>19)&127u)<=100u&&((w>>26)&31u)<=24u;
}
static int tape_store_state_ok(const tape_store_state_t *s){
    if(s->length<16||s->length>64||s->length%16)return 0;
    if(s->filter>>28||((s->filter>>14)&127)>100||((s->filter>>21)&127)>100)return 0;
    if(s->lfo>>21||(s->lfo&255)>135||((s->lfo>>8)&7)>4||((s->lfo>>11)&127)>100||((s->lfo>>18)&7)>5)return 0;
    if(!tape_store_word_ok(s->live))return 0;
    for(unsigned i=0;i<6;i++)if(s->sends[i]>100)return 0;
    for(unsigned i=0;i<TAPE_STORE_STEPS;i++)if(!tape_store_word_ok(s->steps[i]))return 0;
    return 1;
}
static void tape_store_put(uint8_t *p,uint32_t *bit,uint32_t value,unsigned n){
    for(unsigned i=0;i<n;i++,++*bit)p[*bit>>3]|=(uint8_t)(((value>>i)&1u)<<(*bit&7u));
}
static uint32_t tape_store_get(const uint8_t *p,uint32_t *bit,unsigned n){
    uint32_t value=0;for(unsigned i=0;i<n;i++,++*bit)value|=(uint32_t)((p[*bit>>3]>>(*bit&7u))&1u)<<i;return value;
}
static int tape_store_encode(uint8_t *p,const tape_store_state_t *s){
    if(!tape_store_state_ok(s))return 0;
    memset(p,0,TAPE_STORE_BYTES);
    int any=s->length!=16||s->filter!=TAPE_STORE_FILTER_DEFAULT||s->lfo!=TAPE_STORE_LFO_DEFAULT||
        (s->live&TAPE_STORE_LIVE_MASK)!=(TAPE_STORE_LIVE_DEFAULT&TAPE_STORE_LIVE_MASK);
    for(unsigned i=0;i<6;i++)any|=s->sends[i]!=0;
    for(unsigned i=0;i<TAPE_STORE_STEPS;i++)any|=s->steps[i]!=0;
    if(!any)return 1; /* default projects remain byte-for-byte stock */
    p[0]='T';p[1]='P';p[2]='1';uint32_t bit=24;
    tape_store_put(p,&bit,s->length/16u-1u,2);
    for(unsigned i=0;i<6;i++)tape_store_put(p,&bit,s->sends[i],7);
    tape_store_put(p,&bit,s->filter,28);tape_store_put(p,&bit,s->lfo,21);
    tape_store_put(p,&bit,(s->live>>26)&31,5);tape_store_put(p,&bit,(s->live>>9)&1023,10);
    tape_store_put(p,&bit,(s->live>>6)&7,3);tape_store_put(p,&bit,(s->live>>19)&127,7);
    for(unsigned i=0;i<TAPE_STORE_STEPS;i++)tape_store_put(p,&bit,s->steps[i],31);
    return 1;
}
/* 1 known and valid; 0 absent; -1 malformed/unsupported TP record. Output
 * stays untouched on error so callers can validate before changing a project. */
static int tape_store_decode(tape_store_state_t *out,const uint8_t *p){
    if(p[0]!='T'||p[1]!='P'){tape_store_defaults(out);return 0;}
    if(p[2]!='1'||(p[TAPE_STORE_BYTES-1]&0xc0u))return -1;
    tape_store_state_t s;tape_store_defaults(&s);uint32_t bit=24;
    s.length=(uint8_t)((tape_store_get(p,&bit,2)+1u)*16u);
    for(unsigned i=0;i<6;i++)s.sends[i]=(uint8_t)tape_store_get(p,&bit,7);
    s.filter=tape_store_get(p,&bit,28);s.lfo=tape_store_get(p,&bit,21);
    s.live=2u;s.live|=tape_store_get(p,&bit,5)<<26;s.live|=tape_store_get(p,&bit,10)<<9;
    s.live|=tape_store_get(p,&bit,3)<<6;s.live|=tape_store_get(p,&bit,7)<<19;
    for(unsigned i=0;i<TAPE_STORE_STEPS;i++)s.steps[i]=tape_store_get(p,&bit,31);
    if(!tape_store_state_ok(&s))return -1;
    *out=s;return 1;
}
