# Skiff 2.0, Tier B: records

Tier B of the 2.0 plan changes what Skiff stores. Each piece says what it changes in a record, what it costs, how
older firmware sees it, and how it is checked. Piece 1 is per-note ties (#25); SKF4, the settings record and flat
parameter ids come later.

## 1. Per-note ties (#25): notes that overlap

Today a step holds the notes that start on it, and a TIE step holds every note of the step before. So a note can't
start while an earlier note on the same track goes on, and the notes of a chord all end together. START stops at the
end of the note before (STEP n TAKEN).

### What changes for the player
- A note can start while an earlier note on the track is still sounding. On a POLY track both sound. On MONO, LEG and
  UNI the new note takes over, as a key played over a held one does.
- Each note has its own length: the notes of a chord can end on different steps.
- START moves a note, or a chord, back past the end of the note before; that note keeps its length. LENGTH stretches
  a note through steps where other notes start.
- Limits:
  - At most 4 notes on a step, counting the ones going on.
  - A pitch can't overlap itself.
  - A step with lane hits (a melodic track's leftovers from DRUM) takes no per-note tie.
- Patterns without overlaps stay exactly as they are, byte for byte.

### The model
- A TIE step stays as it is: it holds every note sounding. 1.4's patterns load and play unchanged.
- A NOTE step gets a tie bit for each of its 4 notes: bit k says `note[k]` goes on from the step before. The step
  lists every note that sounds on it: the ones starting there and the ones going on.
- A step where every note sounding goes on is kept as a TIE. Per-note ties appear only where notes overlap, or where
  a chord's notes end on different steps (the shortest form, below).
- A tie never starts a note. A tied note goes on only if it is sounding; a start skipped by chance, a condition, a
  mute or a launch leaves its tied steps silent. This is how a TIE behaves today.

### Where the bits live
- **RAM:**
  - Placement: `step_t`'s `acc` byte, on a step without lane hits. `acc` is the lanes' accents only when there are
    hits; on a step without hits it is always 0 today (`acc` ⊆ `hit`).
  - Access: `step_ties(s)` is `s->hit ? 0 : s->acc & ((1 << s->n) - 1)`.
  - What has to keep them: the places that force `acc &= hit` (`project.c` `proj_bound`, the editor's STEP_SET)
    keep them on a step without hits. A lane's hit removed takes its accent bit with it, so a step without hits holds
    nothing but its ties.
  - Why not a new byte: `step_t` stays 13 bytes. One more byte a step costs about 1.9 KB of RAM over the step copies
    (838 B .bss, 544 B pool, 512 B .ramx for the undo ring), and .ramx has 496 B free.
- **Stored step:**
  - Byte 7 of the 9-byte step (`acc & hit`, always 0 without hits today) carries the ties the same way.
  - `step_get9` accepts them: the ties of the notes in use, only when the step has no hits.
- **Pattern record:**
  - It stays 888 bytes.
  - A record holding any per-note tie is written as version 3 (`BK_VER3`), which also covers `BK_VER2`'s motion ids.
    One without stays version 1 or 2. 2.0 reads 1–3.
- **Older firmware (1.4, and the 2.0-dev builds before this):**
  - It reads every pattern without overlaps as now.
  - It reads a version-3 pattern as empty and may reuse its number. Going back to 1.4, take the editor's backup
    first.
- **Backups:** the editor's backup and restore, and the web backup, move chunks as bytes, so they are unchanged.

### The sequencer (`seq.c` `seq_step`, `seq_tick`)
- **A NOTE step:** its notes are built as now (QNT SEQ snaps them).
  - A tied note already in `seq_notes` gets no note-on and no release: it goes on.
  - A tied note not sounding: nothing.
  - An untied note: a note-on (a slide glides as now).
  - What was sounding and isn't tied here is released (normally already, at its GATE: below).
- **The hold into the next step** (in the play direction). Today it is all or nothing (`seq_hold`, `next_tie`).
  - The next step a TIE, or SLIDE set: everything holds, as now.
  - The next step a NOTE with ties: the notes it ties hold; at this step's GATE `seq_tick` releases the others.
  - Otherwise: as now.
- **Release time:**
  - A note ending on the step it started: at the GATE, as now.
  - A note ending on a step it went on into: half a step after that step's GATE, as a TIE chain's last step does now
    (at GATE 50 %: the end of the step).
  - So a step can release in two goes.
- **CHANCE, condition and REST:** they gate a step's new notes; the notes going on go on. A TIE step's MISS still
  releases, as now.
- **RATCH and FLAM:** a step's new notes repeat; its tied notes go on untouched. A ratchet's own notes still never
  tie out.
- **RAND:** the next step isn't known, so nothing is held for its per-note ties: a tied note there finds nothing
  sounding and stays silent. A TIE holds as now.
- **State:** two masks a track (the notes held into the next step, the notes going on at this one) and the half step
  pending, outside `track_t` (its size stays as the ISR's indexing wants), in the pool: new .bss globals moved the
  others and changed code all over (+1.1 KB more, measured).
- **Unchanged:**
  - Launches, the song, STOP and panic release everything.
  - MUTE gates note-ons only.
  - The sequencer sends no MIDI notes, so there are no note-offs to track.

### NOTES (the D page)
Edits that touch ties work on a copy of the track's steps:
1. Unfold it: every TIE becomes the notes it holds, all tied.
2. Make the edit note by note.
3. Fold it back: a step where every sounding note goes on becomes a TIE again.
4. Write the copy back in one go, with the audio IRQ off, as a take does.

The ops:
- **The roll:** a note that goes on is one bar from its start through its steps (joined: no gap at the step line).
  Overlapping notes are bars on their own rows.
- **The cursor and the cards:**
  - PRESETS jumps steps where notes only go on, and only those that carry a note: a tie left over from a cleared note
    is an empty step to it.
  - The cards show the notes starting at the selection, as now.
- **START:**
  - Whole steps (nothing picked) move the step's notes with their ties, its values (velocity, FEEL) and its locks and
    automation. They may land on steps where other notes go on: the moved notes join them.
  - A step where another note starts stops the move (STEP n TAKEN).
  - Moving back past the end of the note before, that note keeps its length: the overlap.
  - Picked notes move as now (into the chord a step on, or onto an empty step with their step's values). They can
    also go onto a step where notes only go on.
- **LENGTH:**
  - STEPS are each note's own. The notes picked go on alone; with nothing picked, the chord's notes together, as now.
  - A note can go on through steps where other notes start, while each keeps 4 notes at most and no pitch twice.
  - 1–100 % stays the step's GATE: how long the notes ending on that step sound.
- **PITCH:** moves a note and its tied steps together.
- **Delete** (the user's ask):
  - EDIT (REC armed) clears the cursor step with its notes' ties. Today it leaves them behind, invisible and silent.
  - SHIFT + EDIT, REC armed or not, deletes the selection with its ties: the picked notes, or every note starting in
    the range. One undo.
- **Copy and paste:** a note's ties come along; a tie whose note isn't in the copy is left out. Pasted notes join
  steps where others go on (4 at most).
- **Step entry (#31):** a key held and PRESETS turned gives that note its LENGTH the same way.
- **FEEL:** a step's values, for the notes starting there, as now.
- **HOME, PATTERN, the SMK's pads:** a step where notes only go on shows as a tie.
  - `step_on` and `step_starts` mean "a note starts here".
  - A NOTE step whose notes are all tied counts as a tie.

### SEQ TOOLS
- **REVERSE:** each note starts at the other end of its span, its ties after it, as whole steps do now.
  - Built (step 5's commit) as note by note on the edit copy when a per-note tie is anywhere in LEN; else the
    whole-step REVERSE of before, byte for byte.
  - A step's values go with its notes' starts; a step's locks and automation go where its first note starts now (the
    others mirror; two of a parameter meeting: the one following a note stays). The TIE of step 1 goes.
- **RANDOM and COOK** (found with REVERSE): they changed every note of a step, tied ones too, cutting a long note
  from its ties. Now only the notes starting there change, each with the steps it goes on (`tl_chord_to`); two
  meeting on a step leave that step as it was.
- **EUCLID:** a hit on a step where notes only go on plays them as a new note, as on a TIE now.
- **SHIFT, DOUBLE, HALF:** as now (a note stays beside its ties).

### Recording
- Step entry: a note entered on a long note's step goes in beside it on POLY (step 3).
- **REC while playing** (POLY, forward; `seq.c`):
  - A key held while another starts on a later step goes on as its own pitch (`rec_hold_mix`): a per-note tie each
    step, beside what the step holds (4 at most; lane hits or no room: that hold ends).
  - A release before a step's middle takes its tie out of that step again (`rec_tm`).
  - A note recorded into a TIE goes in beside the notes the TIE holds (`rec_untie`).
  - One group held (a chord, or one note) writes TIEs as before. MONO, LEGATO, UNISON and the other directions are as
    before 2.0.
  - State in the pool: `rec_mix`, `rec_n` (each held note's steps, for the whole-pattern guard), `rec_tm`.
- **TAKES** (POLY, forward): a held note goes on to its own release, beside what each step holds (`ed_add` on the edit
  copy, folded back by `ed_close`); a note played meanwhile doesn't end it. Else as before (ties into empty steps
  only).

### The editor
- **Wire:**
  - A step on the wire gets a 17th byte, its tie bits (STEP_GET / TRACK_STEP replies). An older editor reads the
    first 16 and ignores it.
  - STEP_SET takes the new length with the ties. An old-length write clears the step's ties: that editor's step is a
    whole step.
- **The web editor:** its step table marks a tied note, and its patch files and library keep the ties.
- **Docs and tests:** `EDITOR_PROTOCOL.md` and `test_web.mjs`'s round trips.

### What it costs (estimates)

| Part | FM-1 flash |
| --- | --- |
| Sequencer (masks, two-step release, ratchet) and the record (byte 7, version 3) | 1.2 KB measured (step 2) |
| Unfold / fold | ~0.3 KB |
| NOTES ops (START, LENGTH, PITCH, delete, paste) and the roll | ~1.0–1.5 KB |
| The editor's byte | ~0.1 KB |
| **Total** | **~2.6–3.1 KB of ~40 KB free** |

- RAM: 0 B in the steps; 36 B of pool for the masks and the half steps; the edit copy (832 B) on the main loop's
  stack while an edit runs.
- The record stays 888 B.

### Order of work (a commit each, the full suite each time)
1. **The delete fixes, now (no format change):**
   - EDIT's clear takes its notes' ties.
   - PRESETS skips only ties that carry a note.
   - SHIFT + EDIT deletes the selection.
   - A test package for the unit.
2. **The format and the sequencer (no UI yet):**
   - The ties in `acc` and in byte 7, version 3, and the sequencer's per-note hold.
   - Host tests per rule:
     - C4 four steps with E4 starting on step 3: C4 one note-on and no retrigger.
     - A MISS on E4's step: C4 goes on.
     - MONO: E4 takes over.
     - A ratchet beside a tied note.
     - A skipped start: its ties silent.
     - Launch and STOP mid-hold.
   - The audio golden unchanged (no old pattern changes), and a 1.4-style reader (versions 1–2) reading a
     version-3 record as empty.
3. **NOTES on overlaps:**
   - Unfold / fold, START, LENGTH, PITCH, delete, paste, the roll's joined bars, the predicates.
   - `test_notes_sel` grows:
     - Your C4/E4 move.
     - A chord's note longer than the others.
     - The 4-note limit, the same pitch, lane hits.
     - SHIFT + EDIT.
   - Then a test package: you try overlaps on the unit.
   - Done as test_notes_overlap (+ the roll's notes_overlap scene). Measured: FM-1 +3.5 KB (more than the estimate:
     the edit copy's helpers and START's two ways). START keeps the steps-round of before when nothing else is on a
     whole step's span and the step beside is empty (locks, slides and ties' own fields move with it, as before); else
     note by note (a start step's locks move, its ties' fields don't). Step entry over a long note: POLY only (MONO
     replaces, as before). A step that can be a TIE is folded to one, so a pattern returns to 1.4's form once its
     overlaps are gone. REVERSE with per-note ties: step 5 (it lost their steps until then).
4. **The editor:** the 17th byte, the web editor, the protocol doc.
   - Done: INFO `57 01` caps bit 6; STEP_GET / TRACK_STEP send the ties after NUDGE, STEP_SET / TRACK_STEP take
     them (an old-length write leaves a step without hits none); the web editor's step table writes `(C4) E4`, its
     patch files keep the ties, a library pattern takes an all-tied step as a TIE (web/EDITOR_PROTOCOL.md "Per-note
     ties"). Measured: FM-1 −68 B (TRACK_STEP's length check through STEP_SET's took `ed_args_ok` out of
     `ed_handle`).
5. **Recording overlaps:** after step 3 on the unit.
   - Done with REVERSE, RANDOM and COOK on overlaps (above). Tests: ui_test test_tools_overlap, test_rec_overlap
     (POLY and MONO), motion_test's takes (two POLY cases).
   - Measured: FM-1 about +2.5 KB (REVERSE note by note 870 B, kept out of `ui_input` with its 1.7 KB of copies;
     `tl_chord_to` 426 B; the recording code about 1.2 KB). `rec_hold`, `rec_hold_mix`, `rec_untie` are not inlined:
     the audio ISR's loop cost went down, 7,780 of its 8,441 budget (inlined, `rec_hold_mix` tripled it).
6. **Docs:** DESIGN.md (NOTES, Patterns, this record), the Learn manual with 2.0's docs.
   - DESIGN.md done (SEQ TOOLS on overlaps, recording overlaps, TAKES, a pattern's per-note ties and version 3).
   - The Learn manual waits for the 2.0 docs pass: chapters 6–8 and the quick reference still teach 1.4's STEP, ROLL,
     CHANCE and PLAY, step by step with their videos' times, so NOTES and FEEL go in with new chapter videos.

### Open, with the defaults taken
- Under RAND per-note ties don't hold (the next step is a surprise); TIEs do, as now.
- Overlap patterns read as empty on 1.4: take a backup before going back.

## 2. The rest of Tier B: the decisions (2026-10-23)

The user's answers to the Tier B questions (the plan, i-want-to-be-dynamic-bunny.md):
- Tier B comes first, before more features: its room and its formats are what the features need.
- 2.0's features beyond what is built: #30 (a sound change on a song entry), #36 (DRUM per-lane pan), #20's preset name
  on HOME. Preset packs, #26 (free take, tempo detection) and #24 (BB Gen) wait; #35 is #21's per-sample mix
  (2026-10-24: and then the user's ask, own samples on the DRUM grid: pick A of design/mockups/v2_kits.png, KIT USR1–3,
  no record change: the KIT value is a parameter as before).
- Stored parameter ids (flat ids), one settings record of Skiff's own, the old readers out of the firmware with a
  converter in the browser, and no down-converter (the way back to 1.4 is the backup taken before 2.0).

What follows is the spec for that, for the user's approval before any code. Pieces 3–8 are formats; 9 is the order.

## 3. Old formats off the unit

### What 2.0 still reads
- Projects: SKF3 (written by Skiff 1.0 to 1.4 and 2.0's builds so far), and SKF4 (below, written from now on).
- User presets: versions 4 and 5 (Skiff's: the NUDGE in the record), and 6 (below).
- Settings: PER4 as Skiff writes it (its mark: `favorites.factory[15][24]` = 0xA5), and SKS1 (below).
- Patterns: the pool's records, versions 1–3, as now.

### What leaves the firmware (about 8–9 KB of flash, measured on e33)
- The project readers of FUN1–FUN9, SKF1 and SKF2, and the SKF1 writer (`proj_import`'s branches for them,
  `proj_unpack`, `proj_pack`: about 3 KB of project.c's 10.7 KB).
- What only old records needed: DIGITAL → FM6 (fm4_convert.c and `proj_fm4`, `fm4_*`, `ENG_FM4_GONE`, the hidden OP
  pages: about 3.1 KB), SAMPLE's PERC → DRUM (`proj_perc`), the GM drum part (`proj_drums_to_part`), PHYS MODEL's
  old meanings (`proj_phys`, `phys_legacy`), bank.c `proj_migrate` (0.6 KB).
- User presets 1–3 (Felucca's; about 1 KB of upreset.c) and the move of 1.0–1.0.2's FM6 patch bank into them
  (up_fm6.c `upf_boot`, `upf_migrate`; the rest of up_fm6.c, the user presets' own patches, stays).
- Settings PER1–PER3 (settings_persist.c's `old1` .. `old3`).
- None of it is lost: it moves into the converter's sources (below), where tests/project_test.c keeps testing it.

### The converter (web/conv/)
- The code above, as a host-only C file (firmware/src/legacy.c, never in a firmware build) with project.c, upreset.c
  and settings_persist.c's import, built for the browser with Emscripten as web/emu is.
- In: a complete backup archive (web/fm1backup.js: objects 0–9, the pattern bank 16–31, the samples 32–34). Out: the
  same archive with every object 2.0 can't read rewritten in 2.0's formats, and a report (what each object was,
  what it became, what was dropped).
- Also reads what no Skiff reads today: Felucca 1.4+'s projects (FUNA), and maps Felucca's user presets by
  parameter name (a table of Felucca's parameter orders by version, from its GPL sources), not by count.
- Run by:
  - the installer page (web/index_pkg.html), installing 2.0 over anything older: the backup it already takes, then
    the install, then the converted objects written back over the same connection (the editor protocol's BACKUP PUT);
  - the editor's Restore: an older archive is converted before it is written.
- From the command line (tools/fm1_install.py) 1.4.x stays the stepping stone: 1.4 converts everything older at its
  first boot, and 2.0 reads what 1.4 wrote.
- 2.0 never overwrites an object it can't read except by the user's own save into that slot: a slot of an older
  format shows as EMPTY and keeps its bytes until then, so the installer's restore (or a 1.4 boot) can still convert it.

## 4. Stored parameter ids

### The problem
- Records keep a track's parameters by position: the first `np - 8` in order, then the engine's 8 (`params_by_count`),
  plus SKF3's PX block for the ones added after the layout froze at 103. A parameter added anywhere but the end
  shifts every one after it for any record written by firmware that didn't have it.
- That is how Felucca 1.4+'s user presets land on Skiff's DIR and ducking, and the class of bug 56ea81f fixed for
  settings.

### The model
- Every parameter has a stored id (SID), 0–254, that never changes and is never reused. 2.0's SIDs are today's
  `P_*` positions (P_LEVEL 0 .. P_E7 134), so nothing is translated now.
- A parameter added later takes the next free SID (#36's lane pans: 135–142), wherever it goes in the enum. One
  removed keeps its SID, retired. `P_SID[P_COUNT]` (core.h, 1 byte each) and its reverse are the only table; while
  the enum and the SIDs agree it is the identity and costs nothing.
- The globals likewise (GSID: today's `G_*`, 27).
- A record stores parameters densely by SID: a count n, then n values (value + 64, one byte, as SKF3 stores them).
  A reader takes the SIDs it knows and skips the rest, so a record of a newer firmware still loads (its new
  parameters at their defaults), and one of an older 2.0 gets the new ones' defaults.
- Motion records (automation and locks) carry SIDs as their id, as they carry today's ids now (BK_VER2 for those past
  127): the same bytes for 2.0's parameters.
- The editor protocol keeps its own ids (unchanged).
- Built (step 1): core.h `sid_put` / `sid_get` and `gsid_put` / `gsid_get`. `sid_p` / `gsid_g` are the identity
  while the enum and the SIDs agree; they become the table when the two part. A reader refuses a record shorter
  than its count says, or holding a byte past 191 (a value no record holds, as SKF3's reader refuses it).

## 5. SKF4 projects

### The layout (2,560 bytes, one flash object)
The fixed-size parts come first, so the lists sit at a fixed place (`proj_slot_refs` counts a slot's pattern
references there); the counted parts follow, then zeros up to the name.

| Bytes | What |
| --- | --- |
| 0–7 | magic "SKF4", size |
| 8–9 | layout version (1: a reader refuses another), flags (0; a reader passes over them) |
| 10–393 | the lists: 6 x 16 entries x 4 bytes: pattern (id + 1, 0 empty, 65 REST), REPS byte (0–15, 16 CHAIN), the entry's sound (#30: 2 bytes, below; 0 0 until then) |
| 394–481 | the entry each track is on (6), the scenes (82) |
| 482–483 | sel, song (0 OFF, 1 ON, 2 ONCE) |
| 484–572 | the DRUM-X kit: its mark `58`, then 88 bytes (0: none, the factory's) |
| 573–1340 | the FM6 patches (6 x 128) |
| 1341– | the globals: GSID count, then their values (2 bytes each); then each of the 6 tracks: engine, preset, user slot + 1 (#20; 0 none), SID count n, n values |
| last 16 | the name (12, at 2544), the hash (4) |

- 2,246 bytes in use today, room for about 50 more parameters a track; the size is fixed at 2,560 so the retained
  cache (`proj_slot`, .noinit) can shrink from 4 x 3,840 to 4 x 2,560.
- `parts` and `phys` aren't stored: every project since 1.0 has `PROJ_PARTS` and `PROJ_PHYS`, which the reader sets.
- #30, an entry's sound: byte 0 = 0 none, 1..NENGINES a factory preset of engine byte0 - 1 (byte 1 its index), 0x80 +
  slot a user preset (byte 1 0). What it does when the entry comes in is the feature's spec, after Tier B.
- #36's lane pans are 8 track parameters (SIDs 135–142): no bytes of their own.
- A project holds no steps or automation (the pool does, as since SKF2): `project_t` drops `step[]`, the old chain
  and its motion store (`proj_scratch` 8,140 -> about 2,850 bytes). That waits for step 5: until the old readers
  leave, they decode steps into `project_t` (`proj_migrate` makes patterns of them), and `proj_slot` holds the older
  records they read.
- 2.0 reads SKF3 (by count and its PX block, as now) and SKF4, writes SKF4. 1.4 sees an SKF4 slot as EMPTY.
- Built (step 2): `proj_pack4` / `proj_unpack4` in project.c, reached through `proj_pack2` / `proj_unpack2` and
  `proj_fmt_of`. So every save writes SKF4 (SAVE, the autosave, `proj_migrate`, the editor's backup of the music),
  and a rename or a restore keeps a record's own format. The CHORUS reset of older formats passes over SKF4. Measured:
  FM-1 and SMK +1,192 B (the reader 706, the writer 434, inlined into `proj_import` and `proj_pack2`).

## 6. The settings record (SKS1)

- OBJ_SETTINGS gets Skiff's own layout: magic "SKS1", the panel calibration, the palette, the favourites (rows 0–14
  of `favorites.factory`, the real ones), and named fields for everything Skiff keeps in borrowed bytes today:
  CLICK / LEVEL / COUNT-IN, MENU's flags, SCREEN OFF, LAUNCH, PERFORM KEYS, the one-time changes (`ui_set_rev`),
  PRESETS' LIST type, HINTS, THEME CUSTOM's colours, PLAYHEAD, 2 CORES / STEP PREVIEW / CPU CLOCK, RETURN, the SMK's KEY
  VELOCITY and MASTER, the slot RESTORE LAST loads and its clean mark. Room for 32 more bytes.
- Read: SKS1; or PER4 with Skiff's mark (the borrowed bytes become the fields); or any other PER4 (Felucca's,
  JIANT's): only its own fields (panel, palette, favourites), Skiff's at their defaults. No more marks or guards in
  `settings_import`, and nothing in another firmware's bytes can read as a Skiff setting again.
- 1.4 refuses SKS1 and starts with its defaults (the calibration too): going back, restore the backup taken before
  2.0 (section 8).

### Built (step 3)
- **The record** (`settings_persist.c` `sks_t`, 656 bytes):

  | Bytes | What |
  | --- | --- |
  | 0–19 | magic "SKS1", then PER4's own fields as PER4 keeps them: palette, low cut, LEDS (in `zoom`), HOLD (in `bold`) |
  | 20–51 | the panel calibration |
  | 52–539 | the favourites of engines 0–14 (their stars only), the user presets' stars, the LIST filter |
  | 540–571 | Skiff's settings by name (`sks_skiff_t`): RESTORE LAST's slot and clean mark, CPU (2 CORES, STEP PREVIEW, CPU CLOCK), T1–T6 CHANNEL, the SMK's KEY VELOCITY and MASTER, THEME CUSTOM's flags, PLAYHEAD, RETURN, the unit's CLOCK / MIDI IN / TUNE, LIST type, HINTS, the one-time changes, PERFORM KEYS, LAUNCH, SCREEN OFF, CLICK .. FEEL, STYLE (retired), MENU's flags, the layers seen |
  | 572–593 | THEME CUSTOM's colours |
  | 594–623 | MIDI LEARN's map |
  | 624–655 | room: zeros, kept as read |
- The spec's list missed four of Skiff's borrowed fields; they are named too: MIDI LEARN's map, T1–T6 CHANNEL, the
  unit's CLOCK / MIDI IN / TUNE, the layers seen.
- **RAM keeps the bytes where they were** (`favorites.factory` rows 1, 14 and 15, ui.c's macros): only the record names
  them. A save packs them into SKS1 (`sks_pack`); a read unpacks an SKS1 into the PER4 it names and runs PER4's own
  checks, so both kinds are read by one path.
- **A PER4 without Skiff's mark** (Felucca's, JIANT's): its own fields kept, and Felucca's own settings at
  `[15][27..31]` (SCREEN OFF, CLICK / LEVEL / COUNT-IN, MENU's flags, the layers seen, as 56ea81f kept them, FEEL's bit
  cleared). Everything of Skiff's is at its default: row 14, `[15][0..26]` and MIDI LEARN's map (its parameter numbers
  aren't Skiff's).
- **The marks are gone at run time.** Once every read leaves Skiff's bytes either Skiff's or cleared, the checks of
  bank.c's mark (`SCENE_FREED`) before THEME CUSTOM, CPU 2 CORES, STEP PREVIEW, CPU CLOCK, RETURN and RESTORE LAST's
  bytes, and bank.c's one-time clearing of the old scenes, have nothing left to guard. All are taken out. The mark at
  `[15][24]` now only tells Skiff's PER4 from another firmware's, in `settings_import_per`.
- A PER4 read at boot is written as SKS1 at the next change of a setting (no write at boot). The editor's backup
  object 1 is SKS1; its restore takes SKS1 or an older archive's PER4, stored as SKS1.
- Measured: FM-1 +92 B (SKS1's code +350 B, the guards and the clearing −260 B); .bss +160 B (`persist_saved` and
  the backup's copy, 84 B each).

## 7. User presets, version 6

- The record as now (engine, name, the FM6 patch beside it) with its parameters by SID (section 4) in the same 144
  bytes; its 16-step pattern fields go (nothing reads them since phase 5).
- Read: 4, 5 (by count, as now) and 6. Felucca's 1–3, and Felucca 1.4+'s by name, only in the converter.
- 1.4 shows a version 6 record as empty and keeps its bytes.

### Built (step 4)
- `UP_VER_SID` 6 in the same 192-byte record and bank (UPB1, record size unchanged): the values by SID over the
  value area and the old pattern's 32 bytes (`sid[]`, 176 bytes, room for 41 more parameters); `np` is how many.
  `up_set_sound` writes one; SAVE › USER and the editor's `UP_PUT` both do (the pattern an editor sends is not kept).
  `UP_GET` of one answers 16 rests and kind 0.
- `up_params` reads it by SID (a newer record's ids passed over, an older one's at their defaults); `up_bank_check`
  leaves its bytes alone (no pattern to normalize, no PHYS DRUM / SAMPLE PERC migration: 2.0 writes neither).
- Versions 1–5 are still read until step 5 moves 1–3 to the converter.
- Measured: FM-1 +116 B.

## 8. Older firmware and the way back
- 2.0's projects, settings and user presets don't load on 1.4 (SKF4, SKS1 and version 6 are all new); patterns load
  unless they hold ties (version 3). Already the case for chains, SONG ONCE and ties.
- The way back is the backup the installer takes before 2.0: install 1.4.x, restore it. Work done in 2.0 stays in
  2.0's backups. No down-converter.
- The installer page says so before installing 2.0 over 1.x, and keeps that backup's file name dated.

## 9. Order of work (a commit each, the full suite each time)
1. SIDs (`P_SID`, GSID) and the dense parameter codec, with tests; nothing written differently yet. Done: c89a36c
   (FM-1 +0 B).
2. SKF4: writer and reader; 2.0 reads SKF3 and SKF4, writes SKF4 (`project_t` without steps and `proj_slot` at 2,560
   moved to step 5: section 5).
   Tests: SKF3 -> SKF4 round trips; a project of a newer layout (unknown SIDs) loads. Done: 2bc31a9 (FM-1 +1,192 B).
3. SKS1: the settings record and its import from PER4 (Skiff's and other firmware's). Done: section 6, "Built".
4. User presets version 6. Done: section 7, "Built".
5. The converter (section 10 has it in detail): legacy.c out of the firmware build, web/conv/ (Emscripten), the
   installer page's and the editor's Restore's use of it, tests on real old backups (Felucca 1.1.5.1 FUN9, Felucca 1.6
   FUNA, Skiff 1.0 SKF3); then the old readers out of the firmware, `project_t` without steps, `proj_slot` at 2,560,
   the flash and RAM measured in the commit. Done: the converter 40c47de, its page eb0e00a, the editor's Restore and the
   installer 6b6c88f, the old readers out e17b3d6 (FM-1 −8,716 B), Felucca 1.2+ c551e8e, the pages' report 73280c3
   (section 10, "Built"). Left: the installer's whole path on a unit, at your word.
6. Then the features: #36's lane pans (their SIDs, the DRUM engine's stereo split, the audio budget), #30 (the
   entry's sound, when it comes in), #20 (the preset's name on HOME).

### What it costs (estimates)
| | Flash | RAM |
| --- | --- | --- |
| Old readers out (3) | -8 to -9 KB | `proj_scratch` -5.3 KB (.bss) |
| `proj_slot` at 2,560 (5) | | -5 KB (.noinit) |
| SIDs, SKF4, SKS1, presets v6 (4–7) | +1.5 to +2.5 KB | |
| #36, #30, #20 (after) | about +4 KB | trk[] +96 B, the undo ring +128 B (.ramx) |

### Open, with the defaults taken
- SKF4's size: 2,560 bytes (the cache's saving), not the object's 3,840: enough for about 50 more parameters a track.
- #30's entry sound points at a preset (factory or user), as a MIDI program change does: editing the sound after
  doesn't change the entry. A snapshot of the sound per entry would need about 12 KB.
- Felucca's parameter names come from its GPL sources, into the converter only.

## 10. Step 5 in detail: the converter, then the old readers out (plan, 2026-10-24)

### What looking closer found
- **The installer's Install takes no backup.** Only "return to the official firmware" does (captureBackup, then the
  file saved). Sections 3 and 8 assumed the install already had one. So installing 2.0 needs a new step, a backup
  first, whether or not it converts anything: section 8's way back is that backup.
- **What a 1.x unit holds, 2.0 reads already:** SKF3, PER4 with Skiff's mark, user presets 4 and 5. Your 1.3.3
  archive of 2026-10-20 (`Downloads/Skiff-backup-2026-10-20.json`) is all of that: the music SKF3, settings PER4, one
  v5 preset, seven patterns, nothing to convert. Older formats are left over only as:
  - a FUN or SKF1 project that `proj_migrate` couldn't turn into patterns (the pool was full);
  - Felucca's user presets (versions 1–3), and DIGITAL sounds in any version;
  - Felucca's settings before PER4 (PER1–3).
  For most 1.x users nothing needs converting. The converter matters for Felucca and JIANT users, and old archives.
- **Felucca 1.4+'s FUNA** is documented in its own source, fetched here as `upstream/main` (to 2026-10-16): 3,840 bytes,
  four tracks, 104 parameters (1.5: 111), 9-byte steps with NUDGE in each note's bit 7, the song's sections, 128
  motion records of 3 bytes. Its tags (v1.2 .. v1.6.1) give its parameter orders by version, for mapping by name.
- **What leaves the firmware, measured on b72684f's FM-1 build:**

  | What | Bytes |
  | --- | --- |
  | DIGITAL to FM6: `fm4_convert` 1,710, `fm4_load_preset` 300, `proj_fm4` 340, `ENG_FM4_GONE` 188, `fm4_apply` 98, the `fm4_*` helpers and tables about 500 | ~3,100 |
  | FUN7–9 and SKF1: `proj_unpack` 824, `proj_pack` (the SKF1 writer) 630 | 1,454 |
  | `proj_migrate` 562 (and `bk_pack_raw` 346, if nothing else needs it) | 562–908 |
  | `proj_perc` 222, `proj_drums_to_part` 110, `proj_phys` 108, `drum_from_phys` / `drum_from_perc` 100, `phys_legacy` 16 | 556 |
  | `upf_migrate` 104, `fm6_bank_valid` 78, `up_migrate` 102 | 284 |
  | FUN1–6, SKF2, PER1–3, presets 1–3: inlined in `proj_import` (2,978 in all, SKF3 and SKF4 stay), `settings_import_per`, `up_params` | ~1,500 |
  | **Total** | **~7.5–8 KB** |

  RAM: `project_t` without `step[]`, the old chain and its motion store (`proj_scratch` 8,140 → about 2,850 B of
  .bss); `proj_slot` 4 × 2,560 (−5,120 B of .noinit).
- **The steps also pass through the runtime:** `project_capture` / `project_restore_runtime` copy them and the motion
  store (the backup's object 0, the autosave). With only SKF3 and SKF4 read they carry nothing (REST), so they go with
  `project_t`'s.

### The steps (a commit each, the full suite each time)
1. **legacy.c, nothing changed yet.** The old readers and conversions above move into `firmware/src/legacy.c`. The
   firmware still includes it (`LEGACY 1`), and the converter will too; the image the same bytes.
2. **The converter, in C** (`web/conv/conv.c`; `tests/conv_test.c` on the host). One archive object at a time:
   - **Projects** (0, 2..5): FUN1–9, SKF1, SKF2 → SKF4. Their steps, timing and motion become patterns in the
     archive's bank chunks (16..31), as `proj_migrate` does now. If the pool is full, that project stays as it is,
     reported. DIGITAL tracks → FM6, SAMPLE PERC and the GM drum part → DRUM, PHYS MODEL's old meanings, as 1.4 loads
     them.
   - **Settings** (1): PER1–3 → SKS1. A PER4 is left alone: 2.0 reads it.
   - **User presets** (6, 7): versions 1–3, and DIGITAL sounds of any version → version 6. A DIGITAL sound becomes
     FM6, its patch into object 9. Versions 4 and 5 are left alone.
   - **1.0–1.0.2's FM6 bank** (8): its patches into object 9 (`upf_migrate`), 8 emptied.
   - **A report:** each object, what it was, what it became, what was dropped.
   - Tests: project_test.c's FUN1..SKF2 builders move into conv_test.c; each converted object is then read back by
     2.0's own readers. Your 1.3.3 archive comes out unchanged.
3. **Felucca 1.4+ (new: no Skiff reads these today):**
   - FUNA projects → SKF4 + patterns, the parameters by name.
   - Felucca's user presets by name, not by count, which fixes the mapping noted in the plan (Felucca's LFO SYNC and
     the parameters after it land on Skiff's DIR and ducking today).
   - A table of Felucca's parameter orders by version, from `upstream` at v1.2, v1.4, v1.5 and v1.6.
   - The song's sections onto the lists as far as they map; Felucca's `song_chain.c` decides how.
4. **In the browser:** `web/conv/build.sh` (Emscripten, as `web/emu`), `conv.js` (an archive in; the archive and its
   report out), and `web/conv/conv_test.mjs` in the suite. `make_site.py` publishes `build/conv`.
   - **The editor's Restore:** an archive holding anything 2.0 can't read is converted first and its report shown,
     then restored as now.
   - **The installer**, installing 2.0 over older firmware that answers complete backups (Skiff, Felucca 1.0.3+):
     1. Take the backup and save it to the computer, dated, as "return to the official firmware" does, and say that it
        is the way back to 1.x.
     2. Install.
     3. When the unit is back: if the archive holds objects 2.0 can't read, convert those and write only them back
        (BACKUP PUT), and show the report.
   - A failed backup stops before anything is written. Firmware without complete backups (stock, JIANT) installs as
     now: none of its data is Skiff's to convert.
5. **The old readers out of the firmware:**
   - `legacy.c` leaves the build.
   - `project_t` loses its steps, chain and motion; `proj_slot` shrinks to 2,560 per slot.
   - `proj_migrate` goes, and engine 1 (DIGITAL) with it.
   - A slot of an older format keeps its bytes and is converted by the editor's Restore or the installer.
   - The flash and RAM measured in the commit.
6. **Docs:**
   - README: the way to 2.0 (the web installer converts; from the command line, 1.4.x first or the editor's Restore)
     and the way back (the dated backup, 1.4.x, Restore).
   - This file and EDITOR_PROTOCOL.md.

### Decisions for you (the defaults are what I'd do)
- **D1. The installer backs up before installing 2.0 over older firmware.** Default: yes. Section 8's way back needs
  it either way.
- **D2. After the install, convert and write back automatically**, only the objects 2.0 can't read, the archive saved
  first. The alternative is to leave it to the editor's Restore. Default: automatic, as section 3 says.
- **D3. A slot of an older format shows OLD FORMAT** (SAVE › PROJECT, USER) instead of EMPTY: its known magics,
  about 100 B. Default: yes; EMPTY reads as lost.
- **D4. FUNA's first cut:** the sounds, each track's pattern, the tempo, and the song's sections where they map.
  Default: yes; the rest reported.

### Risks and checks
- **The installer is the page a mistake costs most on.**
  - The backup adds about 10–30 s, and a failure mode that stops before writing.
  - The write-back only touches objects 2.0 shows empty, and the archive is on disk first.
- **Command-line installs (`tools/fm1_install.py`) convert nothing.** 2.0 keeps those slots' bytes; the README says
  so.
- **Tests:** the host (`conv_test.c`) and node (`conv_test.mjs`) tests do most of the checking. Their inputs:
  - archives synthesized with every old format;
  - your 1.3.3 archive;
  - FUNA records built from Felucca's source.

  On the unit, at your word: the installer's whole path, 1.4.x with old leftovers (Felucca-era presets, a project
  the pool couldn't take) → 2.0, then the way back.
- **The size of the work:** about as much as steps 1–4 together, more with FUNA. Steps 1–4 above land before step 5
  removes anything, so nothing is lost while it's in progress.

### Built: the old readers out (part 4, 2026-10-24)
- **One switch, not a file.** `FELUCCA_LEGACY` (core.h, 0) guards the old readers and conversions where they were.
  `web/conv/conv.c` sets it, and so do the tests of the old formats (project_test, motion_test, ratchet_test, fm4_*,
  descdump). Out of 2.0's build:
  - projects FUN1–FUN9, SKF1 and SKF2: the readers, the SKF1 writer, the chain and motion blocks, the conversions on
    load (the GM drum part, PHYS's DUST and DRUM, DIGITAL, SAMPLE PERC), `proj_migrate` and its boot call;
  - `project_t`'s steps, chain and motion (`proj_scratch` 8,140 → 2,852 B), and `proj_slot` at 4 × 2,560;
  - user presets 1–3, the PHYS DRUM and SAMPLE PERC migration, DIGITAL records (`UP_PUT` refuses one) and DIGITAL's
    conversion (fm4_convert.c after its tables, ui.c `fm4_*`), the favourites' conversions;
  - settings PER1–3; 1.0's FM6 bank (`upf_migrate`; a backup's id 8 only empty).
- **Kept:** engine 1's stub (`ENG_FM4_GONE`: the engine table is indexed by stored numbers), DIGITAL's OP ENV ids
  61–80, and FELUCCA_FM4's pages. The editor's SET G_ENGSEL 1 or PRESET of engine 1 gives FM6 (`set_engine_of`), and a
  project naming engine 1 isn't one.
- **D3, OLD FORMAT.**
  - A slot holding a project of before (FUN1..FUNA, SKF1, SKF2, by magic) keeps its bytes in flash and only its magic
    in RAM (`proj_fetch`). It counts as used: SAVE asks before writing over it and DELETE clears it, but it doesn't
    load. LOAD and NAME say OLD FORMAT, and SAVE › PROJECT and the editor's list show it as the name.
  - A backup takes such a slot from flash as it is (`st_current`), so the editor's Restore can convert it.
  - A restore keeps a whole one as it is (its own size and hash: `proj_old_whole`), for the converter to convert
    another time when the archive's patterns were full. A damaged one is refused.
  - User presets of versions 1–3 or DIGITAL: LOAD says OLD FORMAT, and SAVE asks first, naming it OLD FORMAT. SAVE ›
    USER's list still shows "--": the label there grew `draw_graph` by about 300 B (the giant-function effect: every
    form of it measured the same), more than it is worth for a case only a command-line install leaves.
- **A backup's project object is the record's own size** (SKF3 2,048, SKF4 2,560; 1.x gave a slot's whole 3,840), so
  an SKF3 slot from a 2.0 backup still restores on 1.4.x.
- **Measured** against 6b6c88f: FM-1 image −8,716 B (551,048), .bss −5,280 B, .noinit −5,120 B; SMK-37 image −8,332 B.
- **Tests:** upreset, settings, UI, persistence, bank, backup and robust run twice, as 2.0 is and as the converter's
  build (`*_legacy`); the project fuzzer runs under ASan both ways (2.0's seeds SKF4 and SKF3). New 2.0 checks: the old
  formats refused (an SKF1 or FUN5 slot, PER1–3, a DIGITAL or version-3 preset, `UP_PUT` of engine 1, a non-empty
  id 8); OLD FORMAT slots used, named, backed up byte for byte and deleted; a restore keeping a whole one; the SAMPLE
  preset 4 alias loading PIANO. The UI and audio goldens are unchanged.

### Built: Felucca 1.2+ in the converter (c551e8e) and the pages' report (73280c3)
- **FUNA** (Felucca 1.2 on: 3,840 bytes, the same from v1.4 to main) is read by `web/conv/felucca.c` straight into
  `project_t`, not through SKF1: SKF1 holds 103 parameters by count and no motion past them, which would drop exactly
  Felucca's newer parameters (Skiff's ids 117..126). Each track's steps and its own automation become a pool pattern
  as `proj_migrate` makes them; a slot is then SKF4, the music restored as the music's commit does it.
  - Parameters by name: ids 0..90 and the engines' E values are the same in both (checked at every tag); Felucca's
    own, in its order before P_E0, are LSYNC LTRIG LPOL QUANTIZE (np 103, a 1.2 build), SPREAD (104: v1.4, v1.4.1),
    INSERT TYPE A B C MIX, filter TYPE, ENV SYNC (111: v1.5 .. main), one table for every version. Values: MOD SRC's
    S&H and SLEW after Skiff's ENV2; FM6 SLOT B1..B32 as OWN (the record holds the patch); MIDI IN CH5-8 .. CH13-16
    as CH1-6.
  - Steps: NUDGE (sixteenths of a step, in the notes' bit 7) ×4 as Skiff's 64ths where Felucca played it; RATCH;
    1.5.5's conditions (chance 102..112) as TC_*. Motion: by the table, locks kept, each track's own up to 64.
  - The song (D4): each section onto the lists where it maps (a lane's pattern, the passes that fill it as REPS, a
    silent or empty lane a REST); SONG stays OFF (Felucca's starts from its SONG page).
  - User presets: in Felucca's archive (no pattern bank), a version 4 or 5 record of np 103, 104 or 111 becomes
    version 6 by name, its FM6 patch re-tied. Skiff's own records stay as they are.
  - `conv_note`: what had no place (ANALOG's filter TYPE BP / HP, NUDGE that did not play, automation of no Skiff
    parameter or past 64 on a track, 1.2's preset categories, MIDI IN TRACK, a song section that doesn't map, a track's
    own steps after the song), in each report line.
  - Open: (1) a track whose own steps are in none of the song's lanes is appended after the song (noted), so SONG
    ONCE would play one section more; (2) NUDGE under QUANTIZE ON is zeroed (Felucca never played it); (3) 1.6.1 and
    main moved five engines into a submodule not fetched here: their E values were compared at v1.6; (4) no archive
    from a real Felucca 1.6 unit yet.
- **What stays as it was** (73280c3): an old project the converter couldn't give patterns to (the archive's pool
  full) is reported "kept as it was: the patterns are full" and not written back; a 2.0 unit keeps it whole (OLD
  FORMAT) and converts it from a later backup once patterns are free. A damaged object is refused, and the editor's
  Restore leaves it out instead of stopping halfway. The editor and the installer show each line's note.

## 11. Step 6: the features (#20, #36, #30), spec (2026-10-24)

### #20: the preset's name on HOME
- What the ask still lacks: HOME names each track's sound (and since 1.3.3 its footer, when no lanes show), but a
  project load sets every track's user slot to none, and a sound from a user preset is stored as preset 0. After any
  load (a project, RESTORE LAST at power-on, the editor's restore) such a track shows its engine's first preset's
  name: "PIANO" for the user's "MY KEYS".
- SKF4 already has the byte (each track's user slot + 1, written 0 until now). A save writes the slot the track's
  sound came from (when that slot still holds a sound); a load takes it back when the slot still holds a sound of
  the track's engine, else none (the slot erased or reused since: the engine's preset name, as now).
- No screen changes. About 50 B.
- Built: 127d2fb. Then (the unit, 2026-10-24: a sound saved on SAVE › USER from T1, the unit switched off at once, T1
  back as ACID) SAVE › USER puts the track on the slot it stored to, as a LOAD would (another track on that slot
  comes off it), the autosave's signature holds each track's slot, and a save, a project load or NEW brings the
  autosave up to date within a second (project.c `autosave_soon`: idle 1 s, no gap). Then (the unit: a DRUM-X switch
  and a pan, off two seconds on, lost; the user's pick 1) every write sooner: idle 2 s, at most once every 20 s (1.2's
  10 s and a minute); the worst case, a change every 3 s for 8 h, 1,440 writes, about 140 such days a sector.

### #36: DRUM per-lane pan
- **Eight sound parameters**, PAN for each lane (`P_LPAN0..7`, -64 .. 63 as the track's PAN, C by default), for DRUM
  and DRUM-X (both have 8 lanes and use the lane LEVELs). They belong to the sound, as the lane LEVELs do: a kit
  preset centres them, a user sound keeps them. Automation and locks take them, and the editor as any parameter.
- **Ids.** They go before P_E0, as every common parameter since the first (the by-count readers rely on it: user
  presets 4 and 5, the patterns' automation). So P_E0 moves 127 -> 135, P_COUNT 135 -> 143. Their stored ids are new
  (SIDs 135..142) and the engine values keep 127..134: `sid_p` swaps the two blocks of eight, with no table. SKF3's PX
  block stays at its 32 (the lane pans aren't written to an SKF3: nothing written by firmware before them has any).
  The web editor takes P_E0 from INFO (wide ids) and keys library files by label: it follows.
- **The sound.** The track's PAN law per lane: the far side down, the near one as it is (a lane hard left is what
  that lane alone sounds like on a track panned hard left). Then the track's PAN balances the whole kit.
  - As SPREAD does it, the mono kit (every lane) goes through DRIVE, the INSERT, the SLICER and the sends exactly as
    now; the stereo is the clean difference beside it (two side sums: the lanes panned left, the ones panned
    right, each lane's part by its pan). A kit with every lane at C is bit for bit today's.
  - A kit's SPREAD (hidden: VOICE 2 is not on a kit; a value kept from another engine alternates whole hits left
    and right) plays as in 1.x until a lane is panned; from then on the lanes have the stereo, not SPREAD.
  - Cost: one multiply-add a sample for each panned lane sounding, two side buffers. The ISR budget is checked.
- **Where you set it** (letters below): **1A** a page **LANE MIX** after LANES (DRUM) and after SOUND (DRUM-X): KNOB 1
  LANE, 2 LEVEL, 3 PAN; the 8 lanes drawn as the MIXER draws the tracks (a fader, the pan under it). LANES and the
  SOUND sheet stay as they are. **1B** no new page for DRUM-X: its SOUND sheet's LEVEL row becomes LEVEL PAN DRIVE;
  DRUM's LANES takes PAN on KNOB 3 and TUNE DECAY move to a LANES 2.
  Recommended: 1A (one page, the same for both engines, nothing moves; a kit is mixed level beside pan).
- About 1.2 KB.

### #30: a sound change on a song entry
- **What it is:** an entry may carry a sound (a factory preset of any engine, or a user preset), as a MIDI program
  change: when the track comes onto that entry, it loads that sound, all but its own values (LEVEL PAN MUTE, ARP,
  SCL, LEN DIV SWING GATE, the SLICER, DIR, the ducking, as PRESETS loads keep them). An entry without one leaves the
  sound as it is. To come back to the first sound after the song loops, give the first entry one too.
- **When:** playing, exactly at the switch: the main loop prepares the sound when it stages the entry (the song's
  next, a PERFORM key, a scene: bank.c launch_q), the audio ISR puts it in at the same step boundary as the pattern
  (launch_swap), so the entry's first notes already play it. Stopped, at once. Not an undo step.
  - The prepared sound: every value, the engine, preset, user slot and an FM6 patch, about 430 B a track (2.6 KB in
    .bss: the .pool has no room for it on the SMK).
- **Stored:** SKF4's two bytes an entry, reserved for it (0 0 none; 1..15 a factory preset of engine byte - 1, its
  index in the second; 0x80 + slot a user preset). Firmware before 2.0 can't read SKF4 anyway. RAM: the lists get
  them (192 B of .pool; the SMK keeps more than its 8 KiB).
- **Where you set it** (letters below): **2A** SEQ › SONG's KNOB 2 runs on through two more picks, **SOUND** (OCT+:
  the track's sound now onto the entry, SET SOUND PIANO) and **NO SOUND**; an entry with a sound has a mark on its
  tile; with no pick the footer names the entry's sound. **2B** a page **SOUNDS** after SONG: KNOB 1 ENTRY, KNOB 2 the
  entry's sound, browsed as PRESETS (none, the engines' presets, the user presets); the rows show each entry's sound.
  Recommended: 2A (you choose a sound as always, with PRESETS, and hear it; then put it on the entry).
- The web editor shows and sets an entry's sound later (its Song tab's protocol, LIST2, gets the two bytes).
- About 1.5–2 KB.

### Order (a commit each, the full suite each time)
1. #20. 2. #36's sound and ids (the page after your letter). 3. #30's switch (its UI after your letter). 4. The UI
   picked; docs; a unit package.

### Built (2026-10-24)
- **#20** (127d2fb): as specified. FM-1 +52 B.
- **#36** (a06fa5d): the parameters, ids and sound as specified; a kit's SPREAD (hidden, kept from another engine)
  still spreads its hits until a lane is panned, so every 1.x kit sounds as it did. The web editor shows PAN / PAN 2
  for DRUM and DRUM-X and knows the 2.0 layout (143, 135). The ISR budget: lpan_render 11, lpan_mix 64, mix_spread as
  it was. FM-1 +1,080 B; .bss +1,104, .ramx +128 (372 B left there). The unit's page waits for letter 1.
- **#30**: the switch. lists.snd (192 B of .pool) and SKF4's entry bytes (+2, +3); COPY+ copies an entry's sound,
  CLEAR (and a REST cleared) takes it away. bank.c esnd_make prepares it as PRESETS or USER would load it (preset_fill,
  shared with apply_preset_to; a user slot's FM6 patch through upf_patch, its SLOT through fm6_match), into esnd[k]
  (.bss, 2.7 KB); stage_entry makes it with the entry, launch_swap puts it in (esnd_put: all but param_kept's values,
  engine, preset, user slot, the FM6 patch, a DRUM-X preset's kit; the lanes' mutes off; what sounds let go), the
  automation's base its values. An engine change fades from the swap's own block (this block's engine_block ran before
  the swap: the old engine renders on pe_old), 2.9 ms, the entry's first notes after it, as any engine change's while
  notes sound; a call of engine_block there instead tripled the ISR's static cost (its codegen). A note tied across
  into an entry with a sound ends there (launch_carry). Stopped: ent_go (esnd_go) at once. MOMENTARY's held values for
  the sound before stay out of it (mom_drop). An entry whose user slot was erased since, or whose engine the track
  may not play, plays no sound. The web editor shows and sets them: LIST2's entry sounds (INFO 58 01 bit 1, op 9).
- **The screens, the user's picks 1A and 2A (2026-10-24, "A for both"):** EDIT > LANE MIX for DRUM and DRUM-X
  (after LANES, after SOUND): KNOB 1 the lane (DRUM-X's the sheet's), 2 LEVEL, 3 PAN; LANES' faders 20 px shorter
  with the pans under them, each theme's own mark. Two things differ from the mockup on purpose: the LANE card names
  the lane in full as LANES' does (SNARE, the mockup SD), and the PAN card is -31 % as every PAN card (the MIXER's);
  under the faders the pans are written as the MIXER's strips write them (L31). A key struck picks its lane, the
  same key again too (seq.c last_hits: the DRUM-X sheet followed only a different note). SONG: KNOB 2 runs on past
  CLEAR to SOUND and NO SOUND; SOUND refuses a sound edited since its load (snd_of: SOUND EDITED, SAVE > USER
  FIRST); the cut corners and the footer's name as drawn. FM-1 +2,388 B for both screens (555,556 B).
