#if FELUCCA_TAPE
static void tape_fx_preview(const char *name){
    if(!getenv("TAPE_FX_PREVIEW"))return;
    char path[96];snprintf(path,sizeof path,"build/%s.ppm",name);FILE *f=fopen(path,"wb");if(!f)return;
    fprintf(f,"P6\n240 240\n255\n");
    for(unsigned i=0;i<240*240;i++){
        uint16_t p=host_screen[i];unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};fwrite(rgb,1,3,f);
    }fclose(f);
}
static int test_tape_fx_pages(void){
    int bad=0;ui_power_on();tf_project_reset();tu.page=0;
    hold(B_HOME);ui.menu_sel=MI_TAPE;press(B_OCTUP);turn(EN_SELECT,1);
    bad+=check("Tape SELECT opens FILTER without leaving auxiliary lane",tu.page==1&&ui.menu==3);
    int32_t pitch=tu.pitch,speed=tu.speed,mix=tu.mix;uint32_t length=tu.length;
    turn(EN_K1,30);turn(EN_K2,-40);turn(EN_K3,50);turn(EN_K4,-25);
    bad+=check("Tape FILTER has independent HP LP RES MIX, no playback step change",tape_filter==(30u|87u<<7|50u<<14|75u<<21)&&tu.pitch==pitch&&tu.speed==speed&&tu.length==length&&tu.mix==mix);
    frame();tape_fx_preview("tape-filter");host_blit_rows=0;turn(EN_K3,1);
    bad+=check("Tape FILTER edits redraw its card and graph, not whole page",!ui.force&&host_blit_rows==CARD_H+graph_h());
    turn(EN_SELECT,1);turn(EN_K1,80);turn(EN_K2,1);turn(EN_K3,60);turn(EN_K4,3);
    bad+=check("Tape LFO offers synced rate, shape, depth and destination",tu.page==2&&(tape_lfo&255u)==128u&&((tape_lfo>>8)&7u)==1&&((tape_lfo>>11)&127u)==60&&((tape_lfo>>18)&7u)==TF_PITCH);
    tape_fx_phase=0;frame();host_blit_rows=0;tape_fx_phase=0x80000000u;frame();
    bad+=check("Tape LFO phase updates only old/new seven-pixel strips",host_blit_rows==2*(graph_h()-20)&&tu.marker==120);
    tape_fx_preview("tape-lfo");
    for(unsigned shape=0;shape<5;shape++){
        tape_lfo=(tape_lfo&~(7u<<8))|(shape<<8);ui.force=1;frame();
        bad+=check("Tape LFO draws each supported shape",tu.page==2);
    }
    turn(EN_K1,1000);turn(EN_K3,1000);turn(EN_K4,1000);
    bad+=check("Tape LFO controls clamp packed fields",(tape_lfo&255u)==135u&&((tape_lfo>>11)&127u)==100&&((tape_lfo>>18)&7u)==TF_LEVEL);
    turn(EN_SELECT,-2);bad+=check("Tape SELECT returns to original pitch/speed/length/mix page",tu.page==0&&tu.pitch==pitch&&tu.speed==speed&&tu.length==length&&tu.mix==mix);
    tf_prepare(22050,1,11025);seq_start();bad+=check("Tape restart resets LFO phase without changing controls",!tape_fx_phase&&tape_lfo!=TF_LFO_DEFAULT);
    project_new();bad+=check("Project replacement clears tape playback FX settings and state",tape_filter==TF_FILTER_DEFAULT&&tape_lfo==TF_LFO_DEFAULT&&!tape_fx_phase);
    press(B_HOME);return bad;
}
#endif
