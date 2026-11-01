#include <assert.h>
#include <stdio.h>
#define DM_POOL
#include "../firmware/skiff/capture.h"
int main(int argc,char **argv){
    dm_start(0);assert(dm_begin(1));dm_fill(0,0,240,240,0x0841);
    /* RGB565 source bytes are already panel-endian, not host uint16 order. */
    const uint8_t raw[8]={0xf8,0,7,0xe0,0,0x1f,0xff,0xff};
    dm_blit(0,0,4,1,(const uint16_t *)(const void *)raw);
    uint8_t stripes[4800];for(unsigned i=0;i<4800;i++)stripes[i]=(uint8_t)(i%32);
    dm_blit(0,10,240,10,(const uint16_t *)(const void *)stripes);dm_end();assert(dm.ready&&dm.ready<=DM_CAPACITY);
    if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(dm_bytes,1,dm.ready,f)==dm.ready);fclose(f);}
    uint32_t gen=dm.gen,ready=dm.ready;assert(!dm_begin(2));dm_fill(1,1,1,1,0);assert(dm.skipped);
    dm_ack(gen+1);assert(dm.ready==ready);dm_ack(gen);assert(!dm.ready&&dm.need_full);
    assert(dm_begin(3));dm_fill(239,239,2,1,0);assert(dm.invalid&&!dm.ready);dm_end();assert(!dm.ready);
    dm_start(4);dm_begin(5);for(int i=0;i<400;i++)dm_fill(0,0,1,1,0);dm_end();assert(dm.invalid&&!dm.ready);
    dm_start(10);assert(!dm_begin(3011));assert(!dm.enabled);dm_stop();
    puts("capture: exact panel byte order, bounded overflow, ACK, stale invalidation and expiry passed");
}
