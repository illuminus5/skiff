#include <assert.h>
#define FELUCCA_DISPLAY_BULK 1
#define UI_DISPLAY_CAPTURE 1
#define UI_TEST_NO_MAIN 1
#define DM_POOL
#define DM_CAPACITY 16000
#include "../../tests/ui_test.c"
#include "../firmware/skiff/ui_adapter.h"
static unsigned peak;
static void capture(const char *name){
    char path[160];snprintf(path,sizeof path,"build/host/display-mirror-%s.bin",name);
    FILE *f=fopen(path,"wb");assert(f);dm_start(0);
    for(unsigned band=0;;band++){
        dm_ui_begin();assert(dm.full);ui_draw();dm_end();
        printf("%s band %u: %u bytes\n",name,band,dm.ready);
        assert(dm.ready&&!dm.invalid);if(dm.ready>peak)peak=dm.ready;
        assert(fwrite(dm_bytes,1,dm.ready,f)==dm.ready);dm_ack(dm.gen);
        if(!dm.band)break;
    }
    assert(dm.band==0&&!dm.need_full);fclose(f);
    snprintf(path,sizeof path,"build/host/display-mirror-%s-panel.bin",name);
    f=fopen(path,"wb");assert(f);fwrite(host_screen,1,sizeof host_screen,f);fclose(f);
}
int main(void){
 dm.ready=1;dm.ended_ms=100;fm1_ms=100;assert(dm_ui_wait());
 fm1_ms=115;assert(dm_ui_wait());fm1_ms=116;assert(!dm_ui_wait());
 dm.ready=0;fm1_ms=100;assert(!dm_ui_wait());
 dm.ready=1;dm.ended_ms=0xfffffff8u;fm1_ms=7;assert(dm_ui_wait());
 fm1_ms=8;assert(!dm_ui_wait());dm.ready=0;
 ui_power_on();ui.shift=1;capture("held-home");
 ui_power_on();ui.layer=LAYER_GLO;capture("held-glo");
 unsigned which=0;
 for(unsigned p=0;p<NPAGES;p++)if(PAGES[p].graph==GR_LFO||PAGES[p].graph==GR_LFODST){
        const char *prefix=which==0?"lfo":which==1?"lfo2":"lfodest";
        char label[64],path[160];which++;
        ui_power_on();ui.home=0;ui.page=p;
        if(PAGES[p].graph==GR_LFODST){TSEL->p[P_LD_PIT]=24;TSEL->p[P_LD_FLT]=24;TSEL->p[P_LD_SHP]=24;TSEL->p[P_LD_AMP]=48;}
        snprintf(label,sizeof label,"%s-initial",prefix);TSEL->lfo_t=0;capture(label);
        snprintf(label,sizeof label,"%s-running",prefix);TSEL->lfo_t=8000;capture(label);
        snprintf(path,sizeof path,"build/host/display-mirror-%s-stream.bin",prefix);
        FILE *stream=fopen(path,"wb");assert(stream);
        snprintf(path,sizeof path,"build/host/display-mirror-%s-running.bin",prefix);
        FILE *base=fopen(path,"rb");assert(base);
        int c;while((c=fgetc(base))!=EOF)fputc(c,stream);fclose(base);
        TSEL->lfo_t+=20;dm_ui_begin();ui_draw();dm_end();assert(dm.ready&&!dm.full);
        uint32_t initial=dm.ready;fwrite(dm_bytes,1,dm.ready,stream);
        str_cpy(ui.msg,"MIRROR TEST",sizeof ui.msg);ui.msg_t=100;
        TSEL->lfo_t+=20;dm_ui_begin();ui_draw();dm_end();assert((dm.graph_dirty&3u)==3u&&!dm.skipped);
        dm_ack(dm.gen);assert(!dm.need_full);
        dm_ui_begin();assert(!dm.graph_dirty&&!dm.full);ui_draw();dm_end();assert(dm.ready);
        fwrite(dm_bytes,1,dm.ready,stream);fclose(stream);
        snprintf(path,sizeof path,"build/host/display-mirror-%s-stream-panel.bin",prefix);
        stream=fopen(path,"wb");assert(stream);fwrite(host_screen,1,sizeof host_screen,stream);fclose(stream);
        printf("%s graph-only transfer: %u then %u bytes; header + graph recovery without full page\n",prefix,initial,dm.ready);
 }
 assert(which==3);
 printf("segmented overlay/LFO peak: %u / %u bytes\n",peak,DM_CAPACITY);
}
