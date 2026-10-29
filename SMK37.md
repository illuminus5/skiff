# Skiff on the M-VAVE SMK-37

Skiff runs on the **SMK-37 Pro**: the same six tracks, engines, sequencer and screens as on the FM-1, played from 37
velocity keys, with the 16 pads, four faders and two wheels doing more ([the controls](#what-the-build-has)). It is
tested on one SMK-37 Pro. The SMK-37 Elite and MKE-P37 builds are made the same way but have not been tried on a
keyboard; the Starrykey 37 Play is not supported yet.

## Install

1. **The keyboard must run M-VAVE's firmware V016 first.** An older one: update it with M-VAVE's own updater
   (M-UPGRADE), or on a Pro with `python tools/fm1_install.py SMK-37_Pro_016.fwsc` and M-VAVE's file. The
   installers refuse to put Skiff on an older one.
2. **Back up the keyboard's flash** ([below](#back-up-first)). It holds the keyboard's own key and pad calibration and
   its Bluetooth pairings, which no download can replace. Skiff never writes them, but a backup is the way back if
   anything else goes wrong.
3. **Install:** the [Skiff installer](https://illuminus5.github.io/skiff/webapp/installer/) (Chrome or Edge) finds the
   keyboard and picks its build, or `python tools/fm1_install.py skiff-2.0.3-smk37.fwsc` (`-elite`, `-mke` for those
   models, not tried on a keyboard yet). About two minutes.

Keep it plugged in and switched on while it writes. If the cable or the power is cut anyway, the keyboard stays in
update mode (the screen says UPDATE MODE): plug it back in or switch it on within two minutes, and the installer
carries on by itself; later than that, run the installer again.
Skiff's update writes the firmware's first sector last, so a cut write never leaves a half-written firmware to start.

## Back up first

This needs a computer with Windows or Linux, Python 3, this repository's `tools/`, and
[jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (`pip install -r requirements.txt` in its folder). It
reads the whole 1 MiB through the chip's built-in boot mode:

1. With the keyboard on M-VAVE's V016 (or on Skiff) and connected:
   `python tools/fm1_install.py --uboot skiff-2.0.3-smk37.fwsc`. The keyboard starts Skiff's update loader without
   writing the firmware, then switches to the chip's boot mode: the computer sees a USB disk named `WL82 UBOOT1.00`.
   **If Windows offers to format it, cancel.**
2. In an **administrator** terminal (on Linux: with `sudo`, and `/dev/sgN` as the device), from jl-uboot-tool's folder,
   read it twice:
   `python jluboottool.py --device \\.\PhysicalDriveN "read 0 0x100000 smk37-backup-1.bin"` (N: the disk number
   Windows' Disk Management shows for `WL82 UBOOT1.00`), then the same into `smk37-backup-2.bin`.
3. Check: both files are 1,048,576 bytes and identical, and the text `trimdat` sits at offset 0xC1224 (the
   calibration). Keep two copies, privately: the file holds data unique to your keyboard.
4. Switch the keyboard off and on: it starts its firmware again. If the computer's USB port then sees nothing at all,
   restart the computer (Windows can leave the port stuck after the boot-mode disk goes away).

## Back to M-VAVE's firmware

The installer's **Return to the official firmware**, with M-VAVE's unmodified `SMK-37_Pro_016.fwsc` (it saves a
complete backup of your Skiff music first), or `python tools/fm1_install.py SMK-37_Pro_016.fwsc`. Skiff writes
M-VAVE's firmware with its own update loader, which never touches the calibration (M-VAVE's own loader, started from
Skiff, can erase it: see "Back to stock" below). The calibration and the Bluetooth pairings stay.

Skiff first checks that the keyboard's start-up part (the first 16 KB, which M-VAVE's file carries too) is V016's.
If it is not (the keyboard came with another one, or a firmware before Skiff rewrote it), nothing is written and Skiff
keeps running; the installer says so (2.0.3). M-VAVE's own updaters stop at the same check while Skiff runs. Please
tell us on GitHub what the keyboard ran before Skiff (#64).

## If it does not start

- **The screen says UPDATE MODE, or a computer sees a MIDI device "Felucca Update":** an install was cut off. Run the
  installer again: it finishes the write.
- **A computer sees a USB disk `WL82 UBOOT1.00`:** the chip's boot mode. Switch the keyboard off and on. If it comes
  back to boot mode every time, write the firmware area back from your backup with jl-uboot-tool:
  `write 0x4000 app-area.bin`, where `app-area.bin` is bytes 0x4000..0x9CFFF of your backup (M-VAVE's firmware as it
  was), then switch off and on. Never write the first 16 KiB (0x0..0x3FFF), 0xC0000..0xC1FFF or 0xFF000 and up except
  from your own backup, and only to repair them.
- **The computer sees nothing at all:** another USB data cable, another port, and a restart of the computer.

The rest of this page is how the SMK-37 build is made and what it was tested on.

**Status:** since 2026-10-19 it runs on one SMK-37 Pro (below: "Bring-up so far"): installs both ways, a pulled cable
and a power cut mid-write, the calibration checked byte for byte after each; 2.0 installed over 1.4 the same way
(2026-10-24). Known in 2.0: an update loader started cold (by switching on after a power cut mid-write) is still reset
by the watchdog about 40 to 45 s into each run, though its flash now runs at the app's speed; the installer resumes it
until the write finishes. Its start record (below) names the reset. The sustain pedal's hold is tested on the host
only. The controls beyond the FM-1's (pads, faders, wheels, outer keys, the extra buttons, the pad colours)
follow the directions picked from the mockups in [design/SMK37_UX.md](design/SMK37_UX.md).

The SMK-37 Pro has the FM-1's chip (JieLi AC791N), audio path and 240 x 240 screen. Only the panel wiring and the flash
map differ: 37 velocity keys, 16 RGB pads, 8 endless knobs, 4 faders, two wheels, 18 buttons, and no LEDs under the
keys. The SMK-37 Elite and the MKE-P37 are the same keyboard under other names; each gets its own package, so its stock
updater still takes its factory firmware later. (The Starrykey 37 Play takes the Pro package.)

The board layer is genkigenki's SMK-37 port of Felucca (GPL-3.0-only), adapted to Skiff. Their
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit has the hardware notes, the backup and the way back to
stock. Credits: [LICENSING.md](LICENSING.md).

## Build

```sh
BOARD=smk37 ./build.sh                  # build/skiff-smk37.fwsc        SMK-37 Pro_700
BOARD=smk37 MODEL=elite ./build.sh      # build/skiff-smk37-elite.fwsc  SMK-37 Elite_700
BOARD=smk37 MODEL=mke ./build.sh        # build/skiff-smk37-mke.fwsc    MKE-P37_700
./build.sh                              # the FM-1 last: the tests read its binaries
sh tests/run_tests.sh
```

Everything SMK sits behind `BOARD_SMK37` (default 0). The FM-1 app, package and loader stay byte-identical. Release
builds take `--release X.Y` as on the FM-1: identity `SMK-37 Pro_7XY`, `skiff-smk37-X.Y.fwsc`.

Sizes (Pro, 2.0 dev with the controls, the pedal hold and the app's own copy of the loader): app 571,388 B of the SMK's
619,520 B slot (about 47 KB to spare); RAM .data+.bss 83,668 of 98,304 B; pool 347,088 of 356,352 B; the update
loader 7,859 B (the limit: 7,936 B, below).

## What the build has

- **Keys:** all 37 play. Keybed keys 5..31 (F3..G5) are the FM-1's 27 note keys. The ten outer keys (C3..E3,
  G#5..C6) play the notes below and above on the selected track, and do nothing in a layer, the DRUM grid or NAME.
  Each key plays at its velocity, measured with the unit's own calibration curves (read from USRTRIM, never written),
  through MENU > CONTROL > KEY VELOCITY: DYNAMIC (the default), SOFT, HARD, FIXED 64, 100 or 127.
- **Buttons and knobs:** the FM-1's 14 buttons on their printed SMK counterparts. K2..K4 are SELECT, ALGORITHM and
  PRESETS, and K5..K8 are KNOB 1..4. This is a bring-up default, and HARDWARE CALIBRATION (OCTAVE- + OCTAVE+ at
  power-on) re-teaches it. The knobs use Skiff's own detent decoder, with its frame constants retimed for the SMK's
  625 scans a second (the FM-1 does about 909). SHIFT's map names the controls as the SMK prints them (K4, BT,
  SEQ REC, ...), and MENU > LARGE's knob pills say K5..K8. The play button is printed with a play sign, which the
  screen's fonts don't have, so the map calls it PLAY.
- **MASTER on K1:** a virtual pot, 24 steps a detent. The header says its level while it turns (`MASTER 70%`), and
  the settings keep it once it rests 1.5 s, so it comes back at power-on.
- **The extra buttons:** KNOB BANK is SHIFT, as HOME held but on a button of its own: the map, + BT for TAKE, and
  + a layer's button to lock that layer. Let go, it does nothing (HOME still taps home, and held opens MENU).
  FADER BANK steps the faders' bank. < and > go to the selected track's entry before and after, at once when stopped
  and at the LAUNCH point when playing.
- **Pads:** always a grid. Where the FM-1's keys show a map of their own (a layer, the DRUM grid, NAME, SLICES, or a
  layer's button held), the pads are its 16 white keys, lit as the FM-1's key LEDs would be, in the track's colour.
  Elsewhere they are the selected track's 16 steps of the page shown. A tap sets an empty step to the last note
  played, at the pad's velocity, or clears a set one; on a DRUM track it toggles the selected lane's hit. A run of
  taps is one undo (`PAD STEPS`). The colours follow the screen's step tiles: an empty step faint white, a step in the
  track's colour, a tie at half, the edited step white. A pad has one LED, so the playing step can't get the
  mockup's ring: it shows half way to white instead. The pads' brightness is fixed.
- **Faders:** a mixer for the four tracks the screen shows (T1..T4, or the six-track window). FADER BANK steps
  LEVEL, PAN, REVERB and DELAY, and the header names the bank. A fader takes over only once it reaches the value,
  and again after something else has set it, so nothing jumps.
- **Wheels and pedal:** as MIDI in on the selected track's channel: a pitch bend (2 semitones, or what RPN set), the
  mod wheel as CC1 (the MOD matrix's MODW), the pedal as CC64 sustain. The pedal holds the keys too: while it is
  down, a note played on the keys keeps sounding after its key is let go, until the pedal goes up or the key is
  struck again (MIDI OUT gets its note-off then). A DRUM track's keys ignore it. The wheels calibrate themselves
  (after genkigenki's layer). When another track is selected, the one before gets them back at rest, and the notes
  the pedal held on it end. (2.0.3) KBD HOLD (ARP 2 › HOLD with the ARP off) holds the keys the same way; while the
  pedal is down, a new chord doesn't end what it holds.
- **Battery:** the SMK's own thresholds (530 / 560 / 602 raw), not yet checked on a unit.
- **Button LEDs:** on or off. The FM-1's glow, breath and mid levels have no SMK hardware: lit or breathing shows
  on, and the glow and the mid level show dark. MENU > LEDS is hidden. KNOB BANK is lit while held, and FADER BANK
  while its bank isn't LEVEL.
- **Sample slots:** USR1 only (layout A, below). SAMPLE's SET, GRAIN's SOURCE and SLICE's source stop at USR1, and
  the editor lists one slot.
- **Screen:** the SMK's pins and SPI group, with no backlight pin. SCREEN OFF only puts the panel to sleep.
- **MIDI:** USB as on the FM-1. TRS MIDI IN is off and the build refuses it, because its pin PH8 is a key-matrix row on
  the SMK, so MENU > MIDI > CLOCK offers INT and USB only (a saved TRS loads as USB). The SMK's jack is MIDI OUT
  (UART1 TX on PC7, 31250 baud). It sends what USB MIDI OUT sends, plus MIDI clock (Start, 24 pulses a beat, Stop)
  while Skiff runs on its own clock. The driver isn't checked on a unit yet.
- **Updates:** Skiff's own loader, with the SMK's LCD pins, so UPDATE MODE shows. It writes 0x4000..0x9CFFF only and
  drops update records only in 0x9D000..0xBFFFF. The firmware stages it at 0xBB000, and the record names the stock
  loader `ota-<model>_016`. An FM-1 package is refused on an SMK, and the other way round, before anything is erased.
  The SMK's boot stage starts a valid app even when an update record is pending, so the loader erases the app's first
  sector (0x4000, its header) before it writes any other sector, and writes that sector last. A write cut short then
  leaves no app to start. The next power-on should run the loader from the record, or at worst the mask ROM falls back
  to UBOOT. Both are recoverable.
- **Back to stock, with Skiff's loader:** M-VAVE's own update loader, once staged by Skiff, erased BTIF and USRTRIM
  (0xC0000..0xC1FFF, the factory key calibration) on the unit; the calibration was restored from the backup. So for
  M-VAVE's own package (its loader known by its CRCs) Skiff stages its own loader instead. That loader is a copy the app
  carries (`build/gen/smk_ldr.h`, the same bytes as the SMK package's `ota.bin`). The package's head must still equal
  the unit's, as before, and the record keeps the stock name `ota-<model>_016`. Skiff's loader writes the official
  flash.bin's app area 0x4000..0x9CFFF as it writes Skiff's, with the header last and the protected ranges refused, and
  ignores the package's other entries. **What it skips:** the package's USR image (its type-50 entry, for
  0xF4000..0xFDFFF). Skiff never writes USR, so a return to the same stock version the unit came with (V016) loses
  nothing; a return to another stock version keeps the USR image the unit had. M-VAVE's M-UPGRADE tool is not covered
  yet: Skiff's loader answers the same update protocol, but as `ota-<model>_700` on the USB device "Felucca Update",
  and whether M-UPGRADE looks for that is not checked (a hardware check).
- **The loader's size:** the stock app stages a loader at 0xBD000 below its record at 0xBEF00, 7,936 B. Skiff's SMK
  loader must fit that (the stock app staged genkigenki's smaller one without touching BTIF or USRTRIM), and the build
  fails above it (`build.py` LDR_MAX). It is 7,859 B, with the UPDATE MODE screen. The loader is compiled at `-Oz`,
  its flash helpers are calls rather than inlined copies (it all runs from RAM), and the flash entry points it never
  calls are left out (`fm1_flash.h`). The FM-1's loader is built the same way and held to the same limit: stock V15
  stages a loader at 0xE6000 below its record at 0xE7F00, the same 7,936 B (a V15 dump, 2026-10-20). It also writes
  the app's header last.
- **The loader's start record:** each start of Skiff's SMK loader keeps, in RAM a reset keeps (0x01C7FD20, 48 B at
  the top of the app's NOINIT; `app.ld` leaves it free): the chip's reset reason, whether the app started it (warm)
  or the boot stage did from the flash record (cold), a count of starts, and how far each run got (its stage, the
  sector being written, its time, its slowest erase), with the run before the current one alongside
  (`firmware/loader/ldr_crumb.h` has the layout). Two SysEx queries read it while the loader waits for a host, in the
  same framing as the identity query (`00 59 cmd`, no body, 7-bit packed):
  - `F0 00 32 6D 03 00 00 40 7F F7` (cmd 0x7B): the start record, 32 bytes;
  - `F0 00 32 71 03 00 00 40 7F F7` (cmd 0x7C): the clock registers, 8 words (SYS_DIV, CLK_CON0, CLK_CON2, PLL_CON,
    PLL2_CON0, PLL2_CON1, SFC_BAUD now, and SFC_BAUD as the loader found it at its start), to set a cold start
    against a warm one.
  The reply comes back as `00 59 cmd 20 00 00`, the 32 bytes, then a checksum, packed the same way.
- **Installers:** `tools/fm1_install.py` and the web installer's `web/fm1ota.js` install a package only on exactly
  its model (Pro, Elite, MKE-P37, FM-1), also when finishing a write a unit is stuck in (resume). Without the
  package's identity, nothing is written. `fm1_install.py` takes the unmodified stock SMK-37 Pro V016 package for the
  way back, and both read its 36-block container. The site (`web/make_site.py`) carries the FM-1 package and the
  three SMK ones; the installer page picks the one for the connected model, sends a stock SMK older than V016 to
  M-VAVE's updater first (the CLI: exit 7), and its return to stock takes `SMK-37_Pro_016.fwsc` (pinned checksum)
  as well as the FM-1's V15. A write cut short is resumed when the same model's loader answers again (up to five
  tries, two minutes each), and the bar moves forward only. `fm1_install.py --info` on an SMK loader prints its
  start record and clocks in words.
- **Console** (MENU > USB SERIAL, in a build without `--release`: Skiff 2.0's releases have no console): `smk`
  (inputs, the store's owner state, the calibration marker), `padtest`,
  `leds`, `flr 0xC1224 8` (should read `trimdat`).

## The flash layout (the storage remap)

The SMK lets Skiff write only:
- its store window, 0x9D000..0xBAFFF (30 sectors, 120 KiB);
- USRFLASH, 0xC2000..0xE9FFF (40 sectors, 160 KiB);
- the update staging area, 0xBB000..0xBFFFF.

It must never write 0x0..0x3FFF, 0xC0000..0xC1FFF (pairing and the factory calibration), 0xF4000..0xFDFFF or
0xFF000 and up. Skiff on the FM-1 keeps about 152 KiB besides its samples: nine A/B pairs (72 KiB) plus the 20-sector
pattern pool (80 KiB). That is more than the store window holds.

| What | FM-1 | SMK-37 (built) |
| --- | --- | --- |
| projects 1..4 (A/B) | 0x97000..0x9EFFF | 0x9D000..0xA4FFF |
| user preset banks (A/B) | 0xDC000..0xDFFFF | 0xA5000..0xA8FFF |
| autosave (A/B) | 0xE5000 / 0xE6000 | 0xA9000 / 0xAA000 |
| FM6 patches of the user presets (A/B) | 0x9F000 / 0xFE000 | 0xAB000 / 0xAC000 |
| settings (A/B) | 0xFC000 / 0xFD000 | 0xAD000 / 0xAE000 |
| owner tag `SMK37DATASKIFF` | none | 0xAF000 |
| spare | | 0xB0000..0xBAFFF (11 sectors) |
| update staging + record | 0xE0000..0xE4FFF | 0xBB000..0xBFFFF |
| user sample slots | USR1..3, 0xA0000..0xDBFFF | **USR1 only**, 0xC2000..0xD5FFF |
| pattern pool (64 patterns, 20 sectors) | 0xE7000..0xE8FFF + 0xEA000..0xFBFFF | 0xD6000..0xE9FFF |

**Picked (2026-10-19): A**, the layout built.

- **A: the pool moves into USRFLASH, and the SMK keeps one user sample slot.** Everything else keeps its size:
  four projects, the presets, the autosave, and all 64 patterns. USR2 and USR3 exist only on the FM-1; on the SMK
  they aren't offered, and the editor refuses to load them. 11 store sectors stay spare, for example for five more
  project slots later.

The alternatives not taken:
- **B: two sample slots, a smaller pool.** The spare 11 sectors would hold 7 to 10 chunks, so 28 to 40 patterns instead
  of 64. The pool's ids (track x 16 + slot) and its page would have to change.
- **C: two sample slots and the full pool**, by also using 0xEB000..0xF3FFF. That area isn't in the stock region table,
  but nothing shows that the stock firmware never uses it, and it lies outside the windows the port allows. It could
  only be considered after a verified flash backup shows that area erased, and after an explicit decision to widen
  the allow-list.
- **D: A, with the 80 KiB slot split in two** (two slots of 40 KiB, about 3.7 s each). This would need the editor to
  take the slot size the unit reports.

Rules the layout keeps:
- Every window is checked against the protected ranges at compile time (`fm1_flash_map.h`), and again in the RAM
  flash driver (`FL_GUARD`). Every object has a compile-time assert, and `tests/smk_flash_test.c` checks each one,
  A and B, by its `OBJ_*`.
- The first boot over another firmware's store (the stock firmware's, or Felucca's SMK port, family `FELUCCA`) erases
  the nine pairs and any pool sector holding a Skiff pattern chunk, then writes the tag. Samples check themselves.
- The stock firmware's own USRFLASH mapping stays intact until Skiff first writes there (a sample or a pattern save).
  Just before that, its end marker is erased, so that a return to stock rebuilds the factory mapping instead of
  reading Skiff's data as its own.
- Projects saved on an SMK stay on it: the layouts differ, so no flash image moves between an FM-1 and an SMK.

## The controls: what was picked

On 2026-10-19 every recommendation in [design/SMK37_UX.md](design/SMK37_UX.md) was picked: pads C (always a grid),
pad colours A (as the screen's tiles), faders A (a mixer), wheels A (as MIDI in), outer keys A (more range), extra
buttons A (KNOB BANK as SHIFT, < > the entries), button LEDs A (on or off), and the recommended answers to its
questions 8 to 17. "What the build has", above, describes each one as built. `tests/smk_ui_test.c` covers them on the
host.

Left for the unit, because the host can't show them:
- the pads' brightness and colours in daylight, and whether the playing step reads well without a ring;
- KEY VELOCITY's curves under real playing;
- the wheels' calibration and the pedal's threshold, with a sustain pedal of each polarity. The mod wheel's range is
  taken as about 474..629 raw, up lower, from genkigenki's layer; the first unit read 522 at power-on. If that was
  the wheel all the way down, CC1 would rest near 87 instead of 0;
- the faders: the first unit read raw 1023 on all four at power-on, so check that each one moves;
- the battery thresholds against a charger and a flat battery;
- TRS MIDI OUT into a synth: notes, clock, and no stuck notes on Stop.

## Bring-up, with the keyboard

1. A full flash backup, verified, before anything (the kit's recovery notes). Then install with
   `tools/fm1_install.py build/skiff-smk37.fwsc` (the Pro; `-elite` / `-mke` for those models).
2. Turn on MENU > USB SERIAL, restart, open the console (a build without `--release`: a release has none).
3. Check, in order: `smk` (adc_ok rising, adc_miss near 0; buttons and knobs one step a detent; pads idle at their
   base; faders 0..127; wheels move), `padtest` (the bottom row's first four: red, green, blue, white), every key
   C3..C6, a project saved and found again after a power cycle, `flr 0xC1224 8` still reading `trimdat`. Then the
   list in "The controls: what was picked".
   Then the same at MENU > SYSTEM > CPU CLOCK 320 MHZ: the switch lowers the LSB clock (60 to 53 MHz), which the pad
   LEDs' bit time (1.2 to 1.35 us) and the ADC's conversion are derived from.
4. The way back: `tools/fm1_install.py SMK-37_Pro_016.fwsc`, M-VAVE's own package for the unit's model.

## Bring-up so far (2026-10-19, one SMK-37 Pro)

- **Stock first:** the unit came with stock V012. `tools/fm1_install.py` put M-VAVE's official V016 on it, from a
  file checked against its pinned checksum. M-VAVE's loader writes from the top down, so its progress counts 91 to
  0 %, then shows 100 %.
- **The backup:** a full 1 MiB read, done twice with identical results, over the update protocol's UBOOT request
  (`F0 22 24 35 7D F7`) with genkigenki's snoop37 2.7.0 and jl-uboot-tool. The calibration marker `trimdat` is there,
  0x0..0x9CFFF equals V016's flash.bin, and key_mac reads all 0xFF. After UBOOT and a power cycle the unit booted
  stock V016 again: the update record was gone.
- **Skiff installed:** the 1.3.2-based Pro package (commit 73c9daa) was written by genkigenki's loader (V016 started
  snoop37, whose loader wrote Skiff's package).
- **First run:** the console says felucca v1.3.2, boots 1, CPU 14 % at idle, audio_late 0, 360 MHz, batt_raw 617.
  `smk`: 625 frames a second, the ADC ok, faders raw 1023 on all four, wheel0 512, wheel1 522, pedal 987 (open),
  store_cleared 1 (the first boot cleared the old store and wrote the owner tag), trimdat 1.
- The first hands-on verdict was positive.
- **Skiff's own loader (2026-10-20):** staged by the running Skiff (1.3.3, commit 4e4919e), it came up as
  `ota-SMK-37 Pro_700` on the port `Felucca Update 0` and drew UPDATE MODE. It took the UBOOT key, and a
  Skiff-to-Skiff install through it went 0 to 100 %. Afterwards: console v1.3.3, audio_late 0.
- **Update mode and a power cycle:** with an update record in flash and the app intact, a power cycle boots the app,
  not the loader. This happened twice: with genkigenki's loader over stock V016, and with Skiff's loader over Skiff.
  The backup, taken just after stock V016 staged its loader, shows where the stock firmware puts things: its loader
  at 0xBD000 and its record at 0xBEF00. Skiff stages at 0xBB000 with the record at 0xBFF00. Both locations behaved
  the same, and Skiff's SMK loader (about 8.6 KB) wouldn't fit the stock firmware's 7.9 KB between 0xBD000 and
  0xBEF00, so Skiff keeps its own.
- **For the install guide:** when the UBOOT disk vanished at a power-off, the PC's USB port stayed dead until Windows
  was restarted.
- **Round trip (2026-10-20, commit 85b0390):** Skiff to Skiff, Skiff to stock V016 through Skiff's own loader, and
  stock to Skiff (the stock app staged Skiff's 7,655 B loader): the factory calibration byte-identical at every step.
- **A power cut mid-write is safe:** switched off at about 14 % of a stock-to-Skiff write, the unit came back in
  Skiff's loader from the record, never in a half-written app.
- **A loader started cold reset itself.** The loader the boot stage started from the flash record after that
  power-on reset itself during each resume, at random points 15 to 34 s into the write pass, and came back as the
  loader within about a second. The fourth resume finished. Loaders the app started never did this.
- **The launch build (2026-10-20, grv e5150d7):** installed on the SMK-37 Pro and on an FM-1 next to it (the
  installers pick each unit by its model). A power cut 7 s into the SMK's write pass: the start record said the
  next, cold, loader was reset by the watchdog 20 s into its write pass (sector 0x47000), and that a cold start
  leaves the flash controller's divider (SFC_BAUD) at 0xB9 where a warm one has 1; the second cold start finished,
  the calibration byte-identical. The FM-1's loader, power cut 9 s into a stock-to-Skiff write pass, came back in
  UPDATE MODE and finished in one resume.
- **Its cause, from the start record:** the watchdog, about 20 s into a cold write pass. Of the clock registers, only
  the flash controller's divider differs: SFC_BAUD 0xB9 on a cold start (HSB / 186, about 1 MHz), 1 on a warm one
  (HSB / 2). The cold write pass ran about half as fast again per sector. The boot stage sets the app's divider on its
  way to the app, and the app's reset keeps it for a loader the app starts; a loader started cold gets the boot
  ROM's. Skiff's loaders (SMK and FM-1) now set it back to 1 at their start, when the clock tree is the app's, and
  feed the watchdog before every flash operation; 0x7C's last word reports the divider the loader found.
- **On the unit (2026-10-24, 1.4 launch build to 2.0 dev 0daf4b6, a power cut at 52 % of the write):** the divider
  part works (the cold loader's slowest 4 KiB erase 8 ms), but the cold-started loader was still reset by the
  watchdog about 40 to 45 s into each run: three cold runs, the start record after the last one `p3_rst 0x04`
  (watchdog), `starts 3`, the run before it writing sector 0x5F000 at 40.4 s. The installer resumed by itself each
  time and finished (exit 0); the calibration 0xC0000..0xC1FFF byte-identical to the backup. Warm-started loaders
  still never reset. Open: the loader arms P3's watchdog itself (0x0D, about 8 s) and feeds it about 2,000 times a
  second, so a cold start probably leaves that watchdog on another clock; the next step is to read P3's clock and
  watchdog settings on a cold and a warm start.
