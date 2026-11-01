/* SPDX-License-Identifier: GPL-3.0-only */
static int veq_page_on(void){return !ui.home&&cur_page()->graph==GR_VEQ;}
static __attribute__((noinline)) int veq_ui_get(unsigned k,int*value){uint32_t slot,zone;if(!vtrim_ui_pick(&slot,&zone))return 0;*value=veq_value(vtrim_flags(slot,zone),k);return 1;}
static void veq_ui_turn(unsigned k,int32_t steps){uint32_t slot,zone;if(k>3||!steps||!vtrim_ui_pick(&slot,&zone))return;uint32_t word=vtrim_flags(slot,zone),mask=(k?31u:127u)<<veq_shift(k);int value=clamp(veq_value(word,k)+clamp(steps,-1000,1000),k?-12:0,k?12:127);uint32_t encoded=((uint32_t)value<<veq_shift(k))&mask;if((word&mask)==encoded)return;vtrim_mask_put(slot,zone,mask,encoded,1);ui.force=1;}

static __attribute__((noinline)) unsigned veq_hz(unsigned value){return (CUTOFF_HZ[value>>1]+CUTOFF_HZ[(value+1)>>1])/2;}
