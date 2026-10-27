/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_PORT_ONLY 1
#include "ui_test.c"
#include <assert.h>
int main(void){
 ui_power_on();unsigned page=0;for(unsigned i=0;i<NPAGES;i++)if(PAGES[i].graph==GR_DCOMP)page=i;assert(page);
 assert(!page_visible(page));host_preset(&trk[0],ENGI_DRUM,0);frame();assert(page_visible(page));
 go_title("DRUM COMP");frame();assert(cur_page()->graph==GR_DCOMP);
 uint8_t preset=trk[0].preset;turn(EN_PRESET,1);assert(page_lane()==1&&trk[0].preset==preset);
 turn(EN_K1,5);turn(EN_K2,1);turn(EN_K3,7);turn(EN_K4,100);
 assert(dc_get(0,1,0)==69&&dc_get(0,1,1)==80&&dc_get(0,1,2)==31&&dc_get(0,1,3)==100);
 FILE *image=fopen("build/drum-comp-page.ppm","wb");
 if(image){fprintf(image,"P6\n240 240\n255\n");for(unsigned i=0;i<240*240;i++){
  uint16_t v=(host_screen[i]>>8)|(host_screen[i]<<8);uint8_t rgb[3]={(uint8_t)(((v>>11)&31)*255/31),(uint8_t)(((v>>5)&63)*255/63),(uint8_t)((v&31)*255/31)};fwrite(rgb,1,3,image);}fclose(image);}
 assert(!dc_get(0,0,3));turn(EN_PRESET,-1);assert(page_lane()==0&&!dc_get(0,0,3));
 last_note=DRUM_LANE_NOTE[3];frame();assert(page_lane()==3);
 dc_set(0,3,3,87);uint8_t saved[DC_BYTES];memcpy(saved,drum_comp[0],DC_BYTES);
 apply_preset_to(TSEL,1);assert(!dc_get(0,3,3));undo_step(0);assert(!memcmp(saved,drum_comp[0],DC_BYTES));undo_step(1);assert(!dc_get(0,3,3));
 host_preset(&trk[0],ENGI_DRUMX,0);go_title("DRUM COMP");frame();assert(page_visible(page));turn(EN_PRESET,7);assert(page_lane()==7);turn(EN_K4,127);assert(dc_get(0,7,3)==127);
 host_preset(&trk[1],ENGI_DRUM,0);track_select(1);go_title("DRUM COMP");frame();assert(!dc_get(1,page_lane(),3));
 project_t p;project_capture(&p);project_store_t wire;assert(proj_pack5(&wire,&p));project_new();assert(!project_restore_runtime(&p)&&dc_get(0,7,3)==127);
 go_title("DRUM COMP");frame();host_blit_rows=0;frame();assert(!host_blit_rows);
 puts("DRUM COMP UI: drum-only visibility, four controls, one ratio per detent, PRESETS/key lane selection, track isolation, factory-reset and sound undo/redo, project restore and idle redraw suppression passed");return 0;
}
