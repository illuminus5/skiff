#include <assert.h>
#include <stdio.h>
#define FELUCCA_DISPLAY_BULK 1
#define DM_POOL
#define DM_CAPACITY 16000
#include "../firmware/skiff/capture.h"
static uint16_t pixels[240*138];
int main(void){
 dm_start(0);dm.rows=240;dm.graph_top=76;dm.graph_height=138;
 dm_begin(0);dm_fill(0,0,240,240,0);dm_blit(0,76,240,138,pixels);dm_end();assert(dm.ready);dm_ack(dm.gen);
 assert(dm_graph_cache.valid);
 pixels[50*240+123]=0x1234;
 dm_begin(0);dm_blit(0,76,240,138,pixels);dm_end();
 assert(dm.ready&&!dm.full&&dm_graph_cache.pending);
 assert(dm_bytes[28+5]==3&&dm_bytes[28+18]==124&&dm_bytes[28+22]==4);
 uint32_t bytes=dm.ready;dm_ack(dm.gen);
 dm_begin(0);dm_blit(0,76,240,138,pixels);dm_end();assert(!dm.ready);
 dm_fill(0,80,240,4,0);assert(!dm_graph_cache.valid);
 printf("graph strip cache: %u-byte changed-strip transfer, ACK-only history, unchanged suppression and overwrite invalidation passed\n",bytes);
}
