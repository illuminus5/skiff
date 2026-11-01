/* Dedicated CDC pixel stream for the opt-in video-only profile.
 * No console parser, heap, waits or synth commands. Main-loop producer only. */
#pragma once
static volatile struct { uint32_t match, active, offset, hello, chunk, done, first_ms, last_ms; } dm_bulk;
/* USB ISR: copy one bounded packet from a retained, complete capture. No
 * compression, ACK/cache work, waiting, or concurrent producer writes here. */
static uint32_t dm_bulk_isr_pending(void){
    return dm_bulk.active&&!dm_bulk.hello&&!dm.invalid&&dm.ready&&
        (!dm_bulk.done||dm_bulk.chunk!=dm.chunk_id);
}
static uint32_t dm_bulk_isr_take(uint8_t *dst,uint32_t limit){
    if(!dm_bulk.active||dm_bulk.hello||dm.invalid)return 0;
    uint32_t ready=dm.ready;if(!ready)return 0;
    RING_PUBLISH();
    if(dm_bulk.chunk!=dm.chunk_id){
        dm_bulk.chunk=dm.chunk_id;dm_bulk.offset=0;dm_bulk.done=0;dm_bulk.first_ms=fm1_ms;
    }
    if(dm_bulk.done)return 0;
    uint32_t n=ready-dm_bulk.offset;if(n>limit)n=limit;if(n>64u)n=64u;
    for(uint32_t i=0;i<n;i++)dst[i]=dm_bytes[dm_bulk.offset+i];
    dm_bulk.offset+=n;
    if(dm_bulk.offset==ready){dm_bulk.last_ms=fm1_ms;RING_PUBLISH();dm_bulk.done=1;}
    return n;
}
static void cdc_task(void){
    if(!cdc.dtr || !usb.config){
        if(dm_bulk.active)dm_stop();
        dm_bulk.active=dm_bulk.match=dm_bulk.offset=dm_bulk.hello=0;
        return;
    }
    /* Recognize only FMIR + operation: 1 start, 0 stop, 2 keepalive.
     * At most one input ring's worth per call; no arbitrary console commands. */
    uint32_t budget=CI_N;
    while(ci_r!=ci_w && budget--){
        RING_PUBLISH();uint8_t c=cdc_in[ci_r%CI_N];ci_r++;
        if(dm_bulk.match<4u){
            const char *magic="FMIR";
            dm_bulk.match=c==(uint8_t)magic[dm_bulk.match]?dm_bulk.match+1u:c=='F'?1u:0u;
            continue;
        }
        dm_bulk.match=0;
        if(c==1u && !dm_bulk.active){
            dm_start(fm1_ms);dm_bulk.active=1;dm_bulk.offset=0;dm_bulk.hello=1;
        }else if(c==0u){dm_stop();dm_bulk.active=dm_bulk.offset=0;}
        if(dm_bulk.active && (c==1u||c==2u))dm.last_request=fm1_ms;
    }
    if(!dm_bulk.active)return;
    if(!dm.enabled){dm_bulk.active=dm_bulk.offset=0;return;}
    if(dm_bulk.hello){
        if(CO_N-(co_w-co_r)<41u)return;
        uint8_t h[41]={ 'F','M','I','R',1,1 };
        dm_u16(h+12,240);dm_u16(h+14,240);dm_u32(h+24,13);
        for(uint32_t i=0;i<13u;i++)h[28+i]=(uint8_t)"Skiff stream4"[i];
        uint32_t w=co_w;for(uint32_t i=0;i<41u;i++)cdc_out[w++%CO_N]=h[i];
        RING_PUBLISH();co_w=w;dm_bulk.hello=0;
    }
    /* Finish already queued records before recovery so the byte stream stays framed. */
    if(dm.invalid){
        if(co_w!=co_r)return;
        dm_start(fm1_ms);dm_bulk.offset=0;return;
    }
    /* The ISR has copied the last bytes into the endpoint's separate DMA buffer.
     * Only the main loop releases the journal and commits its graph history. */
    if(dm.ready&&dm_bulk.done&&dm_bulk.chunk==dm.chunk_id){
        RING_PUBLISH();
        dm_perf.capture=dm.ended_ms-dm.started_ms;
        dm_perf.queue=dm_bulk.first_ms-dm.ended_ms;
        dm_perf.wire=dm_bulk.last_ms-dm_bulk.first_ms;
        dm_perf.ack=fm1_ms-dm_bulk.last_ms;
        dm_perf.bytes=dm.ready;dm_perf.gen=dm.gen;
        dm_ack(dm.gen);
    }
}
