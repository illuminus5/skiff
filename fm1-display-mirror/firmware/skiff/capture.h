/* SPDX-License-Identifier: GPL-3.0-only
 * Opt-in LCD pixel journal. Main-loop only; no USB, locks, waits or allocation.
 * An entire bounded render transaction is retained until acknowledged.
 * Overflow invalidates it; never publish a truncated transaction. */
#pragma once
#include <stdint.h>
#ifndef FELUCCA_DISPLAY_BULK
#define FELUCCA_DISPLAY_BULK 0
#endif
#ifndef DM_NOW
#define DM_NOW() 0u
#endif
#ifndef DM_CAPACITY
#define DM_CAPACITY 8192u
#endif
#ifndef DM_SEGMENT_ROWS
#define DM_SEGMENT_ROWS (FELUCCA_DISPLAY_BULK ? 120u : 240u)
#endif
#ifndef DM_POOL
#define DM_POOL __attribute__((section(".pool")))
#endif
#ifndef DM_QUEUE
#define DM_QUEUE DM_POOL
#endif
static uint8_t dm_bytes[DM_CAPACITY] DM_QUEUE;
static struct {
    uint32_t n;
    volatile uint32_t ready; /* main producer publishes; bulk ISR consumes */
    uint32_t enabled, capturing, full, need_full;
    volatile uint32_t invalid, chunk_id;
    uint32_t skipped, gen, last_request, pixels, changed, dropped, band, rows, graph_top, graph_height, graph_dirty, footer_y, footer_h, started_ms, ended_ms;
} dm;
#if FELUCCA_DISPLAY_BULK
/* Fingerprints are committed only after their matching pixel transaction is queued.
 * Two independent 32-bit accumulators per four-row strip, no pixel framebuffer. */
#define DM_GRAPH_STRIPS 36u
static struct {uint32_t capture,queue,wire,ack,bytes,gen;} dm_perf;
static uint32_t dm_graph_hash[2][DM_GRAPH_STRIPS][2] DM_POOL;
static struct {uint32_t valid,pending,y,h,next_y,next_h;} dm_graph_cache;
#endif
static void dm_u16(uint8_t *p,uint32_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void dm_u32(uint8_t *p,uint32_t n){dm_u16(p,n);dm_u16(p+2,n>>16);}
static void dm_fail(void){
#if FELUCCA_DISPLAY_BULK
    dm_graph_cache.valid=dm_graph_cache.pending=0;
#endif
    dm.invalid=1;dm.capturing=0;dm.n=dm.ready=0;dm.need_full=1;dm.band=0;dm.dropped++;}
static int dm_room(uint32_t n){if(n>DM_CAPACITY-dm.n){dm_fail();return 0;}return 1;}
static uint32_t dm_record(uint32_t kind,uint32_t codec,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t n){
    uint32_t p=dm.n;if(!dm_room(28u+n))return 0;
    uint8_t *o=dm_bytes+p;o[0]='F';o[1]='M';o[2]='I';o[3]='R';o[4]=1;o[5]=(uint8_t)kind;o[6]=kind==2u?dm.full:0;o[7]=(uint8_t)codec;
    dm_u32(o+8,dm.gen);dm_u16(o+12,240);dm_u16(o+14,240);dm_u16(o+16,x);dm_u16(o+18,y);dm_u16(o+20,w);dm_u16(o+22,h);dm_u32(o+24,n);dm.n+=28u+n;return p+28u;
}
static void dm_start(uint32_t now){
#if FELUCCA_DISPLAY_BULK
    dm_graph_cache.valid=dm_graph_cache.pending=0;
#endif
    dm.n=dm.ready=dm.capturing=dm.invalid=0;dm.enabled=1;dm.need_full=1;dm.last_request=now;dm.skipped=dm.band=dm.graph_dirty=0;dm.rows=DM_SEGMENT_ROWS;}
static void dm_stop(void){dm.enabled=dm.n=dm.ready=dm.capturing=0;}
static uint32_t dm_begin(uint32_t now){
    if(!dm.enabled)return 0;
    if(now-dm.last_request>3000u){dm_stop();return 0;}
    if(dm.ready){dm.capturing=0;return 0;}
    if(dm.invalid)return 0;
#if FELUCCA_DISPLAY_BULK
    dm_graph_cache.pending=0;
#endif
    dm.started_ms=now;
    dm.full=dm.need_full;dm.n=0;dm.changed=dm.pixels=0;dm.capturing=1;
    if(!dm.full || !dm.band){dm.gen=(dm.gen+1u)&16383u;dm_record(2,0,0,0,0,0,0);}
    return dm.full;
}
static int dm_capture(uint32_t x,uint32_t y,uint32_t w,uint32_t h){
    if(!dm.enabled)return 0;
    if(!dm.capturing){if(dm.ready){
        if(dm.graph_height&&x==0u&&w==240u){
            if(y==dm.graph_top&&h==dm.graph_height)dm.graph_dirty|=1u;
            else if(y==0u&&h==24u)dm.graph_dirty|=2u;
            else if(y==dm.footer_y&&h==dm.footer_h)dm.graph_dirty|=4u;
            else dm.skipped=1;
        }
        else dm.skipped=1;
    }return 0;}
    if(!w||!h||x>=240u||y>=240u||w>240u-x||h>240u-y){dm_fail();return 0;}
    /* Pixel and compressed-output bounds are checked by each record writer. */
    dm.changed=1;return 1;
}
static void dm_graph_overwrite(uint32_t y,uint32_t h){
#if FELUCCA_DISPLAY_BULK
    if((dm_graph_cache.valid&&y<dm_graph_cache.y+dm_graph_cache.h&&y+h>dm_graph_cache.y)||
       (dm_graph_cache.pending&&y<dm_graph_cache.next_y+dm_graph_cache.next_h&&y+h>dm_graph_cache.next_y))
        dm_graph_cache.valid=dm_graph_cache.pending=0;
#else
    (void)y;(void)h;
#endif
}
static void dm_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t color){
    dm_graph_overwrite(y,h);
    if(!dm.capturing){(void)dm_capture(x,y,w,h);return;}
    if(dm.full && dm.rows<240u){
        uint32_t top=dm.band*dm.rows,bottom=top+dm.rows;
        if(y>=bottom || y+h<=top)return;
        if(y<top){h-=top-y;y=top;}if(y+h>bottom)h=bottom-y;
    }
    if(!dm_capture(x,y,w,h))return;
    uint32_t p=dm_record(3,2,x,y,w,h,2);if(!p)return;
    dm_bytes[p]=(uint8_t)(color>>8);dm_bytes[p+1]=(uint8_t)color;
}
/* Small LZ4 block encoder: fixed hash table, source is the live LCD strip.
 * No framebuffer allocation. Only backward references inside this strip. */
