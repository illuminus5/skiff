/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop-only seek from the existing source checkpoint grid. A zone can
 * share encoded data with another key zone; find its material segment by off. */
static int vtrim_seek(uint32_t slot,uint32_t zone,uint32_t position,uint32_t*state){
 if(!position){*state=0;return 1;}
 const smp_zone_t*z=&usr_zone[slot][zone];const slc_src_t*s=slc_get(slot+1u);
 if(s)for(unsigned i=0;i<s->nseg;i++)if(s->seg[i].off==z->off&&position<s->seg[i].n)
  {*state=slc_state_at(s,s->seg[i].at+position)&0x00ffffffu;return 1;}
 /* Valid uploaded VOCAL zones must have a SLICE segment; a malformed source
  * cannot be published by the UI. Never linear-decode from an audio IRQ. */
 return 0;
}
