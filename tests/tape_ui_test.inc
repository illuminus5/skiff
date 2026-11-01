#if FELUCCA_TAPE
static int test_tape_aux_page(void)
{
    int bad=0;ui_power_on();
    tape_wanted=tape_page_on=tape_seq_on=tape_command=0;
    memset(&tape_state,0,sizeof tape_state);memset(&tape_seq,0,sizeof tape_seq);
    memset((void *)tape_steps,0,sizeof tape_steps);tape_seq_length=16;
    hold(B_HOME);ui.menu_sel=MI_TAPE;press(B_OCTUP);
    bad+=check("TAPE is an auxiliary page; all six existing instrument tracks stay available",ui.menu==3&&tape_page_on&&NTRK==6);
    bad+=check("TAPE menu uses the existing cassette icon",MTAB_ICON[MTAB_TAPE]==ICON_TAPE&&AI16_IDX[ICON_TAPE]!=0xFF);
    bad+=check("TAPE enable action pulses OCT+, with no empty-buffer clear pulse",oct_leds()==OCT_BREATH);
    press(B_OCTUP);bad+=check("TAPE OCT+ requests buffer ownership",tape_wanted);
    song.playing=1;for(unsigned k=0;k<NTRK;k++)trk_note_on(&trk[k],48+k,100);
    int32_t out[2*CTL];for(unsigned i=0;i<3000;i++)mix_block(out,CTL);
    bad+=check("TAPE page captures the internal mix using existing buffer",tape_state.enabled&&tape_status_frames>40000&&sl_lent);
    bad+=check("TAPE captured audio makes the clear action pulse OCT-",(oct_leds()&(OCT_BREATH|OCT_BREATH_DN))==(OCT_BREATH|OCT_BREATH_DN));
    /* Measure actual panel traffic as well as pixels: identical full repaint
     * would pass a snapshot check but must fail this regression. */
    static uint16_t previous_pixels[240*240];
    const int32_t turns[4]={3,-5,1,-10};int partial=1;
    for(unsigned c=0;c<4;c++){
        frame();memcpy(previous_pixels,host_screen,sizeof previous_pixels);host_blit_rows=0;
        turn(EN_K1+c,turns[c]);partial &= host_blit_rows==CARD_H;
        for(unsigned y=0;y<240;y++)for(unsigned x=0;x<240;x++)
            if(y<Y_LABEL||y>=Y_LABEL+CARD_H||x<(unsigned)CARD_X(c)||x>=(unsigned)CARD_X(c)+CARD_W)
                partial &= previous_pixels[y*240+x]==host_screen[y*240+x];
    }
    bad+=check("TAPE knob turns repaint only their own card, without blanking header/grid/footer",partial);
    host_blit_rows=0;frame();
    bad+=check("TAPE unchanged frame sends no panel redraw",host_blit_rows==0);
    bad+=check("TAPE knobs edit pitch, speed, length and mix through command words",tu.pitch==3&&tu.speed==75&&tu.length==3&&tu.mix==90&&(tape_command>>31));
    press(B_SAVE);uint32_t first=tape_steps[0];
    bad+=check("TAPE SAVE stores a tape event, without changing instrument sequence notes",(first&3u)==TS_TRIGGER&&((first>>9)&1023)==475&&((first>>19)&127)==90);
    int lock_partial=1;
    for(unsigned c=0;c<4;c++)for(unsigned d=0;d<2;d++){
        host_blit_rows=0;turn(EN_K1+c,d?-1:1);lock_partial &= host_blit_rows==CARD_H;
    }
    bad+=check("TAPE parameter-lock edits do not repaint unchanged step tiles",lock_partial);
    turn(EN_PRESET,1);press(B_SAVE);press(B_REC);
    bad+=check("TAPE has an independently enabled sequence and step cursor",tu.cursor==1&&tape_seq_on&&(tape_steps[1]&3u)==TS_TRIGGER);
    turn(EN_ALGO,1);bad+=check("TAPE auxiliary sequence expands independently to 32 steps",tape_seq_length==32);
    tape_page_on=0;unsigned before_gates=gates();key_down(white(0));tape_page_on=1;key_up(white(0));
    bad+=check("TAPE entry does not swallow note-off for a previously held synth key",gates()==before_gates);
    unsigned voices=voices_busy();key_down(white(2));frame();
    bad+=check("TAPE page's white keys select history positions without playing synth notes",tu.slice==2&&kb_note[white(2)]==KB_SILENT&&voices_busy()==voices);
    key_up(white(2));frame();
    for(unsigned w=0;w<16;w++){key_down(white(w));frame();
        bad+=check("TAPE white-key mapping follows the physical keyboard",tu.slice==w&&kb_note[white(w)]==KB_SILENT);
        key_up(white(w));frame();}
    press(B_REC);mix_block(out,CTL);
    bad+=check("TAPE sequence OFF returns to capture",!tape_seq_on&&!tape_state.held);
    song.g[G_CLOCK]=1;press(B_REC);bad+=check("TAPE rejects unsupported external-clock sequence mode",!tape_seq_on);
    song.g[G_CLOCK]=0;
#if FELUCCA_USB_6CH
    uint32_t routes_before=usb_six_routes,extra_before=usb_extra_routes;int route_ok=1;
    const char *const names[3]={"TAPE L","TAPE R","TAPE MONO"};
    for(unsigned slot=0;slot<UAC_CHANNELS;slot++){
        menu_put(MI_USB_LEFT+slot,25u+slot%3);
        route_ok &= usb_six_get(slot)==27u+slot%3&&menu_get(MI_USB_LEFT+slot)==25u+slot%3&&
            !strcmp(menu_vname(MI_USB_LEFT+slot,25u+slot%3),names[slot%3]);
    }
    sks_t tape_routes;settings_export(&tape_routes);
    usb_six_routes=USB_SIX_DEFAULT;usb_extra_routes=USB_EXTRA_DEFAULT;
    route_ok &= settings_import(&tape_routes,sizeof tape_routes)!=0;
    for(unsigned slot=0;slot<UAC_CHANNELS;slot++)route_ok &= usb_six_get(slot)==27u+slot%3;
    bad+=check("TAPE USB L/R/mono menu choices and saved settings round-trip on all ten channels",route_ok);
    usb_six_routes=routes_before;usb_extra_routes=extra_before;
#endif
    ui.force=1;draw_menu();
    bad+=check("TAPE reuses the normal four parameter cards",!memcmp(ui.col[0],"PITCH|",6)&&!memcmp(ui.col[1],"SPEED|",6)&&
               !memcmp(ui.col[2],"LENGTH|",7)&&!memcmp(ui.col[3],"MIX|",4));
    ui_prefs|=PREF_LARGE;ui.force=1;draw_menu();
    bad+=check("TAPE LARGE mode retains the STEP screen geometry",large_kind()==LK_LABEL&&card_h()==CARD_H&&graph_y()==Y_GRAPH);
    ui_prefs&=~PREF_LARGE;ui.force=1;draw_menu();
    ui.force=0;uint32_t saved_frames=tape_status_frames;host_blit_rows=0;
    tape_status_frames=10000;draw_menu();
    bad+=check("TAPE buffer diagnostics are absent: capture count changes do not repaint footer",host_blit_rows==0);
    tape_status_frames=saved_frames;
    FILE *f=fopen("build/tape-page.ppm","wb");
    if(f){fprintf(f,"P6\n240 240\n255\n");for(unsigned i=0;i<240*240;i++){
        uint16_t v=(host_screen[i]>>8)|(host_screen[i]<<8);
        uint8_t rgb[3]={(uint8_t)(((v>>11)&31)*255/31),(uint8_t)(((v>>5)&63)*255/63),(uint8_t)((v&31)*255/31)};
        fwrite(rgb,1,3,f);}fclose(f);}
    press(B_HOME);bad+=check("TAPE page closes without taking an instrument track",!ui.menu&&!tape_page_on&&song.sel<NTRK);
    press(B_FX);uint32_t count=0,sendpage=NPAGES;
    for(unsigned i=0;i<NPAGES;i++)if(PAGES[i].fam==FAM_FX){++count;if(PAGES[i].graph==GR_NONE)sendpage=i;}
    for(unsigned i=0;i<7&&ui.page!=sendpage;i++)press(B_FX);
    bad+=check("TAPE SEND is seventh FX page",count==7&&ui.page==sendpage);
    song.sel=0;frame();turn(EN_K1,37);turn(EN_K2,80);
    bad+=check("FX TAPE SEND edits only selected track and ignores blank controls",tape_send[0]==37&&tape_send[1]==0);
    frame();host_blit_rows=0;turn(EN_K1,1);
    bad+=check("FX TAPE SEND repaints only its card",host_blit_rows==CARD_H&&tape_send[0]==38);
    turn(EN_ALGO,1);turn(EN_K1,49);
    bad+=check("FX track selection retains independent send levels",song.sel==1&&tape_send[1]==49&&tape_send[0]==38);
    turn(EN_K1,1000);bad+=check("FX TAPE SEND clamps to 100 percent",tape_send[1]==100);
    for(unsigned i=0;i<NTRK;i++){song.sel=i;frame();turn(EN_K1,-1000);turn(EN_K1,10+(int)i);}
    int sends_ok=1;for(unsigned i=0;i<NTRK;i++)sends_ok &= tape_send[i]==10+i;
    bad+=check("FX TAPE SEND independently edits all six tracks",sends_ok);
    press(B_HOME);
    tape_wanted=tape_seq_on=0;for(unsigned i=0;i<10;i++)mix_block(out,CTL);
    bad+=check("TAPE returns stutter buffer ownership after release",!tape_state.enabled&&!sl_lent);
    tape_wanted=1;for(unsigned i=0;i<10;i++)mix_block(out,CTL);
    tape_steps[0]=first;tape_seq_on=1;project_new();
    bad+=check("TAPE RAM audio and events are cleared by project replacement",!tape_wanted&&!tape_seq_on&&!tape_steps[0]&&!tape_state.enabled&&!sl_lent&&!tape_send[0]&&!tape_send[5]);
    hold(B_HOME);ui.menu_sel=MI_TAPE;press(B_OCTUP);frame();
    turn(EN_K1,5);turn(EN_K2,-7);turn(EN_K3,1);turn(EN_K4,-13);
    project_t saved;project_capture(&saved);
    int saved_pitch=tu.pitch,saved_speed=tu.speed,saved_length=tu.length,saved_mix=tu.mix;
    project_new();frame();
    bad+=check("TAPE NEW synchronizes visible controls to defaults",tu.pitch==0&&tu.speed==100&&tu.mix==100);
    int restored=project_restore_runtime(&saved);frame();
    bad+=check("TAPE project restore synchronizes all four visible controls",!restored&&tu.pitch==saved_pitch&&tu.speed==saved_speed&&tu.length==saved_length&&tu.mix==saved_mix);
    bad+=check("TAPE project restore stays disarmed with empty audio",!tape_wanted&&!tape_seq_on&&!tape_state.enabled);
    return bad;
}
#endif
