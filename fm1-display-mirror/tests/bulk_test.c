#include <stdio.h>
#include <stdint.h>
static uint32_t fm1_ms;
#define DM_NOW() fm1_ms
#include <assert.h>
#define FELUCCA_DISPLAY_BULK 1
#define DM_POOL
#define DM_CAPACITY 16000
#include "../firmware/skiff/capture.h"
#define CI_N 64u
#define CO_N 512u
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static uint8_t cdc_in[CI_N],cdc_out[CO_N];
static uint32_t ci_r,ci_w,co_r,co_w;
static struct {int dtr;} cdc;
static struct {int config;} usb;
#include "../firmware/skiff/bulk_adapter.h"
static void command(uint8_t op){const uint8_t p[]={'F','M','I','R',op};for(unsigned i=0;i<sizeof p;i++)cdc_in[ci_w++%CI_N]=p[i];cdc_task();}
int main(void){
 usb.config=cdc.dtr=1;command(1);assert(dm.enabled&&co_w-co_r==41);co_r=co_w;
 fm1_ms=10;dm_begin(10);dm_fill(0,0,240,240,0x1234);fm1_ms=15;dm_end();
 /* A larger retained transaction tests partial writes, a full ring and resumed output. */
 uint32_t gen=dm.gen;dm.ready=1500;for(unsigned i=0;i<1500;i++)dm_bytes[i]=(uint8_t)i;
 /* Main loop can be busy for the entire transfer: ISR consumption must not
  * require any main-loop refill, and must retain ownership until main ACK. */
 uint8_t packet[64];unsigned got=0;
 while(dm_bulk_isr_pending()){
   fm1_ms++;unsigned n=dm_bulk_isr_take(packet,sizeof packet);assert(n&&n<=64);
   for(unsigned i=0;i<n;i++)assert(packet[i]==(uint8_t)(got+i));got+=n;
 }
 assert(got==1500&&dm.ready&&dm_bulk.done);assert(!dm_bulk_isr_take(packet,64));
 fm1_ms=45;cdc_task();assert(!dm.ready&&dm.gen==gen);
 assert(dm_perf.capture==5&&dm_perf.queue==1&&dm_perf.wire==23&&dm_perf.ack==6&&dm_perf.bytes==1500);
 /* The next segmented section has the same generation but a new chunk id. */
 dm_begin(fm1_ms);dm_fill(0,120,240,120,0x5678);dm_end();assert(dm.ready);
 unsigned expected=dm.ready;got=0;
 while(dm_bulk_isr_pending()){unsigned n=dm_bulk_isr_take(packet,64);assert(n);got+=n;}
 assert(got==expected);cdc_task();assert(!dm.ready);
 fm1_ms=2000;command(2);assert(dm.last_request==2000);
 command(0);assert(!dm.enabled&&!dm_bulk.active);
 command(1);assert(dm.enabled);cdc.dtr=0;cdc_task();assert(!dm.enabled&&!dm_bulk.active);
 puts("bulk: ISR streaming without UI refill, retained ownership, same-generation sections, exact byte order, keepalive, stop and disconnect passed");
}
