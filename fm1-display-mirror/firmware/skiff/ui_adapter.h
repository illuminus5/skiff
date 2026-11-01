/* Main-loop UI preparation. Capture actual graph pixels; never synthesize a graph. */
#pragma once
static uint32_t dm_now_ms(void){return fm1_ms;}
#if FELUCCA_DISPLAY_BULK
static int dm_ui_wait(void){return dm.ready && (uint32_t)(fm1_ms-dm.ended_ms)<16u;}
#endif
static void dm_ui_begin(void){
    if(!dm.ready&&!dm.band)
        dm.rows=FELUCCA_DISPLAY_BULK&&(ui.layer||ui.shift)?120u:240u;
    dm.graph_top=graph_y();dm.footer_y=foot_y();dm.footer_h=foot_h();
    dm.graph_height=FELUCCA_DISPLAY_BULK&&!ui.home&&!ui.layer&&!ui.shift&&!ui.menu&&
        !ui.confirm&&!name_on()&&!seq_counting()&&cur_page()->graph==GR_LFO?graph_h():0u;
#if FELUCCA_DISPLAY_BULK
    if(!dm.graph_height)dm_graph_cache.valid=0;
#endif
    if(dm_begin(fm1_ms))ui.force=1;
    if(dm.capturing&&dm.graph_dirty){
        if(dm.graph_dirty&1u)ui.graph_sig=~(graph_signature()+(ui.take_t?take_box.gen*2654435761u+0x7A6Bu:0u));
        if(dm.graph_dirty&2u)ui.head_sig=~0u;
        if(dm.graph_dirty&4u)ui.foot_sig=~0u;
        dm.graph_dirty=0;
    }
}
