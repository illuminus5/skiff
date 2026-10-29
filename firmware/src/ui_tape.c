/* SPDX-License-Identifier: GPL-3.0-only */
/* First auxiliary tape page. Project controls persist; audio gets atomic command words. */
static void enc_drop(void);
static struct {int32_t pitch,speed,mix;uint32_t length,slice,cursor,keys,header_sig,tiles_sig,footer_sig,page,wave_word,edit_gen;int32_t marker,wave_hold;} tu={0,100,100,2,0,0,0,0};
static void tape_ui_sync(void)
{
    if(tu.edit_gen==tape_edit_gen)return;
    uint32_t w=tape_live;tu.pitch=(int32_t)((w>>26)&31u)-12;
    tu.speed=(int32_t)((w>>9)&1023u)-400;tu.length=(w>>6)&7u;tu.mix=(int32_t)((w>>19)&127u);
    tu.slice=tu.cursor=tu.keys=tu.page=0;tu.edit_gen=tape_edit_gen;
}
static uint32_t tape_ui_word(uint32_t action){return ts_pack(action,tu.slice,tu.length,tu.speed,(uint32_t)tu.mix,tu.pitch);}
static void tape_ui_send(uint32_t action){tape_live=tape_ui_word(TS_HOLD);tape_command=0x80000000u|tape_ui_word(action);}
static void tape_ui_input(uint32_t notes,uint32_t pressed,uint32_t oct)
{
    tape_ui_sync();
    int32_t s;int changed=0;
    if((s=panel_enc(EN_SELECT))){
        uint32_t page=(uint32_t)clamp((int32_t)tu.page+s,0,2);
        if(page!=tu.page){tu.page=page;ui.force=1;}
    }
    if(tu.page){
        static const uint8_t shifts[2][4]={{0,7,14,21},{0,8,11,18}};
        static const uint8_t maxima[2][4]={{127,127,100,100},{135,4,100,TF_DEST_N-1}};
        volatile uint32_t *setting=tu.page==1?&tape_filter:&tape_lfo;
        for(uint32_t j=0;j<4;j++)if((s=panel_enc(EN_K1+j))){
            uint32_t shift=shifts[tu.page-1][j],mask=tu.page==2&&j==0?255u:tu.page==2&&(j==1||j==3)?7u:127u;
            uint32_t word=*setting;
            uint32_t value=(uint32_t)clamp((int32_t)((word>>shift)&mask)+s,0,maxima[tu.page-1][j]);
            *setting=(word&~(mask<<shift))|(value<<shift);
        }
    }else{

    if((s=panel_enc(EN_K1))){tu.pitch=clamp(tu.pitch+s,-12,12);changed=1;}
    if((s=panel_enc(EN_K2))){tu.speed=clamp(tu.speed+clamp(s,-80,80)*5,-400,400);changed=1;}
    if((s=panel_enc(EN_K3))){tu.length=(uint32_t)clamp((int32_t)tu.length+s,0,4);changed=1;}
    if((s=panel_enc(EN_K4))){tu.mix=clamp(tu.mix+s,0,100);changed=1;}
    }
    if((s=panel_enc(EN_PRESET))){
        tu.cursor=(uint32_t)clamp((int32_t)tu.cursor+s,0,(int32_t)tape_seq_length-1);
        uint32_t w=tape_steps[tu.cursor];
        if((w&3u)==TS_TRIGGER){tu.slice=(w>>2)&15;tu.length=(w>>6)&7;
            tu.speed=tt_clip((int32_t)((w>>9)&1023)-400,-400,400);
            tu.mix=tt_clip((int32_t)((w>>19)&127),0,100);tu.pitch=tt_clip((int32_t)((w>>26)&31)-12,-12,12);}
    }
    if((s=panel_enc(EN_ALGO))){tape_seq_length=(uint32_t)clamp((int32_t)tape_seq_length+(s>0?16:-16),16,64);
        if(tu.cursor>=tape_seq_length)tu.cursor=tape_seq_length-1;}
    if(changed){
        if((tape_steps[tu.cursor]&3u)==TS_TRIGGER)tape_steps[tu.cursor]=tape_ui_word(TS_TRIGGER);
        tape_ui_send(TS_HOLD);
    }
    if((pressed>>panel.btn[B_REC])&1u){if(tape_seq_on){tape_seq_on=0;tape_ui_send(TS_CAPTURE);}
        else if(song.g[G_CLOCK])ui_message("TAPE: INTERNAL CLOCK");else tape_seq_on=1;}
    if((pressed>>panel.btn[B_SAVE])&1u)tape_steps[tu.cursor]=(tape_steps[tu.cursor]&3u)==TS_TRIGGER?0:tape_ui_word(TS_TRIGGER);
    if(oct&2u){tape_wanted=!tape_wanted;if(!tape_wanted)tape_seq_on=0;}
    if(oct&1u)tape_ui_send(TS_CLEAR);
    uint32_t held=0;
    for(uint32_t k=0;k<27;k++){
        uint32_t place=key_place(k);if(key_black(k)||place>=16)continue;
        if((fm1_in.notes>>k)&1u)held|=1u<<place;
        if((notes>>k)&1u){tu.slice=place;tape_ui_send(TS_TRIGGER);}
    }
    if(tu.keys&&!held&&!tape_seq_on)tape_ui_send(TS_CAPTURE);
    tu.keys=held;enc_drop();
}
static int32_t tape_lfo_y(int32_t x)
{
    int32_t height=(int32_t)graph_h()-20,depth=(int32_t)((tu.wave_word>>11)&127u);
    uint32_t shape=(tu.wave_word>>8)&7u;
    int32_t value=shape==4?tu.wave_hold:tf_wave((uint32_t)(x-12)*19884107u,shape,0)*depth/100;
    return height/2-value*(height/2-4)/32768;
}
static void tape_lfo_strip(int32_t x,int marker)
{
    int32_t height=(int32_t)graph_h()-20;
    cv_begin(7,height,T_BG);
    for(int32_t j=1;j<7;j++)cv_line(j-1,tape_lfo_y(x+j-1),j,tape_lfo_y(x+j),T_THEME);
    if(marker)cv_rect(1,tape_lfo_y(x+3)-2,5,5,T_TEXT);
    cv_blit(x,graph_y());
}
static void tape_fx_graph(uint32_t theme)
{
    tu.wave_word=tape_lfo;tu.wave_hold=tape_fx_mod;
    uint32_t word=tu.page==1?tape_filter:tu.wave_word;
    uint32_t sig=word*16777619u+theme+tu.page;
    if(tu.page==2&&((word>>8)&7u)==4)sig+=(uint32_t)(tu.wave_hold/256)*31u;
    if(ui.force||sig!=tu.tiles_sig){
        tu.tiles_sig=sig;tu.marker=-1;cv_begin(240,graph_h(),T_BG);
        if(tu.page==1){
            int32_t hp=(int32_t)(word&127u),lp=(int32_t)((word>>7)&127u);
            int32_t a=12+hp*216/127,b=12+lp*216/127;
            cv_text(8,6,&AF_S,"PLAYBACK FILTER",T_MID);
            cv_line(12,40,228,40,T_DIM);
            if(a<b)cv_rect(a,38,b-a,5,T_THEME);
            cv_line(a,31,a,49,T_TEXT);cv_line(b,31,b,49,T_TEXT);
            cv_text(12,55,&AF_S,"LOW",T_MID);cv_text_r(228,55,&AF_S,"HIGH",T_MID,T_BG);
        }else{
            for(int32_t x=13;x<228;x++)cv_line(x-1,tape_lfo_y(x-1),x,tape_lfo_y(x),T_THEME);
            cv_text(8,(int32_t)graph_h()-17,&AF_S,"PLAYBACK MODULATION",T_MID);
        }
        cv_blit(0,graph_y());
    }
    if(tu.page==2){
        int32_t marker=12+(int32_t)(((uint64_t)tape_fx_phase*216u)>>32);
        if(marker!=tu.marker){if(tu.marker>=0)tape_lfo_strip(tu.marker-3,0);tape_lfo_strip(marker-3,1);tu.marker=marker;}
    }
}

