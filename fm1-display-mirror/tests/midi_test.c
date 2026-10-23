#include <assert.h>
#define FELUCCA_DISPLAY_MIRROR 1
#define FELUCCA_OTA 1
#define DM_POOL
#include "../firmware/skiff/capture.h"
#define main hostsim_main
#include "../../tests/hostsim.c"
#undef main
static uint32_t ota_now_ms(void){return fm1_ms;}
static void ota_idle(void){}
static uint8_t ed_out[600];static uint32_t ed_n;
static void ed_begin(uint32_t cmd){ed_n=5;ed_out[0]=0xf0;ed_out[1]=0x7d;ed_out[2]=0x46;ed_out[3]=0x4c;ed_out[4]=cmd;}
static void ed_b(uint32_t n){assert(ed_n<599);ed_out[ed_n++]=(uint8_t)(n&127);}
#include "../firmware/skiff/midi_adapter.h"
static uint8_t wire[256];
static uint32_t drain(void){uint32_t n=0;while(so_r!=so_w){uint32_t p=sx_out_q[so_r++%SXQ],cin=p&15,k=cin==4||cin==7?3:cin==6?2:1;for(uint32_t j=0;j<k;j++)wire[n++]=(uint8_t)(p>>(8*(j+1)));}return n;}
int main(void){
 usb.config=1;uint8_t cap[]={0};dm_request(cap,1);uint32_t n=drain();assert(n==17&&wire[4]==83&&wire[5]==0&&wire[6]==(FELUCCA_DISPLAY_BULK?3:2)&&wire[n-1]==0xf7);
 mo_w=1;dm_request(cap,1);assert(so_r==so_w);mo_w=0;
 usb.config=0;dm_request(cap,1);assert(so_r==so_w);usb.config=1;
 uint8_t start[]={1};dm_request(start,1);drain();assert(dm.enabled);
 dm_begin(1);dm_fill(0,0,240,240,0x1234);dm_end();uint32_t ready=dm.ready;
 uint8_t poll[]={2,0,0};dm_request(poll,3);n=drain();assert(n<=192&&wire[6]==1&&dm.ready==ready);
 dm_request(poll,2);assert(so_r==so_w);
 /* Maximum reply fits exactly within the existing 64-event SysEx ring. */
 dm.ready=200;for(uint32_t i=0;i<200;i++)dm_bytes[i]=(uint8_t)i;
 dm_request(poll,3);n=drain();assert(n==190&&wire[n-1]==0xf7);
 uint32_t at=13,off=0;while(at<n-1){uint8_t mask=wire[at++];for(uint32_t j=0;j<7&&at<n-1;j++,off++)assert((wire[at++]|((mask>>j&1)<<7))==(uint8_t)off);}
 assert(off==154);
 uint8_t ack[]={4,(uint8_t)(dm.gen&127),(uint8_t)(dm.gen>>7)};dm_request(ack,3);drain();assert(!dm.ready);
#if FELUCCA_DISPLAY_BULK
 dm_perf.capture=5;dm_perf.queue=1;dm_perf.wire=23;dm_perf.ack=6;dm_perf.bytes=1500;dm_perf.gen=10;
 uint8_t timing[]={5};dm_request(timing,1);n=drain();
 assert(n==20&&wire[5]==5&&wire[6]==1&&wire[7]==5&&wire[11]==23&&wire[13]==6);
 assert((wire[15]|wire[16]<<7)==1500&&wire[17]==10);
#endif
 uint8_t stop[]={3};dm_request(stop,1);drain();assert(!dm.enabled);
 puts("MIDI adapter: capability, bounded atomic replies, existing MIDI priority, disconnect, fragmentation and ACK passed");
}
