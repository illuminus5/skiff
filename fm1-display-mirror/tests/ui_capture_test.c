#include <assert.h>
#define UI_DISPLAY_CAPTURE 1
#define UI_TEST_NO_MAIN 1
#define DM_POOL
#include "../../tests/ui_test.c"
int main(void){
    ui_power_on();dm_start(0);assert(dm_begin(0));ui.force=1;ui_draw();dm_end();
    printf("actual Skiff HOME capture: %u bytes, invalid=%u, scanned=%u pixels\n",dm.ready,dm.invalid,dm.pixels);
    if(dm.ready){FILE *f=fopen("build/host/display-mirror-ui.bin","wb");assert(f);fwrite(dm_bytes,1,dm.ready,f);fclose(f);
        f=fopen("build/host/display-mirror-ui-panel.bin","wb");assert(f);fwrite(host_screen,1,sizeof host_screen,f);fclose(f);}
    dm_start(1);dm_begin(1);ui.menu=1;ui.force=1;ui_draw();dm_end();
    printf("actual Skiff MENU capture: %u bytes, invalid=%u, scanned=%u pixels\n",dm.ready,dm.invalid,dm.pixels);
    uint32_t count=0,over=0,peak=0;
    for(uint32_t engine=0;engine<NENGINES;engine++){
        ui_power_on();set_engine_of(&trk[0],engine);trk[0].engine=trk[0].eng_req;
        for(uint32_t page=0;page<NPAGES;page++){
            if(!page_visible(page))continue;
            ui.home=0;ui.menu=0;ui.page=(uint8_t)page;ui.force=1;
            dm_start(10);dm_begin(10);ui_draw();dm_end();count++;
            if(dm.invalid)over++;if(dm.ready>peak)peak=dm.ready;
        }
    }
    printf("actual visible engine/page sweep: %u screens, %u overflowed, peak %u bytes (capacity %u)\n",count,over,peak,(unsigned)DM_CAPACITY);
    assert(over==0);
    return 0;
}
