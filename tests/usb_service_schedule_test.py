# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real TIMER5 handler with USB/input/clock hardware mocked."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/src/main.c').read_text()
handler = source[source.index('void fm1_timer5_irq(void)'):source.index('extern void isr_timer5(void);')]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#include "usb_audio_config.h"
#define FELUCCA_UAC 1
#define FELUCCA_UART 1
#define BOARD_SMK37 0
#define FM1_TICKS_PER_US 24u
static struct {unsigned timer_irqs, nested, in_audio;} felucca_dbg;
static unsigned fm1_ms, t5_nested_ticks, now, full, audio, uart, input;
static unsigned fm1_ticks(void){return now;}
static void fm1_timer5_ack(void){}
static void fm1_input_tick(void){input++;}
static void uac_service(void){audio++;}
static void usb_poll(void){full++;uac_service();}
static void uart_midi_poll(void){uart++;}
'''
suffix = r'''
int main(void) {
    /* Both normal and nested operation, including owed USB work after render. */
    for(unsigned phase=0;phase<3;phase++) {
        full=audio=uart=input=0;felucca_dbg.in_audio=(phase==1);
        for(unsigned i=0;i<100;i++) {now+=2400;fm1_timer5_irq();}
        assert(input==100 && audio==100/UAC_SERVICE_DIV);
        if(phase==1)assert(full==0 && uart==0);
        else assert(full==20 && uart==(phase==2?21u:20u)); /* owed UART poll after render */
    }
    assert(UAC_STALL_POLLS*UAC_SERVICE_DIV==200); /* still 20 ms */
    assert(fm1_ms==30);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'schedule.c';src.write_text(prefix+handler+suffix)
    for channels in (2,6,8,10):
        exe=Path(tmp)/f'schedule{channels}'
        subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra',
                        '-I'+str(root/'firmware/src'),
                        '-DFELUCCA_USB_6CH='+str(int(channels!=2)),
                        '-DFELUCCA_USB_CHANNELS='+str(channels),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        print(f'{channels}ch: real TIMER5 schedule, nested service, MIDI/control cadence and 20-ms stall timeout passed')