static uint16_t dm_hash[512] DM_POOL;
static int dm_byte(uint32_t v){if(!dm_room(1))return 0;dm_bytes[dm.n++]=(uint8_t)v;return 1;}
static int dm_length(uint32_t n){while(n>=255u){if(!dm_byte(255))return 0;n-=255;}return dm_byte(n);}
static void dm_blit_raw(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *pixels){
    if(!dm.capturing){(void)dm_capture(x,y,w,h);return;}
    if(dm.full && dm.rows<240u){
        uint32_t top=dm.band*dm.rows,bottom=top+dm.rows;
        if(y>=bottom || y+h<=top)return;
        if(y<top){pixels+=(top-y)*w;h-=top-y;y=top;}if(y+h>bottom)h=bottom-y;
    }
    if(!dm_capture(x,y,w,h))return;
    if(w*h>115200u-dm.pixels){dm_fail();return;}dm.pixels+=w*h;
    uint32_t start=dm_record(3,3,x,y,w,h,0);if(!start)return;
    const uint8_t *src=(const uint8_t *)pixels;uint32_t count=w*h*2u,i=0,anchor=0;
    for(uint32_t k=0;k<512u;k++)dm_hash[k]=0;
    while(i+12u<=count){
        uint32_t word=(uint32_t)src[i]|(uint32_t)src[i+1]<<8|(uint32_t)src[i+2]<<16|(uint32_t)src[i+3]<<24;
        uint32_t hash=(word*2654435761u)>>23;
        uint32_t previous=dm_hash[hash];if(i<65535u)dm_hash[hash]=(uint16_t)(i+1u);
        if(!previous){i++;continue;}uint32_t ref=previous-1u;
        if(i-ref>65535u||src[ref]!=src[i]||src[ref+1]!=src[i+1]||src[ref+2]!=src[i+2]||src[ref+3]!=src[i+3]){i++;continue;}
        uint32_t match=4;while(i+match<count-5u&&src[ref+match]==src[i+match])match++;
        uint32_t literals=i-anchor,token=dm.n;
        if(!dm_byte(((literals<15u?literals:15u)<<4)|((match-4u)<15u?match-4u:15u)))return;
        (void)token;
        if(literals>=15u&&!dm_length(literals-15u))return;
        for(uint32_t k=anchor;k<i;k++)if(!dm_byte(src[k]))return;
        if(!dm_byte(i-ref)||!dm_byte((i-ref)>>8))return;
        if(match>=19u&&!dm_length(match-19u))return;
        i+=match;anchor=i;
    }
    uint32_t literals=count-anchor;
    if(!dm_byte((literals<15u?literals:15u)<<4))return;
    if(literals>=15u&&!dm_length(literals-15u))return;
    for(uint32_t k=anchor;k<count;k++)if(!dm_byte(src[k]))return;
    dm_u32(dm_bytes+start-4u,dm.n-start);
}
static void dm_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t *pixels){
#if FELUCCA_DISPLAY_BULK
    if(!dm.capturing&&dm.graph_height&&x==0u&&w==240u&&y==dm.graph_top&&h==dm.graph_height){
        dm_blit_raw(x,y,w,h,pixels);return;
    }
    if(dm.capturing&&dm.graph_height&&x==0u&&w==240u&&y==dm.graph_top&&h==dm.graph_height&&h<=DM_GRAPH_STRIPS*4u){
        uint32_t strips=(h+3u)/4u,dirty=0;
        uint64_t mask=0;
        for(uint32_t s=0;s<strips;s++){
            uint32_t rows=h-s*4u;if(rows>4u)rows=4u;
            uint32_t a=2166136261u,b=0x9E3779B9u;
            for(uint32_t i=0;i<rows*w;i++){
                uint32_t v=pixels[s*4u*w+i];
                a=(a^v)*16777619u;b=((b<<5)|(b>>27))^v;
            }
            dm_graph_hash[1][s][0]=a;dm_graph_hash[1][s][1]=b;
            if(!dm_graph_cache.valid||dm_graph_cache.y!=y||dm_graph_cache.h!=h||
               a!=dm_graph_hash[0][s][0]||b!=dm_graph_hash[0][s][1]){mask|=(uint64_t)1u<<s;dirty++;}
        }
        if(!dm.full&&dm_graph_cache.valid&&dirty*2u<strips){
            /* Merge adjacent changed strips, avoiding extra headers and dictionaries. */
            for(uint32_t s=0;s<strips;){
                if(!(mask&((uint64_t)1u<<s))){s++;continue;}
                uint32_t first=s;while(s<strips&&(mask&((uint64_t)1u<<s)))s++;
                uint32_t end=s*4u;if(end>h)end=h;
                dm_blit_raw(x,y+first*4u,w,end-first*4u,pixels+first*4u*w);
                if(dm.invalid)return;
            }
        }else{
            dm_blit_raw(x,y,w,h,pixels);if(dm.invalid)return;
        }
        dm_graph_cache.next_y=y;dm_graph_cache.next_h=h;dm_graph_cache.pending=1;
        return;
    }
#endif
    dm_graph_overwrite(y,h);
    dm_blit_raw(x,y,w,h,pixels);
}
static void dm_end(void){
    if(!dm.capturing)return;
    dm.capturing=0;
    if(!dm.changed){dm.n=0;dm.gen=(dm.gen-1u)&16383u;return;}
    if(!dm.full || (dm.band+1u)*dm.rows>=240u)
        if(!dm_record(4,0,0,0,0,0,0))return;
    dm.ended_ms=DM_NOW();
    dm.need_full=0;dm.chunk_id++;
    __asm__ volatile("" ::: "memory");
    dm.ready=dm.n;
}
static void dm_ack(uint32_t gen){
    if(dm.ready&&gen==dm.gen){
#if FELUCCA_DISPLAY_BULK
        if(dm_graph_cache.pending){
            for(uint32_t s=0;s<DM_GRAPH_STRIPS;s++)for(uint32_t k=0;k<2u;k++)dm_graph_hash[0][s][k]=dm_graph_hash[1][s][k];
            dm_graph_cache.y=dm_graph_cache.next_y;dm_graph_cache.h=dm_graph_cache.next_h;
            dm_graph_cache.valid=1;dm_graph_cache.pending=0;
        }
#endif
        dm.ready=dm.n=0;
        if(dm.full && (dm.band+1u)*dm.rows<240u){dm.band++;dm.need_full=1;return;}
        dm.band=0;if(dm.skipped){dm.need_full=1;dm.skipped=0;}
    }
}
