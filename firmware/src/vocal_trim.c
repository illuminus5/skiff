/* SPDX-License-Identifier: GPL-3.0-only */
#include "vocal_trim_model.h"
#if FELUCCA_VOCAL_EQ
#include "vocal_eq_math.h"
#endif
typedef struct {vocal_trim_region region;uint32_t state[2];
#if FELUCCA_VOCAL_STRETCH
 uint32_t timing; /* options: bit0 STRETCH, bit1 AUTO, bits2..23 EQ */
#endif
} vtrim_data;
static vtrim_data vtrim_table[SMP_USER_SLOTS][16][2];
static volatile uint8_t vtrim_active[SMP_USER_SLOTS][16];
static uint8_t vtrim_save,vtrim_ready;
static void vtrim_invalidate(uint32_t slot){if(slot<SMP_USER_SLOTS)vtrim_ready&=(uint8_t)~(1u<<slot);}
static void (*vtrim_load)(uint32_t);
static int vtrim_seek(uint32_t slot,uint32_t zone,uint32_t position,uint32_t*state);
static __attribute__((noinline,minsize)) int vtrim_valid(const vocal_trim_region*r,uint32_t n){return n&&r->length==n&&r->edge[0]<=r->edge[1]&&r->edge[1]<r->edge[2]&&r->edge[2]<=r->edge[3]&&r->edge[3]<=n;}
static __attribute__((noinline,minsize)) vtrim_data vtrim_default(const smp_zone_t*z){
 vtrim_data d={0};d.region=vocal_trim_full(z->n);
 if(z->looped&&z->ls<=z->le&&z->le<z->n){d.region.edge[1]=z->ls;d.region.edge[2]=z->le+1;d.state[1]=(uint16_t)z->pred|((uint32_t)z->idx<<16);}
 return d;
}
static __attribute__((noinline,minsize)) vtrim_data vtrim_get(uint32_t slot,uint32_t zone){
 if(slot>=SMP_USER_SLOTS||zone>=usr_nz[slot])return (vtrim_data){0};
 /* Published records remain immutable while a reader executes: main-loop
  * writers join the audio worker before replacing an inactive record. */
 const vtrim_data*d=&vtrim_table[slot][zone][vtrim_active[slot][zone]&1u];
 return vtrim_valid(&d->region,usr_zone[slot][zone].n)?*d:vtrim_default(&usr_zone[slot][zone]);
}
#if FELUCCA_VOCAL_STRETCH
/* Callers have validated slot/zone readiness. One published word, no region copy. */
static unsigned vtrim_flags(uint32_t slot,uint32_t zone){return vtrim_table[slot][zone][vtrim_active[slot][zone]&1u].timing;}
#endif
/* Main-loop writer. Readers copy a snapshot at note/phrase start. The existing
 * audio worker is joined before the main loop resumes, as for MAN slices. */
static __attribute__((minsize)) int vtrim_put(uint32_t slot,uint32_t zone,const vocal_trim_region*r,int dirty){
 if(slot>=SMP_USER_SLOTS||zone>=usr_nz[slot]||!vtrim_valid(r,usr_zone[slot][zone].n))return 0;
 vtrim_data d=vtrim_default(&usr_zone[slot][zone]);
 if(memcmp(r,&d.region,sizeof *r)){d.region=*r;if(!vtrim_seek(slot,zone,r->edge[0],&d.state[0])||!vtrim_seek(slot,zone,r->edge[1],&d.state[1]))return 0;}
#if FELUCCA_VOCAL_STRETCH
 d.timing=vtrim_get(slot,zone).timing;
#endif
 unsigned next=(vtrim_active[slot][zone]^1u)&1u;vtrim_table[slot][zone][next]=d;
 __asm__ volatile("" ::: "memory");vtrim_active[slot][zone]=(uint8_t)next;
 if(dirty)vtrim_save|=(uint8_t)(1u<<slot);return 1;
}
static __attribute__((minsize)) void vtrim_user_scan(uint32_t slot){
 if(slot>=SMP_USER_SLOTS)return;
 for(unsigned z=0;z<16;z++){vtrim_data d=vtrim_default(&usr_zone[slot][z]);vtrim_table[slot][z][0]=vtrim_table[slot][z][1]=d;vtrim_active[slot][z]=0;}
 vtrim_save&=(uint8_t)~(1u<<slot);if(usr_nz[slot]&&vtrim_load)vtrim_load(slot);vtrim_ready|=(uint8_t)(1u<<slot);
}

#if FELUCCA_VOCAL_STRETCH
static __attribute__((minsize)) void vtrim_mask_put(uint32_t slot,uint32_t zone,uint32_t mask,uint32_t value,int dirty){
 if(slot>=SMP_USER_SLOTS||zone>=usr_nz[slot])return;vtrim_data d=vtrim_get(slot,zone);d.timing=(d.timing&~mask)|(value&mask);
 unsigned next=(vtrim_active[slot][zone]^1u)&1u;vtrim_table[slot][zone][next]=d;__asm__ volatile("" ::: "memory");vtrim_active[slot][zone]=(uint8_t)next;if(dirty)vtrim_save|=(uint8_t)(1u<<slot);
}
static void vtrim_option_put(uint32_t slot,uint32_t zone,unsigned bit,unsigned value,int dirty){vtrim_mask_put(slot,zone,1u<<bit,(value!=0)<<bit,dirty);}
static void vtrim_timing_put(uint32_t slot,uint32_t zone,unsigned value,int dirty){vtrim_option_put(slot,zone,0,value,dirty);}
#endif
