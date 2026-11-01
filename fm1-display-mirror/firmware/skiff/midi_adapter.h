/* SPDX-License-Identifier: GPL-3.0-only
 * Display-only editor extension 83. No calls into synth parameters or transport.
 * Included after editor response helpers. All request handling is main-loop. */
#pragma once
static void dm_reply14(uint32_t n){ed_b(n);ed_b(n>>7);}
static void dm_request(const uint8_t *a,uint32_t n){
    if(!n||a[0]>5u||((a[0]==2u||a[0]==4u)?n!=3u:n!=1u))return;
    uint32_t op=a[0];dm.last_request=fm1_ms;ed_begin(83);ed_b(op);
    if(op==0u){ed_b(FELUCCA_DISPLAY_BULK?3:2);dm_reply14(240);dm_reply14(240);ed_b('S');ed_b('k');ed_b('i');ed_b('f');ed_b('f');}
    else if(op==1u)dm_start(fm1_ms);
    else if(op==3u)dm_stop();
    else if(op==4u)dm_ack(a[1]|(uint32_t)a[2]<<7);
    else if(op==5u){
#if FELUCCA_DISPLAY_BULK
        ed_b(1);
        dm_reply14(dm_perf.capture>16383u?16383u:dm_perf.capture);
        dm_reply14(dm_perf.queue>16383u?16383u:dm_perf.queue);
        dm_reply14(dm_perf.wire>16383u?16383u:dm_perf.wire);
        dm_reply14(dm_perf.ack>16383u?16383u:dm_perf.ack);
        dm_reply14(dm_perf.bytes);dm_reply14(dm_perf.gen);
#else
        ed_b(0);
#endif
    }
    else {
        uint32_t off=a[1]|(uint32_t)a[2]<<7;
        uint32_t state=dm.invalid?2u:dm.ready?1u:0u;
        if(off>dm.ready){state=2;off=0;}
        ed_b(state);dm_reply14(dm.gen);dm_reply14(dm.ready);dm_reply14(off);
        if(state==1u){
            uint32_t count=dm.ready-off;if(count>154u)count=154u;
            for(uint32_t i=0;i<count;){
                uint32_t k=count-i;if(k>7u)k=7u;uint32_t mask=0;
                for(uint32_t j=0;j<k;j++)mask|=(uint32_t)(dm_bytes[off+i+j]>>7)<<j;
                ed_b(mask);for(uint32_t j=0;j<k;j++)ed_b(dm_bytes[off+i+j]);i+=k;
            }
        }
    }
    ed_out[ed_n++]=0xF7;
    (void)display_wire_try(ed_out,ed_n); /* host retries a missed reply; no waits */
}
