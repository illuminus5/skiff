/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#define VEQ_MASK 0x00fffffcu
static unsigned veq_shift(unsigned k){return k?4u+5u*k:2u;}
static int veq_value(uint32_t word,unsigned k){int v=(int)((word>>veq_shift(k))&(k?31u:127u));return k&&(v&16)?v-32:v;}
static int veq_valid(uint32_t word){if(word&~VEQ_MASK)return 0;for(unsigned k=1;k<4;k++){int v=veq_value(word,k);if(v< -12||v>12)return 0;}return 1;}
