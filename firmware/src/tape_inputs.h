/* SPDX-License-Identifier: GPL-3.0-only */
/* Each core owns a cache-line-aligned sum. Published sends are sampled before
 * CPU1 starts; capture never contains master/FX/tape output. Settings saved in project metadata; captured audio remains RAM-only. */
static volatile uint32_t tape_send[6];
static uint32_t tape_send_block[6], tape_input_read;
static struct { int32_t l[CTL],r[CTL]; } tape_input[2] __attribute__((aligned(128)));
static __attribute__((noinline)) void tape_input_begin(uint32_t n)
{
    tape_input_read=0;
    for(uint32_t k=0;k<6;k++)tape_send_block[k]=tape_send[k]>100?100:tape_send[k];
    for(uint32_t c=0;c<2;c++)for(uint32_t i=0;i<n;i++)tape_input[c].l[i]=tape_input[c].r[i]=0;
}
static void tape_record_input(int32_t *l,int32_t *r)
{
    uint32_t i=tape_input_read++;
    *l=i<CTL?tape_input[0].l[i]+tape_input[1].l[i]:0;
    *r=i<CTL?tape_input[0].r[i]+tape_input[1].r[i]:0;
}
#define TAPE_RECORD_INPUT(l,r) tape_record_input(l,r)
