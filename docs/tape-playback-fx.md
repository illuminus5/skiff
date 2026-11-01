# TAPE playback filter and LFO

Build the experimental FM-1 profile with:

```sh
FELUCCA_TAPE=1 FELUCCA_USB_6CH=1 FELUCCA_USB_CHANNELS=10 FELUCCA_CDC=0 ./build.sh --release 2.0.1-usb10fasttapefx
sh tests/run_tape_fx_tests.sh
```

Inside MENU > TAPE, SELECT selects TAPE, FILTER, LFO. The four knobs are:

- TAPE: pitch, speed, loop length, live/tape mix.
- FILTER: HP cutoff, LP cutoff, resonance, filter mix. HP OFF and LP OPEN bypass.
- LFO: free/synced rate, shape, depth, destination (OFF/LP/HP/SPEED/PITCH/LEVEL).

RATE spans 0.05–40 Hz followed by 4 BAR, 2 BAR, 1 BAR, 1/2, 1/4, 1/8, 1/16, 1/32. Shapes: SINE, TRI, SAW, SQUARE, S&H. The white marker follows phase with narrow-strip redraws. Depth zero is neutral. Filter mix is separate from the main tape mix.

Effects apply to playback and its USB TAPE wet output, never captured audio or the dry monitor. Filter/LFO controls are saved per project, rather than per-step locks; NEW resets them. Capture-only mode keeps the LFO running but skips filtering. The SKF4 reserved tail carries the TAPE metadata; default audio remains unchanged.

Existing step selection/SAVE/REC/lane-length/white-key and OCT actions remain available. FX > TAPE SEND (7/7) feeds tape from the selected instrument track.

Builds and software tests do not establish physical audio stability or CPU headroom. Test with a single track, gentle cutoff sweeps, then resonance and LFO depth, followed by dense arrangements and USB recording. No automatic flashing.
