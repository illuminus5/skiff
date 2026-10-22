# Independent track FX feasibility

Current status: the compact prototype has been removed at user request; see
[USB FX routing](usb-fx-routing.md). The measurements below explain its limits.

Existing independent INSERT effects (fx.c ins[NTRK]) already provide per-track
phaser, flanger, chorus, drive and bitcrushing. The USB track taps follow them.
The shared chorus, tempo delay and reverb in fx_buses use one combined send
input each; distributing their output cannot recover independent wet stems.

The working six-channel build uses 346,316 of 356,352 bytes of pool and 89,844
of 98,304 bytes of data/bss. One existing shared buffer set uses:

- Delay: 65,536 x 2 = 131,072 bytes, maximum approximately 1.49 seconds.
- Chorus: 2,048 x 2 = 4,096 bytes.
- Reverb: (1,116+1,188+1,277+1,356+556+441) x 2 = 11,868 bytes.
- Total buffer bytes: 147,036, plus state and work blocks.

Six full sets need 882,216 bytes of buffers; adding five requires 735,180 bytes
against 10,036 bytes free in pool. An OFF switch removes computation, but cannot
make simultaneous instances fit. Dynamic allocation does not solve the bound.

Potential bounded designs, requiring a user choice before reducing effects:

1. Compact independent mode for all six tracks. Reuse shared FX buffer memory
   as a union rather than append full new buffers. For example, 4,096 delay
   samples per track plus the current chorus and reverb buffers totals 144,936
   bytes for six sets. This preserves the existing chorus/reverb buffer lengths
   but caps each delay at approximately 92.86 ms, far shorter than many tempo
   divisions. A longer delay requires simpler/smaller reverb or fewer instances.
   All limits must be visible, never silent tempo clamping. CPU feasibility is
   unmeasured: six reverbs are a major additional load. Process active instances
   and audible tails only. OFF consumes no FX processing; reserved memory may
   remain. Audio must be unaffected by whether a host starts USB capture.
2. One selected track owns the existing full effect set. Other tracks retain
   existing per-track inserts, but cannot simultaneously have their own full
   delay/reverb. This preserves delay range, not six independent instances.

Implementation prerequisites for independent mode:

- Separate each track's sends before their cross-core merge, without rerendering
  voices. Give each CPU exclusive ownership of its track capture buffers; join
  before effect processing, avoiding races in current global fx/hl state.
- Make effects state/buffers context-owned, including all model-transition and
  clear paths, rather than time-sharing the current global history unsafely.
- Explicit shared/independent mode with shared as default. Mode changes should
  be bounded, with a ramp/stop policy and no large memory clear inside an audio
  deadline. Keep existing shared output bit-for-bit for the default mode.
- Per-track parameters and saved settings need a specified versioned layout;
  do not silently change existing projects or reuse unspecified bytes.
- Track USB dry/wet choice and FX-only sources must include only that track's
  instance when independent. In shared mode expose only a combined return.
- Metronome routing is a separate tap: render once, share its buffer, prevent
  double advancement or feedback. USB and local click gain/routing are explicit.
- Measure worst-case hardware CPU and USB service headroom at both CPU settings.
  Host builds or estimated MAC counts are not proof that six instances run.
- Test isolated impulse tails, cross-track silence, wet/dry unity, parameter and
  mode changes, state restore, one/two-core equivalence, overload behavior, and
  shared-mode regression. Do not flash automatically.

No independent delay/reverb implementation has been added on the strength of
this feasibility note. The working six-channel firmware remains unchanged.

Implementation update: the bounded compact prototype was removed at the user's
request. The current simple build keeps only the existing shared REV, DELAY and
CHOR return sources. See usb-fx-routing.md. Older experimental packages remain
separate artifacts; their compact processing is not compiled into the new build.