/* Same geometry and tile styling as SEQ > STEP (1/7). A tape event shows its
 * history slice instead of a note. Cursor and running-step number stay distinct. */
static void tape_ui_tiles(void)
{
    uint32_t base=tu.cursor&~15u;
    int32_t top=((int32_t)cv_h-(TL_P+TL_H+3+AF_S_CAP_H))/2;
    cv_rect(0,0,240,(int32_t)cv_h,T_BG);cv_bg=T_BG;
    for(uint32_t k=0;k<16&&base+k<tape_seq_length;k++){
        uint32_t step=base+k,word=tape_steps[step];int on=(word&3u)==TS_TRIGGER;
        int32_t x=4+29*(int32_t)(k%8),y=top+TL_P*(int32_t)(k/8);
        uint16_t fill=step==tu.cursor?T_TEXT:on?T_THEME:T_SURF;
        if(ux.neon&&(on||step==tu.cursor))ne_tube(x,y,TL_W,TL_H,fill,T_BG);
        else cv_rrect(x,y,TL_W,TL_H,3,fill,T_BG);
        if(ux.crypt&&(on||step==tu.cursor))
            cv_frame(x+2,y+2,TL_W-4,TL_H-4,step==tu.cursor?CR_GOLD:ux_mix(fill,T_INK,45));
        char text[8];
        if(on){text[0]='S';fmt_int(text+1,(int32_t)((word>>2)&15)+1);
            cv_text_in(x,y+TL_NY,TL_W,&AF_S,text,T_INK,fill);}
        fmt_int(text,(int32_t)step+1);
        cv_text_in(x,y+TL_H+3-AF_S_CAP_Y,TL_W,&AF_S,text,
                   tape_seq_on&&song.playing&&step==tape_status_step?T_REC:T_MID,T_BG);
    }
}
static void tape_ui_draw(void)
{
    tape_ui_sync();
    /* Independent dirty regions: knob values use the existing card caches.
     * Only displayed step/slice bits invalidate tiles, not pitch/speed/mix locks. */
    uint32_t theme=ui_prefs*31u+ux.gen*43u;
    uint32_t header=tu.cursor+257u*tape_seq_length+4099u*tape_status_flags+
        65537u*tape_wanted+524287u*tu.page+131071u*song.playing+262147u*(tape_seq_on&&song.g[G_CLOCK])+theme;
    uint32_t tiles=(tu.cursor+1u)*16777619u+tape_seq_length+theme;
    tiles=tiles*31u+(tape_seq_on&&song.playing?tape_status_step+1u:0u);
    uint32_t first=tu.cursor&~15u;
    for(uint32_t i=0;i<16&&first+i<tape_seq_length;i++){
        uint32_t word=tape_steps[first+i];
        uint32_t visible=(word&3u)==TS_TRIGGER?1u+((word>>2)&15u):0u;
        tiles=(tiles^visible)*16777619u;
    }
    uint32_t footer=tape_seq_on*4099u+theme+tu.page*65537u;
    if(ui.force)lcd_fill(0,0,240,240,T_BG);
    char text[40];
    if(ui.force||header!=tu.header_sig){
    tu.header_sig=header;
    cv_begin(240,H_HEAD,T_BG);
    cv_text(8,HEAD_MY,&AF_M,tu.page==1?"FILTER":tu.page==2?"LFO":"TAPE",T_TEXT);
    const char *state=!tape_wanted?"OFF":(tape_seq_on&&song.g[G_CLOCK])?"INT CLOCK":
        (tape_status_flags&4)?"WAIT FX":(tape_status_flags&2)?"LOOP":!song.playing?"PRESS PLAY":"CAPTURE";
    cv_text(65,HEAD_SY,&AF_S,state,T_MID);
    str_cpy(text,"STEP ",sizeof text);fmt_int(text+5,(int32_t)tu.cursor+1);
    str_cpy(text+str_len(text),"/",sizeof text-str_len(text));fmt_int(text+str_len(text),(int32_t)tape_seq_length);
    if(tu.page){fmt_int(text,(int32_t)tu.page+1);str_cpy(text+1,"/3",sizeof text-1);}
    cv_text_r(232,HEAD_SY,&AF_S,text,T_THEME,T_BG);cv_blit(0,0);
    }

    /* Reuse Skiff's real four knob-card renderer, including theme/LARGE support. */
    if(tu.page==1){
        uint32_t word=tape_filter;
        for(uint32_t k=0;k<4;k++){
            uint32_t value=(word>>(k*7u))&127u;const char *unit="";
            if(k<2){
                if(k==0&&!value)str_cpy(text,"OFF",sizeof text);
                else if(k==1&&value==127)str_cpy(text,"OPEN",sizeof text);
                else if(CUTOFF_HZ[value]<1000){fmt_int(text,CUTOFF_HZ[value]);unit="Hz";}
                else {fmt_fix(text,CUTOFF_HZ[value]/100,1);unit="kHz";}
            }else{fmt_int(text,(int32_t)value);unit="%";}
            static const char *const names[]={"HP","LP","RES","MIX"};
            draw_column(k,names[k],text,unit,VR_LIVE,k<2?-1:(int32_t)value*10,ICON_AUTO);
        }
    }else if(tu.page==2){
        uint32_t word=tape_lfo,rate=word&255u;
        if(rate>135)rate=135;
        if(rate<128){fmt_fix(text,(int32_t)LFO_HZ_X100[rate],2);}
        else {static const char *const sync[]={"4 BAR","2 BAR","1 BAR","1/2","1/4","1/8","1/16","1/32"};str_cpy(text,sync[rate-128],sizeof text);}
        draw_column(0,"RATE",text,rate<128?"Hz":"",VR_LIVE,-1,ICON_AUTO);
        static const char *const shapes[]={"SINE","TRI","SAW","SQUARE","S&H"};
        static const char *const dests[]={"OFF","LP","HP","SPEED","PITCH","LEVEL"};
        uint32_t shape=(word>>8)&7u,dest=(word>>18)&7u,depth=(word>>11)&127u;
        draw_column(1,"SHAPE",shapes[shape<5?shape:0],"",VR_LIVE,-1,ICON_AUTO);
        fmt_int(text,(int32_t)depth);draw_column(2,"DEPTH",text,"%",VR_LIVE,(int32_t)depth*10,ICON_AUTO);
        draw_column(3,"DEST",dests[dest<TF_DEST_N?dest:0],"",VR_LIVE,-1,ICON_AUTO);
    }else{
    fmt_int(text,tu.pitch);draw_column(0,"PITCH",text,"",VR_LIVE,-1,ICON_AUTO);
    fmt_int(text,tu.speed);draw_column(1,"SPEED",text,"%",VR_LIVE,-1,ICON_AUTO);
    static const char *const lengths[]={"1/16","1/8","1/4","1/2","1 BAR"};
    draw_column(2,"LENGTH",lengths[tu.length<=4?tu.length:4],"",VR_LIVE,-1,ICON_AUTO);
    fmt_int(text,tu.mix);draw_column(3,"MIX",text,"%",VR_LIVE,tu.mix*10,ICON_AUTO);

    }
    if(tu.page)tape_fx_graph(theme);
    else if(ui.force||tiles!=tu.tiles_sig){
        tu.tiles_sig=tiles;cv_begin(240,graph_h(),T_BG);tape_ui_tiles();cv_blit(0,graph_y());
    }
    if(ui.force||footer!=tu.footer_sig){
    tu.footer_sig=footer;
    cv_begin(240,foot_h(),T_BG);
    cv_text(8,foot_line_y(),&AF_S,tape_seq_on?"SELECT PAGE / SEQ ON":"SELECT PAGE / SEQ OFF",T_TEXT);
    cv_blit(0,foot_y());
    }
}
