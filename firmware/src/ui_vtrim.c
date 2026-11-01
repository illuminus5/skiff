/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-phrase non-destructive markers; publish decoded states for next trigger. */
static vocal_trim_region vtrim_view;
static int vtrim_ui_pick(uint32_t*slot,uint32_t*zone){uint32_t source;if(!vocal_family_req(TSEL)||!vocal_selection(TSEL,&source,zone))return 0;*slot=source-SMP_NSETS;return usr_zone[*slot][*zone].n!=0;}
static vocal_trim_region*vtrim_ui_region(void){uint32_t slot,zone;if(!vtrim_ui_pick(&slot,&zone))return 0;vtrim_view=vtrim_get(slot,zone).region;return &vtrim_view;}
static int vtrim_page_on(void){return !ui.home&&cur_page()->graph==GR_VTRIM;}
static void vtrim_ui_turn(uint32_t knob,int32_t steps,int coarse){uint32_t slot,zone;if(!vtrim_ui_pick(&slot,&zone))return;
 vtrim_data d=vtrim_get(slot,zone);vocal_trim_region before=d.region;steps=clamp(steps,-1000,1000);vocal_trim_move(&d.region,knob,steps*(coarse?2205:22));
 if(!memcmp(&d.region,&before,sizeof before))return;
 if(!vtrim_put(slot,zone,&d.region,1))ui_message("SAMPLE NOT READY");ui.force=1;
}
/* Pixel-quantized source positions: no decoder work or audio-state writes. */
static uint32_t vtrim_ui_head(unsigned k){
#if FELUCCA_HARMONY
 if(harmony_on(TSEL)){harmony_part_t*h=harm(TSEL);if(k||!(h->running&1u)||h->generation!=smp_user_gen)return 0;uint32_t n=usr_zone[h->slot][h->zone].n,pos=h->decoder.ph[0];return n?1u+(pos>n?n:pos)*215u/n:0;}
#endif
 return 0;
}
static uint32_t vtrim_ui_sig(void){vocal_trim_region*r=vtrim_ui_region();if(!r)return 0;return vtrim_ui_head(0)*257u+vtrim_ui_head(1)*65537u+vtrim_ui_head(2)*16777259u+r->edge[0]*17u+r->edge[1]*31u+r->edge[2]*61u+r->edge[3]*127u+(ui.hot_t?ui.hot_col+1u:0u);}

#if FELUCCA_VOCAL_STRETCH
static int vtiming_ui_on(void){return 0;}
static unsigned vtiming_ui_get(void){return 0;}
static void vtiming_ui_turn(int32_t steps){(void)steps;}
#endif
