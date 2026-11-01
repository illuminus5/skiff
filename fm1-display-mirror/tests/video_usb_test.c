#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HALF_FRAMES 128
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_CDC 1
#if FELUCCA_DISPLAY_BULK
#define FELUCCA_CDC_DEFAULT 1
#else
#define FELUCCA_CDC_DEFAULT 0
#endif
static void fm1_delay_ms(uint32_t n){(void)n;}
#if FELUCCA_DISPLAY_BULK
static uint32_t dm_bulk_isr_pending(void){return 0;}
static uint32_t dm_bulk_isr_take(uint8_t *p,uint32_t n){(void)p;(void)n;return 0;}
#endif
#include "../../firmware/src/usb.c"
int main(void){
    unsigned n=sizeof CFG_DESC_PLAIN,p=0,midi=0,audio=0,ep=0;
    while(p<n){const uint8_t *d=CFG_DESC_PLAIN+p;assert(d[0]>=2&&p+d[0]<=n);
        if(d[1]==4&&d[5]==1){midi+=d[6]==3;audio+=d[6]==2;}
        if(d[1]==5){assert(d[2]!=0x84);if(d[2]==0x81||d[2]==1){assert((d[3]&3)==2);ep++;}}
        p+=d[0];
    }
    assert(midi==1&&audio==0&&ep==2);
#if FELUCCA_DISPLAY_BULK
    assert(usb_cdc_on==1);
    unsigned bulk=0,notify=0;
    for(unsigned i=0;i<sizeof CFG_DESC;i+=CFG_DESC[i]){
        const uint8_t *d=CFG_DESC+i;
        if(d[1]==5){assert(d[2]!=0x84);if(d[2]==3||d[2]==0x83){assert((d[3]&3)==2);bulk++;}if(d[2]==0x82)notify++;}
    }
    assert(bulk==2&&notify==1);
#endif
#if FELUCCA_DISPLAY_BULK
    co_r=co_w=0;
    for(unsigned i=0;i<128;i++)cdc_out[co_w++%CO_N]=(uint8_t)i;
    assert(cdc_tx_take()==64);for(unsigned i=0;i<64;i++)assert(ep3tx[i]==i);
    assert(cdc_tx_take()==64);for(unsigned i=0;i<64;i++)assert(ep3tx[i]==i+64);
    assert(cdc_tx_take()==0);assert(cdc_tx_take()==~0u);
    for(unsigned i=0;i<65;i++)cdc_out[co_w++%CO_N]=(uint8_t)i;
    assert(cdc_tx_take()==64);assert(cdc_tx_take()==1);assert(cdc_tx_take()==~0u);
    puts("bulk USB: MIDI retained, CDC bulk IN/OUT and notification retained, no audio streaming or EP4");
#else
    assert(usb_cdc_on==0);
    puts("display-only USB: MIDI IN/OUT retained, no audio streaming interface or EP4, CDC off");
#endif
}
