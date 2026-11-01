/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VOCAL_TRIM_MODEL_H
#define VOCAL_TRIM_MODEL_H
#include <stdint.h>
typedef struct {uint32_t edge[4],length;} vocal_trim_region;
static vocal_trim_region vocal_trim_full(uint32_t length){vocal_trim_region r={{0,0,length,length},length};return r;}
static uint32_t vocal_trim_limit(int64_t value,uint32_t lo,uint32_t hi){return value<(int64_t)lo?lo:value>(int64_t)hi?hi:(uint32_t)value;}
static void vocal_trim_move(vocal_trim_region*r,unsigned marker,int32_t delta){
 if(!r||!r->length||marker>3)return;
 uint32_t span=r->edge[3]-r->edge[0],gap=span<32?span:32;
 if(r->edge[2]-r->edge[1]<gap){r->edge[2]=vocal_trim_limit((int64_t)r->edge[1]+gap,r->edge[0]+gap,r->edge[3]);r->edge[1]=r->edge[2]-gap;}
 if(marker==0){r->edge[0]=vocal_trim_limit((int64_t)r->edge[0]+delta,0,r->edge[3]-gap);
  if(r->edge[1]<r->edge[0])r->edge[1]=r->edge[0];if(r->edge[2]<r->edge[1]+gap)r->edge[2]=r->edge[1]+gap;}
 if(marker==1)r->edge[1]=vocal_trim_limit((int64_t)r->edge[1]+delta,r->edge[0],r->edge[2]-gap);
 if(marker==2)r->edge[2]=vocal_trim_limit((int64_t)r->edge[2]+delta,r->edge[1]+gap,r->edge[3]);
 if(marker==3){r->edge[3]=vocal_trim_limit((int64_t)r->edge[3]+delta,r->edge[0]+gap,r->length);
  if(r->edge[2]>r->edge[3])r->edge[2]=r->edge[3];if(r->edge[1]>r->edge[2]-gap)r->edge[1]=r->edge[2]-gap;}
}
#endif
