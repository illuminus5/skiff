# Design

How this firmware looks and behaves. Every screen and feature follows this page. If something here is wrong, change this page first, then the code.

Each rule comes from what FM-1 owners said about the stock firmware, Felucca and the other community firmwares, in GitHub issues and Discussions, forums and reviews. The issue numbers in brackets are Felucca's unless marked.

## Rules

1. **The K1–K4 legend is one row of four, in knob order, at the top of the screen,** because the physical knobs sit top right. Never a 2×2 grid.
2. **Every screen labels every live control:** the four knobs on its cards, the page as n/m, and (U5) what every encoder, button and key does here on SHIFT's map (HOME held). An unused knob is an empty tile (U4: one empty look; "--" means nothing picked), a value only read (PATTERNS' LENGTH, USED) has no gauge and is MID. (D#80)
3. **Encoders have one job everywhere:**

   | Encoder | Job |
   |---|---|
   | ALGORITHM | Track |
   | PRESETS | Sound, the step cursor on SEQ pages, or the list selection (DRUM-X's SOUND sheet: the drum sound; FM6's OPERATOR and ALL OPS: the operator) |
   | SELECT | The page before or after in the family |

   Tempo changes only while GLO is held, so it can't be bumped by accident. (#58, D#84, D#128)
   - A new unit starts with BPM LOCK on, and (U1) a unit coming from Felucca gets it turned on once at its first boot (`main.c`, `ui_set_rev`); the MENU can turn it off again.
   - **SELECT turns pages** (phase 8; `ui.c` `page_step`, `ui_input.c` `select_turn`): either way, a detent a page, in the order the family's button steps them, stopping at the first and the last (no wrap). GLO's MIXER, DELAY, REVERB, CHORUS and GLOBAL are one family (U3). The button still steps forward from wherever SELECT left you. On a sheet (DRUM-X's SOUND, FM6's OPERATOR and ALL OPS; `ui.c` `sheet_t`) SELECT moves the knobs down the sheet first, then on to the page beside it.
   - On HOME (no pages) SELECT still says BPM LOCKED; with a layer held it does nothing to the page under it. With BPM LOCK OFF, SELECT is the tempo everywhere, Felucca's way, except on the sheets, where nothing else reaches their rows (U1).
4. **There is one modifier model: layers.** A button held for longer than the hold time (MENU > HOLD, every button's) shows its map, and SHIFT + the button keeps it open (U2: it was a double tap, which fast page flipping set off).
   - HOME held is **SHIFT**. Held alone a moment, its map covers the panel under the page's cards (U5, the review's A1; `ui_layer.c` `draw_shift`): what every control does on this page, 4 rows of 3 cells as the layer maps, this page's own jobs in TEXT (NOTES: PRESETS STEP, KEYS PICK, KEYS + REC WRITE, EDIT + REC CLEAR, + EDIT DELETE), the ones the same everywhere in MID, the SHIFT combos lit (+ SAVE TAKE, + REC REC SET, + BUTTON LOCK); the header's title says SHIFT, the footer LET GO: MENU. A combo ends it; HINTS OFF shows none.
   - HOME held alone and then let go opens the MENU, on release, so it never races a combo.
   - A tap never flashes a layer screen. (#40, #59, D#83; Sloop #1, #46)
5. **Knobs change only what is on the screen.** A turn made while a layer opens or closes is ignored. (#39, #102)
6. **LEDs tell the truth:**
   - The lit button is the current page.
   - Idle buttons glow (MENU > LEDS), so the black unit's labels stay readable.
   - LEDs breathe, never blink (U6: NAME's cycling key too).
   - Nothing is shown only on an LED. (#35, #119; Sloop #10, #33, #62)
7. **Destructive actions:** a dialog with CANCEL (OCT-) as the default, or a hold the screen names while it runs (the REC layer's CLEAR: HOLD: CLEAR, then held for the hold time). Undo where the firmware can (SAVE held), and say CAN'T BE UNDONE where it can't. Clearing still has to be quick. (D#91)
8. **Never lose a jam.** The session comes back at power-on (MENU > SYSTEM > RESTORE LAST, on by default). (D#130; Sloop #20) (2.0) A save or a project load brings it up to date within a second, so switching off right after one keeps it.
9. **Legibility floor:**
   - Values in large numerals; no hairline glyph details.
   - True-black and high-contrast palettes.
   - Colour is never the only cue: a track colour always comes with its number.
   - Animations are short and ANIM OFF turns them off. (D#80, #46, #50)
10. **HOME is the all-tracks overview:** a lane per track with its steps in its colour, the playhead, and bar.beat in the header. The selected track is a band of its colour. (D#112, D#134)
11. **One line of hint** says what to do next, and it can be turned off (U4: MENU > DISPLAY > HINTS; an action page's OCT+ / OCT- line stays, it says what they do now). (D#88)

Things to avoid:
- a knob turn used as enter or confirm;
- a button whose meaning changes from screen to screen;
- jargon names (MOTION became AUTOMATION);
- text that only fits when ellipsised;
- SELECT dedicated to tempo.

## Controls

| Control | Tap | Hold | SHIFT + it |
|---|---|---|---|
| ENV, LFO | Open its page family; again: the next page | As a tap | — |
| ARP | ARP pages | PERFORM layer: the list's entries or the scenes on the white keys, mutes on the black, throws on K1–K4, LAUNCH on SELECT | Lock |
| SAVE | Its page family (PRESETS, USER, PATTERNS, PROJECT): the page it was left on, USER the first time (U3) | **UNDO**, one level back (8 levels kept) | — |
| FX | FX pages | FX layer: effects on the white keys; on the black the tracks' MUTE as GLO's ((2.0, D 1A): a tap latches it, held the track is cut; see One mute), macros on K1–K4 | Lock the layer open; locked, it latches (U2: MENU > FX LATCH retired) |
| GLO | MIXER, DELAY, REVERB, CHORUS, GLOBAL (U3: the buses, FX's before; SYSTEM gone) | GLO layer: mutes, solos, tap tempo; with SELECT: tempo | Lock |
| SEL | SCALE, CHORD (1.2.1, #8: the button is printed SEL; the page was SCL) | SEL layer: root on the keys | Lock |
| EDIT | EDIT pages, on every page (U2; on NOTES with REC armed it clears the cursor step, with a step held its locks) | EDIT layer: engine on the keys | Lock |
| SEQ | SEQ pages | SEQ TOOLS, on every page (U2) | Lock |
| REC | Arm, on the press (U2); on an armed track every track disarmed (1.4.1) | — | REC layer, locked: CLEAR (held), CLICK, COUNT-IN, LEVEL, FEEL |
| HOME | HOME (a locked layer closes); on HOME, the tracks or the scope; with a layer's button held: locks that layer (1.2.1, #13) | **SHIFT**. Let go alone: MENU (not over a dialog or NAME, U2) | — |
| PLAY | Start / stop | — | — |
| OCT− / OCT+ | Octave. In dialogs and on action pages: OCT− cancel (or HOME, as its footer says), OCT+ do it. While the undo box (or message) shows: OCT− undo further, OCT+ redo (on action pages too, U2) | Both for 5 s: UPDATE MODE | — |

**SHIFT combos:**

| Combo | Does | Arrives in |
|---|---|---|
| SHIFT + SAVE | **TAKE:** keep the last loop of notes and knob moves on the selected track (`take.c`, `ui_take.c`) | Phase 2 (done) |
| SHIFT + PRESETS (NOTES, FEEL) | **RANGE:** the selection stretched from where it was (`ui_input.c` `sel_shift`) | 2.0 (D) |
| SHIFT + OCT− / OCT+ (NOTES, FEEL) | **COPY / PASTE** the selection (`sel_copy`, `sel_paste`) | 2.0 (D) |
| SHIFT + EDIT (NOTES, FEEL) | **DELETE** the selection, REC armed or not: the picked notes, else every note starting in it, with their ties; one undo (`sel_delete`). Elsewhere SHIFT + EDIT locks EDIT's layer, as any layer's button; here EDIT held + HOME does | 2.0 |
| HOME held alone, then let go | MENU (settings). It opens on release, so it never fights a SHIFT combo. | Phase 1 |

**TAKE in detail:**
- **What it keeps:** the last *complete* pass, notes and knob moves together.
- **What is logged:** while the track plays with REC off, the keys and their chords, MIDI and the arpeggiator (everything REC would record) and the knob moves are logged.
- **How notes merge:** as REC merges them. A held note ties into empty steps only, so a take never wipes a note. (2.0, #25) On a POLY track playing forward a held note goes on by itself to its key's release, a per-note tie beside whatever each step holds (4 notes a step, no pitch twice; no room: it ends there), and a note played meanwhile doesn't end it; a note played into a long one goes in beside it.
- **Undo:** one undo (hold SAVE) puts back the steps and the automation.
- **On a chain** (2.0, #44): a TAKE keeps the chain's last whole round, each part's pass into it (`bank.c` `take_part`), and a note held over a switch goes on into the next part's first steps. Each part gets its own undo copy: the part shown first; the others' are undone on them (elsewhere: UNDO NEEDS PATTERN 07). PLAY ONE LOOP FIRST until a whole round has played.
- **What it says:**
  - **BLOCKS:** the TAKE box sits over the panel for about 2 s. It shows the track's number on its colour, TAKE KEPT, what went in (14 NOTES, 3 KNOB MOVES; AUTOMATION FULL when the moves found no room), and [SAVE] HOLD TO UNDO. A newer message, an undo for example, ends it.
  - **Views with no panel for the box** (a layer, LARGE's tall cards, the MIXER): TAKE T1 in the header, then HOLD [SAVE]: UNDO (U4: one wording for the undo hint).
  - **Partial:** TAKE PARTIAL (TAKE T1 PARTIAL in the header) when more than about 128 notes in the pass overflowed the log.
  - PLAY ONE LOOP FIRST, NOTHING PLAYED or DISARM REC FOR A TAKE when there is nothing to take (U4: the way out, not just the problem).
- **Hint:** while HOME is held, the header says [SAVE] TAKE.

Add a combo here before adding it to the code. Every SHIFT combo has to appear on the SHIFT map.

**UNDO in detail** (`ui.c` `undo_ring`, `undo_step`):
- **What's kept:** the last 8 undoable acts, on any tracks. A copy is taken by a sound load, a take, a clear, a SEQ TOOLS action or an automation edit. Loads in a row while browsing count as one. Pattern loads, launches and the song take none: the pattern left is kept as it was.
- **How:** SAVE held goes one back. Then, while the undo box shows (2.7 s, `UNDO_WIN`), OCT− goes further back and OCT+ forward again. A new edit after undoing drops the redo.
- **A copy belongs to its pattern:** taken while the track was on another pattern, SAVE held says UNDO NEEDS PATTERN 07 (its number) and changes nothing.
- **What it says:** the box (BLOCKS) or the header names the act and its track (UNDO T1 TAKE) and how far back you are (2 OF 5 UNDONE). The keys dim when there is nothing more that way.
- **Not undoable:** project and user-sound saves, a project load, a scene stored over another (U2: its dialog), erasing a sound, clearing an entry or a pattern. Their dialogs say CAN'T BE UNDONE. (U1) EDIT's step clear on NOTES and the grid takes a copy: a run of them (under 1.5 s apart, nothing else changed) is one. (U2) A recording pass is one: REC arming takes a copy (RECORDING), dropped again when it recorded nothing.

**FLAM** (`seq.c`): RATCHET's fifth value on SEQ › FEEL (2.0; KNOB 2: X1–X4, then FLAM; SEQ › CHANCE's knob 3 before). The step plays a grace hit at 9/16 of its velocity, then the full hit 15 ms later (at most half the step). Chords and drum hits flam too; the chance is rolled once for both.

**SEQ › NOTES and FEEL** (2.0, the user's D: design/mockups/v2_notes.png; STEP, ROLL, CHANCE and PLAY before). PRESETS moves the cursor on both, so no knob is the step any more; it jumps a long note's ties (back onto its first step, on past its end), and the header shows the cursor's step (NOTES 5). The footer says so, PRESETS: STEP (the SMK: K4: STEP), then on NOTES the keys' PICK or WRITE; SHIFT's map has the combos (RANGE, COPY, PASTE, DELETE).
- **The selection** (`ui.c` `nsel`): the cursor's step, or a range: SHIFT + PRESETS stretches it from where it was (a SHIFT combo; the header says NOTES 5-8). With REC off a key picks its note in the selected steps (again: dropped; its row lit on the roll's keyboard strip); none picked: every note. The cursor moved any other way drops it.
- **NOTES**, the piano roll (Felucca's STEP page): PITCH, START, LENGTH, VELOCITY of the selection as a unit; nothing selected (an empty step, a tie): the cards read --, and the knobs write nothing (notes go in with REC or a paste). PITCH walks the track's SCALE (CHROMATIC: semitones), each note with the steps it goes on; one note off the keyboard, or onto a note that stays there, and none moves. START moves whole steps (nothing picked, or whole chords): their notes, each as long as it is, with the step's values, locks and automation (with nothing else on their steps and an empty step beside them, the steps round as before 2.0); picked notes go into the chord a step on or onto a step where none starts, with their step's values. **Notes overlap** (2.0, #25, design/TIER_B.md): a note moved back past the end of the note before goes in beside it, and that note keeps its length; a step where another note starts stops whole steps. Something in the way: nothing moves, STEP n TAKEN. LENGTH is 1–99 % of the step (the GATE lock below: the step's, for the notes ending there), then 1 STEP (100 %; (2.0, the user's standard) where a new pattern's notes start: PATTERN's GATE is 100 % by default, 50 % before), then 2–128 STEPS ((2.0.2, #58) 2–16 before, though REC tied further; past its pattern's end a note goes on into a chain's next part, never past that part), each note's own: the picked ones alone (a chord's notes can end on different steps), through steps where other notes start. A note goes on only where there is room: 4 notes a step, no pitch twice, no lane hits; else it ends before that step, STEP n TAKEN. VELOCITY moves every step by the same amount. Each knob's run of turns is one undo. The roll draws a note going on as one bar through the step lines.
- **How it is kept** (`ui_input.c` `ed_open` / `ed_close`): every edit works on a copy of the steps unfolded (each TIE as the notes it holds, tied), note by note, folded back (a step where the notes before all go on, and nothing else: a TIE again) and written at once with the audio IRQ off. A step not changed keeps its bytes; a pattern without overlaps folds back to TIEs, as 1.4 reads it.
- NOTES keeps STEP's ways: step entry with REC on (keys held together: a chord; (#31) PRESETS meanwhile its LENGTH; let go: the cursor past its ties; (2.0) on a POLY track a note entered on a long note's step goes in beside it, the long one going on; MONO replaces it as before), a held step's locks, [EDIT] clearing the cursor step with its notes' ties while REC is armed (the notes going on through it end before it; a tie left behind would hold nothing, unseen; PRESETS lands on such a tie as on an empty step). **SHIFT + EDIT** deletes the selection, REC armed or not: the picked notes, else every note starting in it (its hits too), each with the steps it goes on; a step left with nothing is cleared with its locks; one undo, DELETED 3 NOTES. A DRUM track's NOTES is its grid: LANE, HIT, ACC (the range's steps), VELOCITY.
- **FEEL**, the 16 tiles (the B mockup's STEP): CHANCE (OFF, a REST; 1–100 %; then COND's conditions FILL to 4:4, one knob as Elektron's), RATCHET (x1–x4, FLAM), NUDGE, TYPE (- ACC SLD A+S), each set on every selected step. A tile shows its step's value of the knob turned last; a range is framed. A DRUM track shows its grid.
- **SHIFT + OCT- / OCT+** copy and paste the selection: its notes (picked: those), each as long as it is, from the first one's step (two bars at most), with their steps' values; pasted at the cursor into what is there (the chords and the notes going on there: 4 a step, no pitch twice, no tie onto hits; a step with no start of its own takes the copied one's values); anything in the way: nothing, said. The paste is one undo and the selection after. **On a DRUM track's grid** (2.0, the user's ask) the copy is the LANE card's lane, its hits and their accents from its first hit (COPIED KICK 4 STEPS; none there: NO KICK TO COPY), and the paste puts them on the LANE shown then (PASTED ON CLAP: a figure moved from lane to lane); SHIFT + OCT- again on the same steps and lane copies every lane (COPIED ALL LANES), pasted where they were, and once more the lane. FEEL has no LANE card: every lane.
- A note's own length within a chord, and notes that overlap, are a tie bit a note (#25: core.h `step_ties`, the pattern record's version 3). Velocity, chance, ratchet, nudge and type are a step's.
- **SEQ TOOLS on notes that overlap** (2.0, #25, `ui_tools.c`): REVERSE goes note by note when a per-note tie is anywhere in LEN: each note starts where it ended, its ties after it, with its step's values; a step's locks and automation go where its first note starts now (lane hits and steps with no start of their own mirror). Without overlaps REVERSE is as before (whole steps, a note's ties after it). RANDOM and COOK change only the notes starting on a step, each with the steps it goes on (`tl_chord_to`; two meeting on a step: that step as it was). SHIFT, DOUBLE and HALF keep a note beside its ties as they are.
- **Recording overlaps** (2.0, #25, `seq.c` `rec_hold_mix`, `ui_take.c`): REC while playing on a POLY track going forward: a key held while another starts on a later step goes on as its own pitch, a per-note tie each step, beside what the step holds (4 notes a step; lane hits or no room: its hold ends there); let go before a step's middle, it is taken out of that step again. A note recorded into a TIE goes in beside the notes the TIE holds. MONO, LEGATO and UNISON, and the other directions, record as before 2.0: a new note on another step ends the hold, held notes write TIEs. TAKES merge the same way (above).
- **On a chain** (2.0, #44, SEQ › SONG's CHAIN below): the parts are one pattern here too. The header names the chain and the part (A-B · 02), the title and START count the steps on through its parts (NOTES 71, START 71 /128), the DRUM grid its pages (PAGE 5/8). Stopped, PRESETS past the part's end puts the track on the next part (its first after its last), back past its start on the one before, step entry's cursor and the grid's page keys likewise (`bank.c` `chain_cursor`); what the track had is kept first, as at any switch. Playing, the cursor stays in the part playing and the pages follow it (a pattern in RAM a track, as ever).
  - **A note across the parts** (the user's report on the unit: notes couldn't span them): a note goes on from a part into the next as from a step into the next: its tail is the next part's first steps tying it on (`bank.c` `chain_tails`; the next part's pattern changed and kept, its copy staged to come in too). LENGTH past the part's end writes the tail (the card counts the whole note, NOTES' 16 steps at most); PITCH takes it along; an edit that leaves the note short of the part's end (deleting it, moving it, shortening it) takes the tail out. From the chain's last part it goes on into its first, as the chain loops. Not into a REST, nor into the same pattern again (PATTERN END there).
  - Playing, at a LOOP switch with both going forward (a chain's next part, `launch.c` `launch_next`), the notes the incoming first step ties (or its TIE holds) go on: one note-on, no gap.
  - REC: a key held over the switch goes on into the next part as into a step: its first steps tie it (`seq.c` `rec_hold`).
- **PATTERN**'s PRESETS turns the track's DIR (PLAY's knob 3 before), shown under its steps.

**NUDGE** (`seq.c` `seq_enter`): a NOTE step plays up to half a step early or late, in 64ths of its own length (−32 to +31; 0 on the grid).
- **Early** notes play in the step before, in the play direction. The first step after PLAY can't be early, so it plays on the grid; so does an early step under RAND, and one where the next pattern may come in (a launch, the song: the early note would be the outgoing pattern's).
- The step's automation and locks move with it. RATCH parts and a FLAM's full hit count from where the step plays.
- A trig condition on an early step counts the pass that step opens.
- TIE and REST steps keep to the grid.
- The piano roll and the DRUM grid draw a nudged note where it plays. The card reads +12 /64.
- **FEEL** (REC held, knob 4; OFF by default): REC and takes keep where you played. A note going into an empty step sets that step's nudge, so it plays back as played. A step that already has notes keeps its own nudge. With FEEL OFF, recording lands on the grid as before.
- **Saved** with the pattern. Felucca's FUN9 projects still load, every step on the grid (they become SKF3 projects, their steps patterns: see Patterns).
- User presets are sounds only since phase 5: SAVE › USER writes no pattern, and SEQ › PHRASES is gone with the factory phrases.

**A step's own note length** (GATE as a lock: `motion.c` `motion_param`, `ui_input.c` `len_set`): (2.0) NOTES' LENGTH up to 100 %, SEQ › PLAY knob 4 GATE before.
- It shows the cursor step's GATE: its own, or the track's (2.0: both live; SEQ › PLAY dimmed the track's). Turning it gives the step its own, starting from the track's; turned back to the track's value, the step follows the track again. SAVE held undoes it.
- The step's notes last that long, as a share of the step (1–100 %); the next step goes back to the track's GATE. RATCH parts take it too. For a note longer than a step, TIE steps still do it.
- It's a parameter lock, so it uses one of the 64 shared automation records (AUTOMATION FULL when they're used up), and the roll marks the step. CLEAR and [EDIT] on a held step remove it with the step's other locks. (2.0.2, #56) A step held no longer turns it with knob 4 after the PATTERN page: SEQ's own pages never give a held step their knobs (a held step kept PATTERN's LEN, DIV, SWING and GATE until a sound page was shown, the drum grid's KIT, PITCH, TONE and DECAY gone).
- GATE can be automated now too: knob moves recorded with REC, and takes. LEN, DIV and SWING still can't.
- Patterns and live switches keep it with the pattern's locks; the pattern's own GATE is always the track's, never a step's.

**PLAY DIRECTION** (`seq.c` `seq_next`): DIR, per track: (2.0) PATTERN's PRESETS (PRESETS: DIR), SEQ › PLAY knob 3 before.
- **FWD:** 1 to LEN, as always. **REV:** LEN down to 1. **PING:** up and down, each end played once (1 2 3 4 3 2 1 2 …). **RAND:** any step each time.
- **A pass is LEN steps** in every direction. Takes, trig conditions (1:2 …) and launches at a loop's end count passes this way, so a 1:2 step in REV still plays every other time round.
- **REC and takes** put a note played late in a step on the next step in the direction. With RAND nobody knows the next step, so the note goes on the step playing.
- DIR stays when a sound is loaded; a pattern brings its own.

**Patterns, lists and the song** (phase 5; `bank.c`, `ui.c` `lists`, `launch.c`): the notes live in patterns, and a project says which ones each track plays.
- **A pattern** is a track's notes exactly as they play: all 64 steps whole (chords, velocity, accent, slide, chance, RATCH or FLAM, trig condition, nudge, drum hits and their accents, (2.0) each note's own tie: notes that overlap), LEN, DIV, SWING, GATE and DIR, and the track's automation and locks. The sound is not part of it. A pattern with a per-note tie is record version 3, which 1.4 and older read as empty (design/TIER_B.md).
- **The pool:** 64 patterns, 01–64, shared by every project. Entries naming the same pattern share it: an edit made in one is heard in the others, in any project. COPY (SEQ › SONG) gives an entry its own.
- **The lists:** each track has 16 entries, A–P. An entry names a pattern (or none) and its REPS, the passes it plays (1–16). A track is on one of its entries and plays that entry's pattern; HOME, the SEQ pages and the keys edit it there.
- **A project** keeps the sounds, the mix, the lists and the entry each track is on, SONG, the scenes, the FM6 patches and the name: SKF3, 2048 bytes (`project.c`; SKF2, 1280 bytes, held four tracks: phase 5's, still loaded and kept as they are). It names patterns instead of holding steps.
- **Kept by itself:** a pattern has no save. What a track has is kept as the pattern it's on:
  - Playing, a pattern a track leaves (a launch, the song) waits in RAM, six at most (one a track); whatever reads it reads that copy.
  - Written at the autosave's quiet stop: stopped for 2 s with nothing sounding, at most once every 20 s (sooner when six are waiting; 1.x: 10 s, once a minute). RESTORE LAST OFF still writes them; only the project isn't.
  - A project save or load and the editor's backup write them first. Pulling the power before then loses the last edits, as with the autosave.
  - Loading a project stops the transport first.
- **A new pattern:** steps on a track that's on no pattern (a new project, a cleared entry) become one when first kept: the first free number, on the track's entry (the list's first empty one when that entry names another).
- **PATTERNS FULL:** with no free number, or (playing) no room left to wait, what a track has can't be kept, so the save, load, launch or song move that would leave it doesn't happen. SAVE › PATTERNS clears the unused ones.
- **AUTOMATION FULL:** the 64 automation records are the six tracks'. A pattern that comes in without room for its automation plays without it and keeps it stored; its track takes no new automation until another pattern comes in.
- **Projects from before** (Felucca's FUN9, Skiff's SKF1) become SKF3 at the first clean boot (SKF2 ones stay SKF2: they already name patterns): each track's steps a pattern on its entry A (one the same in the pool already: that one). The 64 bank patterns of before are 01–64 (T1's A–P 01–16, T2's 17–32 …). Old SONG rows and scenes are dropped. A project the pool has no room for stays as it was and loads as before. An old backup restored is converted the same way.
  - (2.0, design/TIER_B.md 10) The unit converts none of them any more: the web installer and the editor's Restore do (web/conv, the same code built with the old readers). A slot of an older format (FUN1..FUN9, Felucca's FUNA, SKF1, SKF2) shows **OLD FORMAT** as its name in SAVE › PROJECT and keeps its bytes: LOAD and NAME say OLD FORMAT, SAVE asks before writing over it, DELETE clears it, a backup takes it as it is. A user sound of versions 1–3 or DIGITAL: LOAD says OLD FORMAT and SAVE asks first, naming it so (SAVE › USER's list shows "--").
- **Flash:** 20 sectors (the VM area's last two, the package's USR region; never the BTIF sector): 16 chunks of four patterns and 4 spare. A write puts a chunk whole into a spare sector, its commit record last; at power-on each chunk's newest intact copy wins, so a write cut off by a power loss leaves its patterns as they were. In the editor's full backup as objects 16–31.

**SEQ › SONG** (`ui_graph.c` `graph_song`, `ui_input.c` `song_knob`, `bank.c` `song_*`), after PLAY. (U2: SEQ held is SEQ TOOLS on every page; SONG is reached as a page.)
- **The rows** (the mockup's): a row per track, as on the PERFORM map: its number in its colour, then its 16 entries. An entry naming a pattern is the pattern's number in a tile, an empty one a dot. The entry the track is on is a tile of its colour; the one staged to come in (a launch, the song's next) is outlined; REPS past 1 put a tick each under the tile (three at most). The selected track's row is a band of its colour, everything on it INK; KNOB 1's entry is white. A REST (#28) is a tile like a pattern's with a dash for the number, in the track's colour while the track is on it (the user's pick B of design/mockups/rest_sheet.png); the rests between patterns stay a bare dash.
- **Knobs:** 1 ENTRY (A–P), 2 PATTERN, 3 REPS (CHAIN, 1–16x), 4 SONG (OFF, ON, ONCE).
- **KNOB 2 picks, OCT+ puts it in.** It runs through REST, the kept patterns, then NEW, COPY, COPY+ (2.0, the user's pick 1A of design/mockups/v2_songs.png), CLEAR, then (2.0, #30, the user's pick 2A of design/mockups/v2_feats.png) SOUND and, on an entry with a sound, NO SOUND; an empty entry's -- first, then NEW, then REST and the patterns. The footer says what OCT+ will do:
  - **SET 07:** the entry names 07. When the track is on that entry, 07 comes in as a launch does.
  - **SET REST** (#28): the entry is a REST, a rest wherever it is in the list (below). When the track is on that entry, it goes silent as a launch does.
  - **COPY 06 TO 17:** a new pattern, a copy of the entry's (the track's edits in it first), and the entry names it. A track on that entry goes on with the copy. (2.0, #34, the user's pick 2A) The copy takes the first free number after its original's (06's copy is 08 when 07 is used; round to the first free before it), so copies sit beside their originals in the pool; nothing else is renumbered (`bank.c` `pool_alloc_after`).
  - **NEW 22** (2.0, #32): a new empty pattern, the first free number, with the track's LEN, DIV, SWING and DIR and GATE at its default, 100 % ((2.0.2) the track's before, so a pattern from before 2.0 passed its 50 % on; a track on an empty entry gets 100 % too), and the entry names it (T1 F NEW 22); a track on that entry goes to it as a launch does. Recorded into or edited, it is kept as patterns are. PATTERNS FULL when no number is free. The web editor's too (LIST op 7).
  - **COPY 06 TO 22 AT F** (2.0, #33, COPY+): the copy (its number as COPY's) goes into the first empty entry after the entry, past the block it starts (C D E: F; an empty entry between patterns, a rest, counts as empty), with the entry's REPS; the entry stays as it is. Not offered when no empty entry follows. On a chain (#44), all of it: COPY A-B TO F-G, each part copied into as many empty entries in a row after the chain, a chain again, their REPS too. On the rows that entry shows the copy's number, framed in TEXT, while COPY+ is picked. The web editor's too (LIST op 8).
  - **PLAY** (no pick): the track goes to the entry as a PERFORM key does: at once when stopped, at the LAUNCH point when playing.
  - **SET SOUND STRINGS** (2.0, #30): the track's sound now goes on the entry (T2 C SOUND STRINGS), a user sound by its slot, a factory one as its engine and preset (`bank.c` `snd_of`, `song_snd`). It must be that sound as it was loaded: values, an FM6 patch, a DRUM-X preset's kit unchanged since (edited: SOUND EDITED, then SAVE > USER FIRST; OCT+ doesn't breathe). **SET NO SOUND** takes the entry's off (T2 C NO SOUND).
  - OCT− drops the pick (CANCELLED); with none, a launch staged for the track (not the song's next).
- **An entry's sound** (2.0, #30; design/TIER_B.md 11): when the track comes onto the entry it loads that sound as PRESETS or USER would, all but the track's own values (`param_kept`), not an undo step. Playing, the main loop prepares it when the entry is staged (`stage_entry`: `esnd_make`) and the swap puts it in with the pattern (`launch.c` `esnd_put`): the entry's first step plays it, an engine change fading from that block (2.9 ms). Stopped (`ent_go`), at once. A tile with a sound has its top right corner cut (a 4 px step to the ground), and with no pick the footer names the entry's sound after OCT+'s hint (OCT+: PLAY  SOUND STRINGS). COPY+ copies it with the entry, CLEAR takes it away. Projects keep it (SKF4's entry bytes).
- **CLEAR clears the entry** (KNOB 2's last choice but SOUND's, after COPY and COPY+; U2: was EDIT), stopped (STOP TO CLEAR), after a dialog: CLEAR ENTRY C?, then what becomes of its pattern. It goes too when nothing else names it (erases pattern 07 too) or stays (pattern 07 stays: entry D uses it; entries D and T1 A use it; 3 entries use it; a saved project uses it). T2 goes empty when the track is on that entry: its steps and automation go with it. Can't be undone. A REST clears at once, playing too: nothing goes with it, so no dialog.
- **SONG ON:** playing, each track moves to its next entry at its own pass end, once the entry's REPS are played, whatever LAUNCH says. An empty entry between the track's first and last pattern is a rest (1.2.1, #12): silence, for its REPS passes of the track's LEN (the timing the track has), on no pattern, so nothing is kept unless it is recorded into (then a new pattern names that entry). The empty entries after the last end the list; after the last, the first again. A list of one entry stays on it. On the rows a rest is a dash, lit while it plays, framed while staged, its repeats under it; KNOB 3 sets an empty entry's REPS too (were --).
  - **A REST** (#28, KNOB 2) is such a rest wherever it is: the list runs from its first pattern or REST to its last, so a track can start later (a REST first) or end sooner (a REST after its last pattern), without an empty pattern. Its REPS count passes of the track's length (the pattern before it). GLO + PLAY starts a track on its REST. A PERFORM key on a REST makes the track drop out at its launch point; recorded into, a REST becomes a new pattern in that entry. Empty entries before the first pattern or after the last are still passed over, so the lists of before play as they did. The entry stores 65 (`PAT_REST`): firmware before this one refuses a project that holds a REST.
  - PLAY goes on from the entry each track is on; GLO + PLAY starts every track from its first entry.
  - A launch (a PERFORM key, a scene, SONG's PLAY) moves the track's place: the song goes on from there. PERFORM's OCT− leaves the song's next alone.
  - SONG OFF: each track loops the entry it's on.
  - **SONG ONCE** (2.0, #47, the user's pick 4A of design/mockups/v2_songs.png): PLAY starts every track from its first entry (or REST); each plays its list once, its last entry's REPS too, then goes silent (a rest on the empty entry after its last, on its last when all 16 are used); when the last track's end comes in, the transport stops and every track is on its first entry again. A launch of a track's own (a PERFORM key, a scene) after its end plays, and its list goes on from there. STOP mid-song, then PLAY: from the top. The web editor sets SONG as OFF, ON or ONCE (LIST2, INFO 58 01); an editor before shows ONCE as on.
- **A chain** (2.0, #44, the user's pick 3A of design/mockups/v2_songs.png and A of v2_join.png, two patterns as one; `bank.c` `ent_block`): KNOB 3 below 1x is CHAIN (an entry with one after it): the entry goes on into the next, and entries on CHAIN with the one they end on are one pattern wherever the track plays them, each part (a pattern or a REST) with its own LEN, DIV and SWING, up to all 16 entries. An empty entry ends a chain. KNOB 3 right again: a number, the chain split.
  - SONG OFF: the track loops the whole chain (its next part at each part's pass end, the first after the last).
  - SONG ON and ONCE: the chain is one block, each part once, its last entry's REPS repeating the whole block (A and B on CHAIN, C 2x: A B C A B C), then the list goes on (ONCE: after it, the track's end).
  - A launch (a PERFORM key, a scene, SONG's PLAY) onto any of its parts starts it from its first. LAUNCH LOOP while the track plays a chain comes in at the chain's end, as at a pattern's (until then the rows frame it as staged; PERFORM's OCT- drops it). A scene naming the chain a track plays leaves it be. PLAY from stopped starts a chain from its first part.
  - The rows put a chain's tiles on a box of their own (the user's pick A of design/mockups/v2_chainmark.png; the 2 px bar over them before hardly showed on the unit): the band's height and 2 px round the tiles, the track's colour dimmed (35 % of it on the ground), on the selected track's band too. The tiles on it are drawn as anywhere (the one playing, the one picked); a staged frame and the repeats' ticks on it take the track's colour. Two chains side by side keep a notch between them (each box ends 2 px inside its tile there); COPY+'s copies of a chain are on one too. Its parts on CHAIN have no ticks, its last's are the block's repeats.
  - Stored as the entry's REPS byte 16 (`REPS_CHAIN`): firmware before this one refuses a project holding one. The web editor draws a chain's box (the track's colour dimmed on the Song tab's ground, a notch between two side by side) and has CHAIN below 1x in REPS (LIST2); an editor before shows 1x, and a REPS it sets there splits the chain as KNOB 3 turned right.
- **The header** (BLOCKS), in the selected track's colour before the title: SONG ON (SONG ONCE) on this page while the song plays; on the other SEQ pages, the pattern being edited: its entry, its number and, when more than one entry names it, how many (C · 06 · 2x); on a chain, its entries and the part's number (A-B · 02).

**SAVE › PATTERNS** (`ui_graph.c` `graph_bank`, `bank.c` `pool_refs` / `pool_clear`), after USER: the pool (the mockup's).
- **The grid:** the 64 patterns, 8 × 8. A kept one is its number in a tile: TEXT while something uses it, MID and outlined while nothing does. An empty number is a dot; the picked one is white.
- **Knobs:** 1 PATTERN (01–64), then its LEN, USED (the entries naming it in the lists and the saved projects, and a track on it that no entry names) and (2.0, #46, the user's pick 5A on design/mockups/v2_songs.png) ACTION: CLEAR, EMPTY or UNUSED, which OCT+ does (the footer says it: OCT+: EMPTY 21, OCT+: CLEAR 3 UNUSED).
- **CLEAR** (OCT+): only a pattern nothing uses (else USED BY 3 ENTRIES; NOTHING TO CLEAR on an empty one), only stopped (STOP TO CLEAR), after a dialog: CLEAR PATTERN 23?, no entry uses it, can't be undone.
- **EMPTY** (2.0): the pattern's steps, NUDGEs and automation go, its number, LEN DIV SWING GATE DIR and every entry naming it stay (a used one too); a track on it empties with it. Stopped, after a dialog: EMPTY PATTERN 21?, 4 entries go quiet, can't be undone (`bank.c` `pool_empty`).
- **CLEAR UNUSED** (2.0): every pattern nothing uses, at once; while it is KNOB 4's, those patterns are lit (TEXT, outlined). Stopped, after a dialog: CLEAR 3 UNUSED?, can't be undone; CLEARED 3 PATTERNS (`pool_clear_unused`). #46's NEW is SONG's: a pattern lives on an entry. The web editor has EMPTY and CLEAR UNUSED too (POOL ops 2, 3), after a dialog of its own.

**PERFORM layer and live switching** (`ui_layer.c` `layer_perf`, `launch.c`, `bank.c` `bank_switch`): ARP held.
- **The map** (the B mockup's rows): a row per track, its number in its colour, its list's 16 entries A–P across as the white keys run (F3 = A to G5 = P).
  - An entry naming a pattern shows its letter, an empty one a dot. The entry the track is on is a tile of the track's colour (DIM while muted); the one staged to come in is that tile's outline.
  - At the row's end, M in REC's colour while the track is muted, outlined while a mute is staged to turn (the M's colour says how it is now).
  - The selected track's row (the keys' and knobs' track) is a band of its colour with everything on it in INK, as on HOME.
  - Under the tracks, SCN and the 16 scenes in the keys' order (see Scenes).
  - **Header** (BLOCKS), as the mockup's: PERFORM and the LAUNCH point at the right (PERFORM BAR), the lock before it while locked; a message takes its place for a while.
  - **Footer**, as the mockup's: WHITE: PATTERN and BLACK: MUTE. In SCENE, WHITE: SCENE and [SAVE] + WHITE: STORE, since storing is the gesture to learn there. This is the one screen that doesn't label SELECT, PRESETS and OCT− (UX rule 2): the mockup's choice, kept on request.
  - **Keys:** the entry playing and the staged one lit, the other kept ones breathing.
- **A key, stopped** (or counting in): the track goes to that entry at once, no dialog. What it had is kept as the pattern it was on, so there's no undo to take.
- **A key, playing:** the entry is staged and the header says when it comes in (T1 C AT LOOP END). Another key replaces it; an empty entry says T1 F EMPTY and leaves it staged.
- **Black keys 1–6: staged mutes** (`bank.c` `mute_switch`). Each tap turns its track's MUTE the other way ((2.0, D 1A) held past HOLD, the track is cut while held instead, nothing staged: One mute). Stopped, at once, as on GLO. Playing, at the LAUNCH point, as a pattern (LOOP: that track's own pass end); the header says so (T4 MUTE AT LOOP END), and a second press before it lands takes it back (T4 MUTE CANCELLED). The step starting at the point plays as the new MUTE says. The map's M shows them; a sounding track's key is lit, a muted or staged one breathes.
- **OCT−** cancels everything staged, every track's entry and mute (CANCELLED; NOTHING STAGED when there was none). The song's next stays: it's the song.
- **The list replaces the chains** of before: an order of patterns to play through is the track's list with SONG on (see SEQ › SONG).
- **PRESETS turns KEYS:** TRACK (the selected track's entries, as above) or SCENE, kept with the settings; the footer's WHITE: PATTERN or WHITE: SCENE says which.
- **Scenes** (`bank.c` `scene_store` / `scene_recall`): 16 on the white keys in SCENE. A scene is the entry each track is on (none when the track is on no entry's pattern: that track is left as it is) and every track's mute.
  - **SAVE held + a key** stores the tracks as they are now (SCENE 5 SAVED), playing or not. With the layer locked, SAVE leaves the lock alone (in TRACK it closes the lock, as on any layer).
  - **A key** recalls it: each track goes to its entry and its mute turns as the keys do: at once when stopped (LOADED SCENE 5), staged for the LAUNCH point when playing (SCENE 5 AT NEXT BAR), so with BAR or BEAT everything lands together. A track already on the scene's entry gets nothing staged; an empty scene says so.
  - **The map:** the row under the tracks, SCN and then the 16 scenes in the keys' order: a kept one its number in a box (bright while KEYS is SCENE), an empty one a dot; the one recalled or stored last is filled, outlined while its changes are still to come. Its key is lit, the other kept ones breathe.
  - Kept in the project (SKF3) and with it in the autosave, so each project has its own. A scene stored before six tracks (an SKF2 project's) holds nothing of T5 and T6: it leaves them as they are.
- **SELECT turns LAUNCH** (the header shows it: PERFORM BAR), kept with the settings:
  - **LOOP** (the default): when the track's pass ends, in its direction.
  - **BAR:** on the next bar, as the click counts bars (four beats of four 1/16 steps).
  - **BEAT:** on the next beat.
  - **NOW:** on the next step; the playhead goes on where it is.
  - LOOP, BAR and BEAT start the pattern from its first step in its direction, a new pass.
- **The switch** happens in the audio interrupt on a step boundary: the steps, LEN, DIV, SWING, GATE and DIR and the track's automation and locks all at once. The old pattern's notes end; its NUDGE or RATCH parts still to come are dropped. What the track had is kept as the pattern it left.
- **No undo:** a switch takes no undo copy (nothing is lost). An undo copy taken on the pattern left says UNDO NEEDS PATTERN 07 and changes nothing.
- **Stopped before the launch point:** the staged entry or mute comes in at once (the song's next doesn't: PLAY stages it again). A project load drops them.
- **No room for its automation** in the shared pool: the steps come in without it, and staging says AUTOMATION FULL.
- **Throws** (knobs 1–4; `throw.c` `throw_id`), the mockup's four: **CUTOFF** (the sound's tone: its first HOME parameter where that is one, CUTOFF on most engines; U3 V2 `THR_TONE` where it picks a sample or a source), then **REVERB**, **DELAY** and **DIST**, the track's sends on every engine (U4: were VERB, DLY, DRV). Each knob pushes its parameter away from its value for a moment. The card shows how far it's out, in the knob's steps (+0 at rest, +18), its gauge from the middle.
  - Once the knob rests (120 ms), the throw glides back in a line over one bar ((2.0, D 2A) over MENU › CONTROL › RETURN's time: One way back). STOP drops it at once. It never goes past the parameter's range. (2.0) OCT+ in PERFORM keeps every throw out: its value as it sounds then becomes the parameter's own (`throw_keep`; not recorded, no undo, as MOMENTARY's OCT+).
  - It's never saved and never edits the sound: the audio interrupt puts it on at the start of each block (so a note starting then reads it too, its attack for example) and takes it off after the mix. What the screen, the editor, projects and presets see is always the parameter's own value. Automation or a lock writing the parameter meanwhile wins.
- **Timing:** Felucca rounds each DIV's step down to whole samples, so 1/32 and triplet tracks run a few samples a beat ahead of 1/16's. Their step starting less than half a step before the bar line counts as the bar's.

**DUCK** (FX › DUCK, after SLICER; `duck.c`): a track dips under another track's hits, the classic sidechain pump.
- **Knob 1 BY:** the track it ducks under, OFF or T1–T6. The knob steps over the track itself.
- **Knob 2 DEPTH:** how far down at each hit (100 % is silent). The default is 76 %.
- **Knob 3 REL:** how long it takes to come all the way back, in a straight line. Times are the envelopes' (1 ms to 10 s); the default is 199 ms.
- **What counts as a hit:** every note the source starts: steps, ratchets, flams, keys, MIDI and the arpeggiator. A DRUM kit counts its KICK lane only, so its hats never pump anything. A muted source strikes nothing.
- **The sound:** the gain drops to DEPTH over 3 ms (no click) and comes back over REL. It sits on the track's own signal after DIST and the SLICER, before LEVEL, PAN and the sends, so its effect sends dip too, while delay and reverb tails already there ring on.
- **The graph:** the track's level over one bar of its source's steps: a dip at each hit, its recovery over REL, the hits marked underneath in the source's colour.
- The three belong to the track's mix: a sound load keeps them, and they're saved in the project. DEPTH and REL can be automated; BY can't. With BY OFF the mix is bit for bit as without ducking.

**Six tracks** (phase 6; `core.h` `NTRK`, `NHEAVY`; mockups `design/mock_p6.py`, chosen 2026-10-10): T1–T6, T5 and T6 light.
- **The voices** are still 8, shared by every track (`voice.c`): two more tracks add each block's fixed work, not voices.
- **Light tracks** (`engines.c` `eng_allowed`): T5 and T6 play every engine but PHYS and GRAIN, whose state is the big one (T1–T4 keep a PHYS-sized area each; a light track has DRUM's 1.8 KB, `eng_lite`).
  - The ENG knob and EDIT's KNOB 1 step past them; the PRESETS list leaves them out; the EDIT layer shows them dimmed, their keys dark.
  - Asking for one anyway (an EDIT key, a user sound, the editor) is refused with PHYS: T1-T4 ONLY in the header; nothing changes.
  - A project holding one there (only a crafted file can) loads that track's power-on sound.
- **Power-on sounds:** T5 SAMPLE PIANO, T6 DRUM KIT.
- **The track screens show all six** (U3, the review's N1; `design/mock_u3.py`, the user's pick A. Phase 6 showed four with a scroll bar, `trk_bar`, still drawn by a four-track build):
  - HOME: the selected track's lane at its size (its name over its engine), the other five compact: 18 px, the number in M, the name in S, 12 px cells (`ui_home.c`).
  - SEQ › SONG: six rows, 20 px bands 22 apart, 15 px tiles.
  - PERFORM: six track rows and SCN, 17 px rows 19 apart, 13 px tiles.
  - MIXER: six 36 px strips at the layer maps' columns (`ui_graph.c` `mix_strip`): the number, its state (recording a square in REC, armed its outline in the accent, muted M), the sound's first word, LEVEL a fader with the output meter beside it and its dB under it, the PAN (L31, C, R37). REV is on the cards only (the selected track's KNOB 3). The selected track's strip is a band of its colour. LARGE: the number, the state, the name, LEVEL as a gauge and the meter under it.
  - GLO's cards: four, the ones around the selected track (T1–T4, or the four ending at T5 or T6; `ui.c` `trk_top`), KNOB 1–4 their LEVELs.
- **The key maps show all six:**
  - GLO: SOLO 1–6 on F G A B C D, then ALL on E4, FILL on F4, TAP on G4 (two columns wide; in this order since 1.2.1, #11), MUTE 1–6 on F# G# A# C# D# F#. 36 px cells.
  - FX: the effects as before; the mute row of six as GLO's.
  - PERFORM: black keys 1–6 stage the mutes.
- **MIDI IN** (MENU > MIDI IN CH1-6, U3): channels 1–6 play T1–T6; 7–16 are free for other instruments. (1.3, from Felucca 1.5.5) MENU > MIDI > T1..T6 CHANNEL move a track to any channel (`seq.c` `trk_midi_ch`, settings bytes [15][6..11] as the distance up from the track's own, so 0 is CH1-6): MIDI IN hears the channels the tracks are on (the first track on a channel plays it), the keys play out on them, and a channel no track is on any more lets go of its notes at the next block (`midi_route_drop`). MIDI IN says TRACK once a track moved.
- **MIDI LEARN** (1.3, from Felucca 1.5): MENU > MIDI > MIDI LEARN (OCT+) closes the MENU onto the page; a knob turned picks its parameter on the track it edits, the next CC heard is learned to it (`midi_learn.c`), OCT- clears the pick's, OCT+ is done; the footer's one line says what is picked and its CC, OCT+ DONE at the right. Ten entries of 3 bytes (code, CC, track) in engine 1's favourites row, bytes 2..31 (DIGITAL, retired: its stars only ever byte 0, still migrated); an engine parameter's code is 0xF8 + k, so moving P_E0 keeps them. A learned CC leaves the standard map. LEARN CLEAR forgets them; RESET SETTINGS too.
- **The MENU scrolls** (1.3): a tab of more than six rows (MIDI's ten) shows six, the cursor's row the last shown once past them, a thin bar at the right for where they are. Rows with no value that OCT+ acts on (`menu_items.c` `menu_act`): CALIBRATION, RESET SETTINGS, ABOUT, MIDI LEARN and LEARN CLEAR (its count before the chevron); the key hint says START / STOP, CLEAR or OPEN.
- **Projects:** SKF3, 2048 bytes, the six tracks and the scenes of six (`project.c` `proj_fmt_t`: SKF2's layout for six tracks). Older projects (SKF2, SKF1, Felucca's) load with T5 and T6 on their power-on sounds; SKF2 slots stay SKF2. ((2.0) SKF4 is written, SKF3 read; the older ones are the converter's.)
- **Scenes:** 82 bytes: the 50 of before, then 2 bytes a scene for T5 and T6. A scene stored with four tracks leaves T5 and T6 as they are.
- **The pool** stays 64 patterns, shared by the six; six patterns can wait in RAM.
- **Splash:** six blocks.
- **Cost:** the audio interrupt's static cost 37,934 against a budget of 37,502 (within its margin); RAM .data+.bss 74.7 KB of 96 KB, the pool 339.8 KB of 348 KB.

**DRUM-X** (phase 7; engine 14, `eng_drumx.c`, `drumx_voice.c`, `ui_dx.c`): the drum engine of [JIANT FM](https://github.com/juanjiant-bit/JIANT) by juanjiant-bit (GPL-3.0-only), after the idea of Sonic Charge's Microtonic. It sits beside DRUM, which is unchanged: old sounds and projects play as before.
- **A synthesized kit:** the same 8 lanes as DRUM (KICK SNARE CLAP CL HAT OP HAT TOM RIM BELL; BD SD CP CH OH TM RS CB on the grid). Each is a tuned oscillator (SIN, FM, MTL or BEL) with a pitch envelope, plus noise through a filter (LP, BP, HP), each with its envelope. Every sound has two patches, A and B, of five values: PITCH, P MOD, DECAY, NOISE, COLOR.
- **A drum track as DRUM's:** the same keys (the GM drum map), grid, lane LEVELs (EDIT › LANES), the closed hat choking the open one. Its KICK lane is DUCK's source, as DRUM's.
- **EDIT 1 and EDIT 2** (KIT, HIT): MORPH (the morph from every sound's A to its B; KNOB 1 on HOME), PITCH, COLOR, DECAY, NOISE (each moves every lane from its patch; 64 is the kit as it is), FM, FOLD (a wavefolder after the envelope), DRIVE (U4: the words spelled out). They move a hit while it rings, and they can be automated and locked as any engine's.
- **The kit is the project's:** one kit for every DRUM-X track, saved with the project (SKF3: six tracks) and kept by the autosave; a project without one (older, or SKF2) gets the factory kit. Two DRUM-X tracks share its sounds and differ by their knobs. A user sound keeps the knobs, not the kit.
- **EDIT › SOUND, the sound sheet** (phase 8, after EDIT 2, DRUM-X tracks only; `ui_dx.c`; the user's pick of three mockups, `design/mock_p8.py` direction 2): one sound of the kit, all of it on one page.
  - **The sound:** a key struck on the track, or PRESETS (BD .. CB; no sound is loaded). The header names it: SOUND SD.
  - **The knobs** are a window on the sheet that SELECT moves, A beside B so there is no side to choose: PITCH A, PITCH B, P MOD A, P MOD B; then DECAY A, DECAY B, NOISE A, NOISE B; then COLOR A, COLOR B, LEVEL (the sound's lane level: automation as any knob), DRIVE; then the switches WAVE (SIN FM MTL BEL), FILTER (LP BP HP), SNAP, P SHAPE (the pitch modulation: DECAY LONG NOISE SINE). Past its top SELECT goes on to EDIT 2, past its foot to (2.0) LANE MIX, the family's last page (U3: VOICE and VOICE 2 change nothing on the hits, LANES is DRUM's: the sheet has LEVEL).
  - **The panel:** the 8 sounds, the one edited in the track's colour; then a row a value, A's bar in the track's colour and B's in white with their numbers, LEVEL and DRIVE, the switches in words; the knobs' rows on a band with a mark at the left, a scroll bar at the right. The footer shows the page number, as every page does (SELECT: ROWS is on SHIFT's map).
- **A kit edit isn't undoable,** as a knob turn isn't. **INIT SOUND** on a DRUM-X track puts the factory kit back too (the dialog says INITIALIZE SOUND? and the DRUM-X kit); SAVE held undoes both at once (`ui.c` `sound_init`, UNDO_KIT).
- Left out of JIANT's DRUM-X, for now: its group mutes, X-MOD (a tempo-synced LFO on MORPH), PUNCH and the dice. Features like these would come as one considered choice, not piecemeal. The SOUND pages' look may be revisited.

**FM6 LAB** (2026-10-13; `ui_fm6.c`; the user's pick of four mockups, `design/mock_fm6.py` direction D): every value of an FM6 patch on the unit, so a sound can be built from the bare sine and saved, without the web editor.
- **EDIT › OPERATOR and EDIT › ALL OPS** (after EDIT 2, FM6 tracks only): two sheets. PRESETS picks the operator (OP1 .. OP6, no sound is loaded; the header says OPERATOR 3), SELECT moves the knobs through five windows each, then on to the page beside it.
  - OPERATOR: TONE (OUTPUT, RATIO, FINE, DETUNE), ENV 1 (RATE 1, LEVEL 1, RATE 2, LEVEL 2), ENV 2 (3 and 4), KEYS (L CURVE, L DEPTH, R DEPTH, R CURVE: the keys below BREAK on the left, above it on the right, as the chart), MORE (BREAK, KEY RATE, VELOCITY, LFO AMP).
  - ALL OPS: ALGO (ALGO, FEEDBACK, TRANSP), PITCH 1 and PITCH 2 (the pitch envelope's rates and levels), LFO (WAVE, RATE in Hz, FADE, KEY SYNC), DEPTH (VIBRATO, VIB SENS, TREMOLO). OKS isn't there: the engine never reads it.
  - **RATIO** is one knob over x0.5, x1 .. x31, then the fixed frequencies 1, 10, 100, 1K HZ (MODE); FINE shows the ratio or the Hz it makes (x14.00); DETUNE ±7; the curves -LIN -EXP +EXP +LIN; BREAK a note (MIDI 21 + it: C4 at 39); PITCH in semitones.
  - **What plays:** ALGO and FEEDBACK show the patch through EDIT 1's ALGO and FEEDBACK, and a turn puts that into the patch with the macro back to neutral (PAT, 0). The other macros (MOD LVL, RATIO, MOD ENV, VEL MOD, DETUNE) stay offsets over several operators.
  - A knob edits a copy of the patch, then `fm6_set_patch` and `fm6_adopt`: SLOT turns OWN (or the factory patch it now equals), so PATCH never loads over an edit. Some values sound from the next note (OUTPUT, the ratios, the scaling); the rates and levels from the next stage. A patch edit isn't undoable, as a knob turn isn't.
- **The panel** (after an idea in a video by Floyd Steinberg, drawn our own way): every operator's envelope as the engine runs it (`fm6_op_env_init`; the macros through `fm6_macro`, as the audio's copy), a note of MIDI 60 at velocity 100, straight between its breakpoints on a log time axis (the held note, a dotted key up, the release), so a 1 ms attack and a 4 s decay both show.
  - **Brightness:** each carrier is a thick line, shaded along its length in 12 steps from cool blue (a sine) to REC's red (FM's bite): the strongest modulator feeding it at that moment, its level over its top 60 dB, square-rooted (a little FM is heard at once), the feedback counted as FB / 7 of its operator. (Steinberg averages the modulators; with six operators and several carriers an average dulls a bright pair.) The caption says where it bites most (BRIGHTEST 12 MS, a mark under it) or PURE SINE, the length, and the key: SINE, the ramp, FM. Colour is never the only cue.
  - The modulators are thin lines in their own colours (the palette's six), the one picked brighter; the picked operator's breakpoints are rings, the one the knob just turned filled. Tabs OP1 .. OP6 above, the picked one outlined; under each its line's colour (a carrier's the ramp); on TONE each with its ratio.
  - KEYS and MORE: the picked operator's level across the keyboard (MIDI 21 .. 108, 0 to -48 dB), BREAK a dotted line with its note, C4 a dot. ALGO: the algorithm chart with the picked operator lit, and what it does (OP4 MODULATES OP3, GOES TO THE OUTPUT, IS SILENT). PITCH: the pitch envelope over the note, the sound's outline behind. LFO and DEPTH: two seconds of vibrato from the key, FADE bringing it in, the tremolo under it.
  - The simulation runs only when what it draws changes (`fm_sig`), at most a few ms.
  - **In the themes of their own** (FM6 LAB T, 2026-10-13): CRYPT's fire (the ramp ash, blood, ember, gold, flame; square chunky strokes; skulls for handles, the one turned aflame; a flame where it bites; stone tabs, the picked one gold-framed; the ASCII shade under the sound; KEYS' BREAK a dagger; the vibrato a snake), NEON's night to sunset (cyan, violet, pink, orange, yellow; tubes and their glow; sparkles; a laser where it bites; the striped sun and the grid behind; glass tabs, a laser frame; the sun's bands under KEYS, PITCH and LFO; BREAK a laser), QUARK's energy (dim cyan, cyan, white, gold; the modulators a bubble chamber's dotted tracks in the colour charges; nuclei; a collision where it bites; a Monte Carlo fill under the sound; a cell's brackets on the tabs). ALGO's picked operator: a gold frame, a laser, brackets. SKIFF, HI-CON and PAPER as drawn above.
- **INIT SOUND** on an FM6 track is the bare sine to build from: `FM6_INIT` (OP1 alone, ALG 1), SLOT OWN, the macros at 0; SAVE held undoes it. An FM6 track on its own patch shows the patch's name (INIT VOICE) where the sound's name goes.
- **Saved as a sound** (SAVE › USER): the user sound keeps the whole patch, as before (`up_fm6.c`); a name typed for it goes into the patch's own name too (its first 10 letters), so a `.syx` from the web editor carries it. The automatic name and a factory patch (F n) leave it alone.

**Factory presets for the themes** (2026-10-11): ten sounds each for CRYPT (dungeon synth), NEON (synthwave) and QUARK (particles), added at the end of their engines' lists, so every earlier preset keeps its number.
- CRYPT: CRYPT CHOIR, MONK CHANT, CATHEDRAL, HARPSICHORD, BONE FLUTE, WAR HORN, GRAVE BELL, DARK STRINGS, WAR DRUMS, CAVE WIND.
- NEON: NIGHT BASS, NEON LEAD, SUNSET PAD, HERO BRASS, GLASS KEYS, TURBO SYNC, NEON ARP, DREAM CHOIR, NEON KIT, SYN TOM.
- QUARK: GEIGER, QUARK CLOUD, PHOTON, ISOTOPE, COLLIDER, QUANTA, NEUTRINO, PLASMA, HADRON KIT, DARK MATTER.
- **A preset's other values** (`core.h` `preset_t` `more`): besides its engine values, envelope, sends and voice mode, a factory preset may set up to four more of the sound's parameters: the LFO, GLIDE, ENV › PITCH, (phase 9) DRIVE's TYPE and TONE. Twelve of the thirty do (vibrato, PWM, pitch drops, S&H bleeps). Like every other sound parameter, they go back to their defaults when a preset without them is loaded. The track's own settings are never among them (`editor_test` checks every preset).

**Sounds and their types** (phase 9, 2026-10-15):
- **148 factory sounds**, at least 8 an engine. A table only grows at its end: a preset's number is in projects, favourites, the power-on sounds and the goldens. Names have 12 letters at most, each once among all the engines (a type's list mixes them; phase 9 renamed the older ones that weren't: ANALOG's PLUCK BRASS WIND, FM6's BELL BRASS MARIMBA ORGAN PLUCK, PHASE's BRASS ORGAN STRING BELL and DRUM-X's DRUM KIT, now OWN KIT; `ui_test` checks it), and no drum machine's or synth's trademark. Each new sound passed the audition page (the user's KEEP / CHANGE and a note) and regress's HEALTH, at the loudness of the sounds of its kind there were before.
- **A type each** (`core.h` `preset_t.tag`, `TAG(PT_*)`): BASS LEAD PAD KEYS PLUCK DRUM FX. SAVE › PRESETS' KNOB 4 is **LIST**: ALL, FAV, then the types. A type lists every engine's sounds of it, in the PRESETS list's order (`ENGINE_ORDER`); KNOB 1 and the PRESETS knob step through it wherever they browse; KNOB 2 (ENG) still steps the engines. User sounds are on ALL and FAV only. A light track leaves PHYS and GRAIN out of every list, as of ALL. The footer names the LIST beside the engine; an empty one says NO SOUNDS (a type) or NO FAVORITES.
- **The LIST is the unit's** (not the track's, not the project's): the settings byte `favorites.factory[15][21]` (0 none, else the type), kept below 8 at boot. `favorites.filter` stays 0 or 1, because older firmware refuses a backup whose filter is above 1; the LIST is the type + 1, else the filter.
- **DRUM's kits:** presets for its five model kits (80 10 66 55 77) and variations of the standard one, never on the retired KIT values 1..3 (a preset's values load as they are).
- **DRUM-X's kits** (`eng_drumx.c` `DX_KITS[]`, the factory kit first; `DRUMX_PKIT[]`, a kit + 1 per preset): such a preset loads its kit as the project's one kit, with the sound, as one undo (`UNDO_KIT`: SAVE held brings the kit back). Presets 0 and 1 keep the kit, because every engine switch loads preset 0, which would otherwise wipe the project's kit. `track_sig` hashes the kit, so an edit of it between two loads is an undo of its own.
- **SAMPLE's variations** (`tools/gen_samples.py` `SET_VARIANTS`, `MORE` too) follow PERC's slot; the placeholder there is a PIANO alias, hidden from browsing.
- **SLICE's loops** (phase 9, `assets/breaks/`): BREAK (one bar at 120 BPM, its 16ths on the grid) and FUNK (SOURCE 5, after PIANO: two beats at 86.33 BPM), cut from drum takes made with the YuE2 music model (`ATTRIBUTION.txt` there), mono at 22.05 kHz like every sample. Their AUTO slices are what the firmware's own detector finds (`gen_samples.py` `slc_onsets` runs `slc_scan`'s integer steps; `slice_test` checks them to the sample). FUNK BREAK plays FUNK's sixteenths. Flash: FUNK costs 15.3 KB.

**One mute** (2.0, D 1A, the user's pick on design/mockups/v2_mutes.png; `ui_layer.c` `mute_keys`, `perform.c` `perf_mute`): the black keys 1–6 in GLO, FX and PERFORM are the six tracks' MUTE and do the same in each. A tap (let go within MENU › HOLD) toggles it, latched and saved (MIXER's card, HOME and every map agree): GLO's and FX's at once, PERFORM's at its LAUNCH point (stopped: at once). Held, the track is cut while the key is: FX's and GLO's from the press, PERFORM's once it is a hold (so a tap there cuts nothing). Muted means silent now: a MUTE ramps the track to 0 in 2.9 ms in the mix, as a cut does (`fx.c` `mix_part`), and still refuses new notes; before 2.0 a MUTE only refused new notes and let the sounding ones ring out. FX LATCH latches the effects only. FX's map draws the black keys as GLO's (the speaker, the note, the number; lit while the track sounds), its footer WHITE: EFFECTS, BLACK: MUTE, K1-4: MACROS (latched: WHITE: ON / OFF, BLACK: MUTE, OCT-: ALL OFF).

**One way back** (2.0, D 2A, the user's pick on design/mockups/v2_mutes.png; `ui.c` `mom_*`, `throw.c`): MOMENTARY's values (LFO held + a knob), the FX layer's macros and PERFORM's throws come back the same way, set once in MENU › CONTROL › RETURN (after STEP PREVIEW; the editor's id 20): SNAP (at once, MOMENTARY's before 2.0), 1 BEAT, 1 BAR (the default, the throws' before 2.0), in a line. What starts it stays: LFO let go (or the menu, a dialog, NAME, a layer), FX let go (a locked FX layer: closed), a PERFORM knob at rest (120 ms). OCT+ keeps them in all three while held: LFO held, FX held, PERFORM open (OCT+ breathes then). A list's value (a TYPE, a WAVE, MIXER's MUTE) comes back at once, not through the values between; a value something else writes while it comes back (an edit, automation, a load) stays as written. FX's macros used to snap back when FX was let go; FX + OCT- (latched) and the menu still turn them off at once. The footer says it while something would come back (MOMENTARY's page, FX's map, PERFORM's): LET GO: BACK OVER 1 BAR (BACK OVER 1 BEAT, BACK), OCT+: KEEP, side by side. MOMENTARY's values and FX's macros share one list in the main loop (12 values: 8 LFO's, FX's 4); the throws keep their offsets in the audio interrupt, over what sounds (the sheet had them in the list too: there a lock or automation would wipe a throw at its next step), and only take RETURN's time (`thr_beats`, set each pass).

**The FX layer's map** (phase 9): fourteen effects, seven a row, an octave each (F3 .. E4 over F4 .. E5), in 30 px cells; the REPEATs' rate written 8, 16, 32 (the "1/" doesn't fit beside the key's letter). The four new ones, in the white keys after OCT DN (`perform.c`): GATE (the mix chopped on the sixteenths, open their first half; DEPTH how deep), DUB (the dry mix into the delay, its feedback up to 0.94 and its return at MIX's default while held; the echoes ring out after), FLANGE and VINYL (buffer effects: a jet sweep 0.3 .. 6 ms, a bar each way, fed back; a 0.555 Hz wow of ±0.4 %, a 4.5 kHz low-pass and crackle). Their icons, Fukiai's: a pulse, echoes trailing off, a comb, a record.

## LEDs

| LED | Meaning |
|---|---|
| Page button | Lit: the current page family. Breathing: its layer map is open. Glow: idle. |
| PLAY (green) | Playing |
| REC | Lit: a track is armed |
| OCT± | The octave. In a dialog, OCT+ breathes when it would act. |
| Keys | Notes sounding or held; the sequencer's and MIDI-in notes; the current scale (SCALE LEDS); a layer's choices; the step grid on drum pages |

## Visual tokens: BLOCKS (direction B, chosen 2026-10-07)

**Type:** Barlow Condensed, SIL OFL 1.1. Labels are lighter than the bold values.

| Face | Font | Use |
|---|---|---|
| S | Medium 13 px | Labels, units, lists |
| M | Bold 16 px | Names, the header, values too wide for V |
| V | Bold 24 px | Knob card values (BLOCKS): three figures and a unit fit a card |
| L | Bold 28 px | Big numbers |

**Palette SKIFF**: neutral, so the colour belongs to the tracks.

| Role | Colour |
|---|---|
| BG | #0C0C0C |
| SURF (tiles) | #1E1E1E |
| TEXT, THEME, ACCENT | white |
| REC | red |
| MID, DIM, LINE | derived greys |

**Track colours:**

| Track | Colour |
|---|---|
| 1 | vermilion #FF5A36 |
| 2 | teal #22C3B4 |
| 3 | amber #FFBF00 |
| 4 | violet #9580FF |
| 5 | pink #FF6FAE |
| 6 | lime #A3E635 |

The colours differ in lightness as well as hue, and always appear with the track's number. Other palettes give every track their THEME colour.

**BLOCKS**, the look (`firmware/src/ui_blocks.c`, `ui_home.c`). U2: the only one; FLAT and LINE and the MENU's STYLE are retired (a saved STYLE byte is left as it was):
- Square tiles on BG, with no dividers.
- **Header:** the transport on the left: the play state, which becomes the REC mark while a track is armed; the selected track's bar.beat (grey 1.1 when stopped); then "124 BPM". At the right, as the mockup's, the page's title in MID (HOME: the project's name; a layer its own: GLO SET, PERFORM BAR). The octave sits before the title only while shifted; when the title leaves too little room before the BPM (FM6's OPERATOR n), drop the "OCT" word and keep the signed number, or hide it rather than cover the BPM (#70). The battery after it at every level (charging on USB). A message starts right of the BPM, at x 106 or later, and takes the title's place for a while.
- **Knob cards:**
  - The label (S) sits top left.
  - The value is big (V), with its unit right-aligned on the same baseline. The value drops to M, then S, when it is too wide.
  - The gauge is a 3 px bar of the track's colour along the bottom edge.
  - The knob just turned is a whole accent tile, with everything on it in INK.
  - Card icons are dropped, except MOTION's mark. Values snap: no rolling digits.
- **Graphs** are drawn in the selected track's colour.
- **Dialogs** (`ui_blocks.c`):
  - A SURF tile, square, over the page dimmed, as the mockup's: the page drawn again with every colour 70 % of the way to BG (`confirm_backdrop`; the header stays itself, so REC shows).
  - The question, then what it is about: a name, or a sequence's steps and notes.
  - The detail and undo lines in the mockup's sentence case: 16 steps, 9 notes; hold SAVE: undo, or can't be undone in REC's colour (U4: the undo hint's words, in the explanations' sentence case). The second covers project and user-sound overwrites, a project load (U1: LOAD PROJECT B?, the project's name under it), erasing a user sound, and clearing an entry or a pattern. Names keep their capitals.
  - CANCEL (OCT−) is the filled button. The action (OCT+) is outlined in REC's colour and named by its verb: CLEAR, OVERWRITE, INITIALIZE, ERASE. Each key sits under its button.
- **TAKE box:** see TAKE in detail.
- **Footer:** one line, as the mockup's, with no step strip: the selected track's number, big in its colour, then its engine and sound; the page's title and n/m at the right. On HOME it says HOLD HOME: SHIFT; on an action page or a drum grid, the hints (OCT+: LOAD  OCT-: BACK; PAGE 1/2  KEYS: STEPS); on NOTES, PRESETS: STEP  KEYS: PICK (KEYS: WRITE with REC armed); on FEEL, PRESETS: STEP; on PATTERN, PRESETS: DIR.
- **Key names are words**, as the mockup writes them (`gfx.c` `kc_plain`): no keycaps. A hint reads KEY: WHAT (PLAY: RESTART, HOLD HOME: SHIFT); a key under a dialog's button is its name (OCT-); a state is a coloured word (the mixer's REC in red, MUTE in MID).
- **ENV:** three LINE guides behind the curve (its top, middle and foot) and its points as 5 px blocks (the peak, the decay's end, the release's start); the knob just turned has its point white.
- **One level of tiles:** SURF tiles on the black ground, the selection a white tile with INK. The layer maps' cells and the NAME screen's keys sit on the ground with no panel under them. Every shape is square, the "rounded in every style" pills included: the menu's tabs, its page dots, the COLOR swatches.
- **Menu:** ON and OFF are words like its other values (OFF in DIM), and there are no rules between its rows.
- **A footer short of room** for its hints keeps only the page's n/m at the right (the header names the page), so every hint keeps its word. An action card's value is its key's name, in M (LOAD: OCT+).
- **A page's own actions are cards** (U2, the review's G3): EDIT always means the EDIT pages. SAVE › USER's KNOB 3 picks ERASE and, one step further, NAME; SAVE › PROJECT's KNOB 2 picks DELETE and, one step further, NAME (1.2.1, #6), and its KNOB 3 is NEW on an empty slot (a blank project, the power-on one); SONG's PATTERN knob ends in COPY, then CLEAR; AUTOMATION's VALUE turned past its lowest shows DELETE. OCT+ does the picked card, OCT− drops it.
- **The header names the page, the footer where it is** (U4, the review's L5): the header the page's name (EDIT: the engine's own, OSC, KIT), the footer at the right only its place in the family (1/4; with the page's icon), so a name is said once. NOTES' scale (C MIN, the footer's before) sits under the roll at its right.
- **No silent dead ends** (U4, F7): SELECT at a family's first or last page flashes the footer's n/m; OCT+ on an action page with nothing picked says PICK WITH A KNOB FIRST; a second layer's button says ONE LAYER AT A TIME; GLO's TAP counts the taps before the tempo (TAP 2 MORE); PRESETS on SLICES picks the slice (KNOB 1's).
- **MENU > SYSTEM > RESET SETTINGS** (U4, A3): its dialog on the page under the MENU, then every MENU setting as a new unit has it (Skiff's palette, BPM LOCK ON, CLOCK INT, MIDI IN CH1-6, TUNE 0, HINTS ON ..); the calibration, the favourites and the music stay.
- **One word for one thing** (U4, the review's L1–L3; `params.c` TP / GP, the engines' rows, the cards' own words): spelled out where the card has room (Barlow Condensed fits 8 letters), the vocabulary of the review's appendix: LEVEL, ATTACK DECAY SUSTAIN RELEASE, CUTOFF PITCH SHAPE (the ENV's and LFO's routings; (2.0) MOD's grid: CUT), PHASE, OCTAVE, SWING, CHANCE, ORDER, SCALE, QUANTIZE, TRANSP, LENGTH, DIST CHORUS DELAY REVERB, VOICE GLIDE GL MODE PRIORITY DETUNE, SLICER PATTERN, CHORD VOICING, CLOCK FEEDBACK COLOR ROUTE ENGINE; on the engines DETUNE, NOISE, CUTOFF, DRIVE, KEYTRK, CRUSH, SWEEP, VIBRATO, VOWEL, BREATH, RANDOM, PITCH (every semitone pitch: SAMPLE's and the drums' TUNE, VOICE's SHIFT, TRIO's PITCH2 PITCH3), SOURCE, DENSITY, SPREAD, STRUCT, BRIGHT, ACCENT, EXCITE, DRIFT, ALGO, MOD LVL, RATIO, MOD ENV, VEL MOD, PATCH (FM6's SLOT), MORPH, COLOR (DRUM-X's TONE, its sheet's COLR); the cards VELOCITY, CHANCE, RATCHET, SOURCE DEST AMOUNT (MOD). One name, one meaning (L2): DEST for a destination (DST was distortion too), RVRS for reverse (REV is reverb: SLICE's, the play direction's), PLAYED for the arp's order as played. The lanes CL HAT and OP HAT in full, BD SD CH OH on the grid. AUTOMATION's PARAM card shows ENV or LFO on its label's line (ENV ATTACK). The web editor's library files keyed by the labels of before (LVL ATK ..) still load: `legacyKeys` takes them to today's by id.
- **Messages say the way out** (U4, the review's F3, L6): a refusal names what to do, on the header's second line where it needs one (`ui.c` `ui_way`): PATTERNS FULL, then SAVE > PATTERNS: CLEAR; AUTOMATION FULL, then AUTOMATION: DELETE ONE; BPM LOCKED, then GLO + SELECT: TEMPO; PLAY ONE LOOP FIRST, DISARM REC FOR A TAKE, NOTHING BEFORE TO JOIN, STOP TO ERASE / RENAME / SAVE as it is. Musicians' words for developers': CLOCK IS EXTERNAL, SLICES NOW MANUAL, PICK A USR SAMPLE FIRST, SAVED UNTIL POWER OFF (no flash), STORAGE ERROR, UPDATE MODE, KEYS RECORD LANES, HOLD [GLO]: SHORTCUTS, UNDONE: SEQUENCE / MIX / SCALE / REC, PATTERN 06 CLEARED, SOUND INITIALIZED. After a crash the session is not restored on purpose, and the boot says SESSION NOT RESTORED. MIDI IN into a track not selected marks its number on HOME for 0.4 s (was a header message every 4 s, covering the title). Values show up to 7 letters (SPRING, MELLOW; were cut at 5), the card falling to M, then S, where V is too wide. AUTOMATION names a lane as its kit plays it (CONGA). The REC layer says CLICK, COUNT-IN, CLICK LVL as MENU > AUDIO does; GLOBAL's swing is SWING+ (it adds to PATTERN's SWG).
- **Every page has a graph of its own** (U5, the review's V1; `ui_pgraph.c`, design/mock_u5.py): (2.0, D 1A, design/mockups/v2_mod.png) MOD the seven fixed routings as a grid, the LFO and the ENV a row each, PITCH CUT SHAPE AMP a column (the ENV's AMP the envelope itself: -), the route picked white, over the matrix's four slots as routes 8 to 11 (ENV DEST's and LFO DEST's own graphs went with their pages); DELAY the dry hit and an echo every TIME, the first MIX high, each next FEEDBACK lower (`fx.c`), over the bar's beats; REVERB the first reflections and the tail SIZE long, its top dimmed by DAMP, SPRING's coil; CHORUS the dry line and its drifting copy (one modulated delay), (phase 9) FEEDBACK its copy's copy fainter behind, FLANGER and PHASER their response across the spectrum (the comb, the two notches) drawn as FLT's curve; REVERB's HALL the tail twice ROOM's; FX › DRIVE (phase 9) the TYPE's transfer curve at the DIST, the straight line behind it, and one sine cycle through it as `fx.c` runs it (CRUSH's hold, TONE's low-pass in it); GLOBAL a bar's 16 steps, the off steps late by SWING+ over where they would be, the long:short share; VOICE what the mode does and the GLIDE between two notes; VOICE 2 the engine's voices, those sounding lit, the next one outlined (POLY, ROT), DETUNE's UNI spread; ANALOG's OSC the wave and its detuned second, FLT the low pass's curve, its peak at CUTOFF as high as RES; ARP and ARP 2 one graph, the arp's notes over 16 steps from the notes held (else C E G, said), GATE a tile's length, SWING, the steps CHANCE skips outlined. Of a page's curves the knob just turned (else the biggest amount) is the track's colour, the others MID, a zero one DIM. The other engines' EDIT pages keep their charts (FM6, WHEEL, SAMPLE) or the output's scope; FM6's OPERATOR and ALL OPS draw the note as the engine plays it (FM6 LAB, below).
- **The page families** (U3, the review's structure; `params.c` `PAGES`, `ui.c` `page_visible`):
  - FX holds the track's effects (FX, (phase 9) DRIVE: DIST TYPE TONE, SLICER, DUCK). (2.0, D 2A, the user's pick on design/mockups/v2_mod.png) DRIVE's TYPEs are INSERT's shapers (`fx.c` `ins_shape`, one curve for both, in DIST's own low cut, tone and make-up): SOFT (with DIST's bias, as before), HARD, FOLD, CRUSH (BITS by its old name, the same sound), FUZZ (new, last: a saved TYPE stays). HARD and FOLD sound a little different from 1.x's: the ceiling 24000 (was 24576), the folds x 1.5 (were x 2). The buses every track shares are GLO's, after MIXER: DELAY, REVERB (TYPE SIZE DAMP; TYPE ROOM SPRING, phase 9 HALL), CHORUS (RATE DEPTH, were CRT CDP; phase 9 MODE FEEDBACK), then GLOBAL (BPM, SWING). Its footer counts the five as one (MIXER 1/5).
  - (Phase 9) ENV's third page, ENV 2 (ATTACK DECAY SUSTAIN RELEASE, the ENV page's graph), is a second envelope for the MOD matrix only (source ENV2; `voice.c` `env2_tick`, ticked only while a slot uses it).
  - SEQ goes by task: (2.0) NOTES, FEEL, PATTERN, AUTOMATION, SONG last (STEP, ROLL, CHANCE, PLAY before). On a drum track NOTES and FEEL are the grid.
  - AUTOMATION is 1.1.5's AUTO LIST: the records, and a first row of its own (ROW ALL) with PLAY on KNOB 2 and CLEAR on KNOB 4. The AUTOMATION page of before, its counts and scope, is gone.
  - EDIT > LANES (DRUM) is one page: KNOB 1 the lane (the grid's too), KNOB 2 its LEVEL, (phase 9) KNOB 3 its TUNE (±12 semitones, 0.0 as the kit has it), KNOB 4 its DECAY (a signed offset, 0 as the kit has it), the eight as faders; a held step's KNOB 2 3 4 lock that lane's. DRUM-X has its sheet's LEVEL instead (and tunes its sounds there).
  - (2.0, #36, the user's pick 1A of design/mockups/v2_feats.png) EDIT > LANE MIX, DRUM's and DRUM-X's (after LANES, after SOUND): KNOB 1 the lane (DRUM: LANES' and the grid's; DRUM-X: the sheet's; a key struck picks its lane, the same key again too: `ui.c` `lane_follow`, `seq.c` `last_hits`), KNOB 2 its LEVEL, KNOB 3 its PAN (-64 .. 63 as the track's, the card -31 %); a held step's KNOB 2 3 lock them. The panel (`ui_graph.c` `graph_lmix`): LANES' faders 20 px shorter (`lane_fader`, its themes' too), under each its pan, a row 20 px wide, C its middle, the pan a 5 x 7 mark (CRYPT a skull on a dotted leader, NEON a spark on a glass line, QUARK a nucleus on its energy levels) and written as the MIXER writes it (C, L31, R100: `pan_txt`), then the name's tile 2 px lower than LANES'.
  - (#21) EDIT > SAMPLES (a SAMPLE track on a USR set with samples, after EDIT 2): KNOB 1 the sample (its number; a key played picks its sample), KNOB 2 its LEVEL (square law as LANES', 100 % the sample as uploaded), KNOB 3 and 4 its REVERB and DELAY, added to the track's sends (127 at most in all; `fx.c` `smx_render`: before DRIVE and the INSERT, after the track's LEVEL, mute, SLICER and DUCK); the panel its waveform, as SET's. Kept with the slot (`slice_store.c`: after SLICE's record, for that slot header only) once the page is left with the music stopped (MIX SAVED); never a lock or automation. The editor's Samples tab and `fm1_sample_upload.py mix` set the same values (cmd 81 SMP_MIX).
  - (2.0, #35, the user's pick A of design/mockups/v2_kits.png) DRUM's KIT goes on to **USR1–USR3**, a user sample slot as the kit (`eng_drum.c` `drum_user`, `drum_smp`): each lane plays the slot's narrowest zone holding its key (`drum_user_zone`, the lanes' GM notes), the sample as recorded whatever the zone's root; the kit's and LANES' TUNE and DECAY (shorter only), LANES' LEVEL, LANE MIX's PAN, the open hat cut by the closed one, the grid, DUCK's KICK and DRIVE as on a synthesized kit; TONE under its middle a low-pass. A lane without a zone is silent, its fader and name dim (`ui_graph.c` `lane_fader_of`); the sound's name is the slot's (MY KIT) unless a user sound is loaded. The editor's Samples tab builds such a slot (Drum kit: a WAV a lane, cut to where the sound is, each with a level; one gain for the kit, so the files keep their levels to each other: `kitZones`).
  - A page that changes nothing on the track's engine is not there: ENV, ENV 2, VOICE, VOICE 2 and SCALE on DRUM and DRUM-X (`engine_t.oneshot`), ENV and ENV 2 on FM6 (`ownenv`); VOICE 2 is ALLOC, DTUNE and (1.3) SPREAD (PAN, MUTE: MIXER's). A card that does nothing there is dimmed: (2.0) MOD's routes the LFO to PITCH CUT SHAPE on the hits, the ENV's there and on FM6 (`mod_dead`; ENV DEST was hidden there), DRUM's KICK on a model kit, (1.3) LFO's RATE while LFO 2 SYNC is on, SPREAD on MONO and LEGATO. A button with no page says so (NO ENV ON DRUM); a page a track no longer has goes to its family's first (or HOME) and says so too.
  - Every family reopens the page it was left on (SAVE and GLO too), and the MENU closes back onto the page it was opened over.
  - SAVE › TOOLS is gone (CLEAR is the REC and SEQ TOOLS layers', INIT the EDIT layer's), and so is GLO › SYSTEM: CLOCK and MIDI IN are MENU › CONTROL's ((1.3) MENU › MIDI's), TUNE MENU › AUDIO's, CPU (read only) MENU › SYSTEM's. (Skiff 1.3) CPU picks 1 CORE or 2 CORES (BETA, from the next power-on; RESTART until then), then the load: the second core renders tracks (fx.c mix_block: the split by the last block's cost, the sums per core, bit for bit as one core; tests/dualcore_test.c), and the voice budget is 12 instead of 8 while it runs (core.h VOICE_BUDGET). A core that misreads or stops answering is held: one core, CORE 2 FAILED, the row back to 1 CORE. (Skiff) CPU CLOCK, after CPU (SYSTEM's seven rows scroll): 360 MHZ (the boot stage's, the default) or 320 MHZ, JieLi's rated clock, switched at boot from RAM with the IRQs off, the cache and the flash controller retimed around the PLL change (curist's hal/fm1_clk320.h, hal/fm1_clk.h, main.c), the screen up, before the second core, the audio and USB; from the next power-on (RESTART, STOP while playing, as CPU). Clock registers not as the boot stage leaves them: not switched, the header says NO 320 MHZ and why, the row REFUSED; a reset during the switch is no failed boot, and no switch is tried again until power-off; a soft reset keeps the PLL, and a kept 320 is not switched again. ABOUT shows the clock (CPU 320 MHZ), the console's `status` cpu_mhz, clk320 and the loop-measured clk320_loop_mhz.
  - CLOCK, MIDI IN and TUNE are the unit's (`ui.c` `unit_g_*`, settings bytes [15][18..20]): a project still stores them (for older firmware) but a load leaves them; the first boot with U3 takes the restored session's.
- **Sentence case** for the lines that explain, as the mockup's dialog and TAKE box: recording starts after 1, list ALL to add sounds, teach each button and knob. Titles, labels, values and key hints stay in capitals.
- **Track numbers** (`icons.c` `cv_trk`): no cushions. The number stands alone in its track's colour, as on the HOME lanes: V where Felucca's 16 px cushion went (footer, MIXER, the GLO layer's SOLO keys), M where its 12 px one went (the GLO cards, the layers' mute keys). It's INK on a light tile or a held key, and DIM for a silent track. The TAKE box's chip, the number on its colour, is the one square, as in the mockup.

**Defaults:** a new unit gets SKIFF. **THEME** (the review's R1; MENU > THEME, was COLOR; `tools/gen_ui_palettes.py`): SKIFF, HI-CON, PAPER, CRYPT, NEON, QUARK. A unit on HI-CON or PAPER keeps it; on any other palette of before (GREY, GREEN, AMBER, ICE, VIOLET, ROSE, NIGHT, MONO, Felucca's twenty) it starts on SKIFF. CRYPT (`gfx.c` `ux.crypt`) brings its own font and widgets too (the theming phases C2-C4); NEON (`ux.neon`) CRYPT's
font and its own widgets (N1-N8); QUARK (`ux.quark`) Skiff's Barlow faces and its own widgets (Q1-Q5).

**THEME CUSTOM** (#27, @tradiuz; after Felucca 1.6's CUSTOM palette; design/mock_c27.py, mockups/c27_sheet.png): the
user's eleven colours from the web editor (BG SURF TEXT THEME ACCENT and the six tracks'), in a look they pick: a
built-in THEME's faces and widgets (`gfx.c` `ux_cpal` over `palette_set`, `ui.c` `theme_apply`). THEME's last value
while a set is stored. The other tokens come from the five as for every palette, and each look's motifs are drawn in
them (CRYPT's skulls, stone and pixel letters, NEON's chrome, bands and high score table, QUARK's brackets, fills and
collisions); a motif's own art keeps its colours (the torch's flame, the sun's bands). The SMK-37's pads take the
tracks' colours as the screen does. **PLAYHEAD** (#27; MENU › DISPLAY, under THEME): THEME, the figure at HOME's
playhead on the selected lane (CRYPT's torch, NEON's car, QUARK's collision), or LINE, the playhead's line only.

**CRYPT** (THEME; design/mock_crypt.py B TORCHLIT with A's ASCII graphs and the motifs the user asked for: skulls, snakes, torches, swords; `ui_crypt.c`):
- **Letters:** our own 5×7 pixel glyphs (`tools/gen_pixel_font.py`), every pixel full or empty: labels 1×, names a 1× bold, card values 2×, MENU > LARGE's 3×; capitals only (lower case folds to them). Each face keeps its Barlow face's line height and centres its capitals on Barlow's, so every layout holds; `gfx.c`'s AF_S AF_M AF_L AF_V and CAP_IN follow the theme.
- **Stone and frames:** stone (a speckled tile, `gfx.c` `cr_stone`) wherever BLOCKS lays a SURF tile (`cv_rrect`, so every cell, list and box follows): the graph panel, cards, HOME's lanes, the MIXER's strips, the layer maps' and SHIFT's cells, the MENU's rows, the dialog (a tablet); the ASCII double line round the panel, the MENU and the dialog. The hot card is lit, framed in amber; the MENU's selected row and tabs are square.
- **The motifs:** a card's gauge is a sword (the hilt, the blade as long as the value, its edge gold; MENU › LARGE's tall cards too); REC is a blood drop (another track armed: its outline); the header ends in a castle wall's crenels; HOME's playhead lines are gold with a torch (twice the size, in a dark outline, after the hardware test) on the selected track's; the MIXER's levels are torches (ember at the foot, gold at the top, a flame on it; muted: out, its mark a crossed skull); the splash a skull between two torches.
- **ASCII steps:** HOME's step cells and the drum grid's hits are characters: # a note or a hit (the drum grid's accents bright), - a tie, . nothing.
- **The MENU as an old book's contents:** a dotted leader from each name to its value (AUTOMATION's rows too).
- **The SEQ pages:** PATTERN's steps ASCII (# - .); the piano roll's notes swords (a gold crossguard where the note
  starts, the blade as long as it lasts, its edge bright), its cursor and playhead gold; the step tiles plaques, their
  border engraved (the edited one's gold); the pool's and SONG's tiles stone.
- **The graphs as ASCII art** (`cr_art`: a level a 6 px character column, joined by 2 px strokes, _ / \ _| as the curve moves): ENV with skulls on its three points over a ':' shade; LFO a snake along its path up to where the LFO is now; DELAY # columns under skulls, the dry hit under a torch; REVERB's tail in shade characters # * + : ., its top blood (DAMP), SPRING's coil ~; CHORUS's copy a snake; GLOBAL # columns, ^ where an off step would be; VOICE's glide timed by a sword; VOICE 2's voices skulls, the next one in a gold double frame; OSC's wave with blood in its teeth; FLT's curve with a skull on its peak and a dagger at the cutoff; ARP's notes #, a skipped one a crossed skull; (phase 9) DRIVE's curve a snake, its head at the end, the straight line faint behind it, its sine as OSC's wave, blood in its teeth; FLANGER's and PHASER's response as FLT's curve over its shade, a skull on the middle peak; CHORUS's FEEDBACK copy MID art behind the snake; HALL as ROOM's tail. FX's sends and DRUM LANES' levels are the MIXER's torches; SLICER's steps # columns (a GATE step ':' as high as it stays open, STUT '='); WHEEL's drawbars # columns as far out as they are; DUCK the level as ASCII art over the shade, the source's hits ^ under it. The ':' shade is DIM (LINE vanished into the stone's specks).

**The LFO's graphs** (every theme; LFO, LFO 2; `ui_graph.c` `lfo_view_at`): the LFO over 2 s from its retrigger (a phrase's first note), as it runs: RATE's cycles in it, PHASE where it starts, FADE rising from nothing; a dot (CRYPT: the snake's head) where it is now, crossing the whole graph in the 2 s; past them the window scrolls, the last 2 s. (The hardware test: RATE and FADE did nothing to the old two fixed cycles.)

**NEON** (THEME, synthwave; design/mock_neon.py A SUNSET, the user's pick, with all four motifs: the car, the sun and
the grid, VHS snow, the high score table; `ui_neon.c`):
- **Letters:** CRYPT's pixel faces. The values in chrome, slanted (`gfx.c` `cv_fx_on`: a px right every 3 rows above the
  baseline, a ramp down the capitals: white, sky blue, violet at the horizon, orange to gold); the header's BPM and the
  track numbers slanted. Next to a unit the value's top passes over the unit's (its gap at the unit's height).
- **Glass and dusk:** every SURF cell glass (its top edge lit violet); the hot card a dusk (gold, orange, pink) in a
  white frame; a card's gauge an 80s dash bargraph (slanted segments, cyan, violet, pink as they climb), MENU › LARGE's
  too. A neon rule (pink, violet, cyan) under the header and over the footer; REC a red light, glowing.
- **HOME:** a highway: the selected lane a neon sign (its colour into violet), the others asphalt with the lane's paint
  between them; a step a lit tube, nothing a dim dash; the playhead a cyan laser and on the selected lane the car (24 x 8,
  after the hardware test: 2x hid too many steps) in a dark outline, its nose at the playhead, its light trails back over
  the steps just played.
- **MIXER, PERFORM:** VU columns (cyan, violet, pink, gold), LEVEL's cap white; a muted track no signal: VHS snow (MUTE;
  selected: dark snow on its band).
- **The scope** (HOME tapped on HOME; an engine's EDIT without a chart): the sound as the skyline: the wave a ridge of
  vector mountains over the horizon, the striped sun behind it swelling with how loud it is, the grid floor running
  toward the viewer. CRYPT's: the sound burns: fire up from the foot as tall as the wave (ember, amber, gold, flame),
  flickering, behind it a skull whose eyes go from ember to flame with the loudness.
- **MENU:** an arcade's high score table: ranks 1ST..6TH, slanted, in the tracks' colours; dotted leaders; values cyan;
  the selected row and tab a sunset bar. The dialog: night glass in a double neon frame, the page under it paused like a
  tape (every other row darker); CANCEL lit cyan, the action red neon. The splash: the striped sun between palms, the
  grid floor, the car.
- **The graphs:** every curve glows (`cv_line_t`: a ring out of it on the background only). ENV over the sun's bands, its
  points sparkles; LFO a ridge of vector mountains, a white-hot trail into a sparkle where it is now; DELAY's echoes on the
  grid toward a dim sun, ghosting wider; FLT and DUCK over the sun's bands, and (phase 9) DRIVE's curve, FLANGER's and PHASER's
  response and REVERB's tail (its reflections lit tubes, DAMP's share violet), sparkles at DRIVE's ends and at the
  chorus copy's, a ghost behind it its FEEDBACK; GLOBAL, FX's sends, LANES and WHEEL VU columns;
  SLICER, the piano roll's notes, the drum grid's hits and the step tiles lit tubes; the cursor a cyan laser.
- **The SEQ pages:** PATTERN's steps lit tubes, the cursor a laser under them; the roll's playhead a laser; a list's
  selected row a sunset bar (AUTOMATION, the presets), AUTOMATION's values cyan on dotted leaders; the pool's tiles glass,
  the pick a dusk; SONG's band its colour into violet, the entry playing a lit tube.

**QUARK** (THEME, particle physics; design/mock_quark.py A COLLIDER, the user's pick, with all four motifs: beams and
collisions, dark matter, the hazard sign, the polar scope; `ui_quark.c`, its circles on `sine_i`):
- **The colour charges:** the six tracks red, green, blue and their anticolours cyan, magenta, yellow; values and curves
  cyan, the one active thing gold. Skiff's Barlow faces.
- **The cells:** a card is the periodic table's cell: bracket corners, the knob's number top right where the label leaves
  room, the gauge energy levels (a dot every 4 px); the hot cell cyan. The panel in an instrument's bracket corners;
  dotted rules under the header and over the footer; REC a red nucleus.
- **HOME:** the lanes beam pipes (the selected one a beam, its colour faint, its walls lit; the others two rails); a note
  a bunch of particles, spent (a ring) once played this bar, a tie its trail; the playhead the beam line, dotted gold, and
  on the selected lane a collision whose tracks fly out a new way each step.
- **MIXER, PERFORM:** the level a calorimeter tower (its cells the energy deposited, a gold cap); a muted track dark
  matter: a dotted, empty tower (DARK), a dotted outline round its row.
- **MENU:** an instrument's settings: the values readouts in brackets on dotted leaders, the row a cyan tint in corners,
  its value gold. The dialog: gold corners, what can't be undone under the radiation sign, CANCEL cyan, the action in
  red brackets. The splash: an atom in a collision's tracks.
- **The graphs:** ENV a Monte Carlo fill under the curve, its points nuclei, DECAY's and RELEASE's half-lives written
  on it; LFO the orbit and its projection (the electron's height drawn out as the wave, dashed to where it is now);
  DELAY a decay chain; FLT and DUCK Monte Carlo fills, and (phase 9) DRIVE's curve (a nucleus on its top, the electron at
  its sine's end), FLANGER's and PHASER's response and REVERB's tail (its reflections towers, DAMP's share sparse);
  CHORUS's copy ends in the electron, its FEEDBACK copy dotted; GLOBAL, FX's sends, LANES, WHEEL and SLICER calorimeter towers;
  the roll's notes bubble tracks from their vertex (its cursor in gold corners); the drum grid's hits and PATTERN's steps
  bunches. The scope (HOME tapped on HOME; 1.3.3, Q6): the 1.2 tour video's event display, live: each note a track
  plays a collision at the vertex, three particles flying out in the track's colour and curling in the field, then
  fading; round the beam pipe's rings the calorimeter, eight towers a track, as loud as the track is; the vertex gold,
  brighter just after a hit. (It replaced Q5's wave wrapped round the detector.)

**The web editor looks like the unit** (1.0; `design/editor_looks.png` A BLOCKS, the user's pick over a softer app
look): the night, each section a tile, white values, Barlow Condensed for headings, labels and values, each track its
colour (the track buttons, the mixer's strips, the Song tab's rows), the sliders and the steps in the selected track's.
Once connected it follows the unit's THEME: SKIFF and HI-CON dark, PAPER light (cream tiles, ink), CRYPT (stone, bone,
amber headings), NEON (glass, pink headings glowing) or QUARK (a detector's dark, cyan headings on a dotted rule), each
with its tracks' colours; before that, the system's light or dark (`web/editor.html`, `data-theme`; colours only as
tokens). (#27) In THEME CUSTOM the editor's tokens are the set's colours and its look's faces stay (`customTokens`);
Settings › Device display edits the set: eleven colour wells, Look, Start from… (a THEME's own colours), shown on the
device as they change (RAM only), Save, Revert, Remove, and a warning for colours hard to read (nothing is refused).

**The web editor reaches what the device has** (W, the review's W1–W7; `web/editor.html`, `web/EDITOR_PROTOCOL.md`):
a step's FLAM, COND and NUDGE and its locks; the MENU settings (Settings); the mixer's REV; the DRUM-X kit (a DRUM-X
track's Sound tab); PLAY, STOP and REC in the header; the projects' names (SAVE AS, RENAME); the Song tab: each
track's list (SET, COPY, PLAY, REPS, CLEAR), SONG, LAUNCH, the scenes, the staged mutes, the pattern pool (CLEAR an
unused one). Each acts through the device's own code and the device says so on its screen; what can't be undone is
asked first, as on the device. Nothing of these is pushed: the editor reads what it shows again about once a second.

**Screen grid:** Felucca's, so every page inherits the restyle (`firmware/src/ui.c`); BLOCKS' footer is one line, so its panel is 16 px taller (`foot_less`, `graph_h`):

| Region | Size |
|---|---|
| Header | 24 px |
| Cards | 4 × 57×44 at y 28 |
| Panel | y 76–198 (BLOCKS: 76–214: ADSR and LFO grow into it, the fixed layouts sit centred) |
| Footer | y 202–240 (BLOCKS: 218–240, its line where the names' line was) |

**HOME overview** (`firmware/src/ui_home.c`):

| Element | Spec |
|---|---|
| Lanes | 31 px tiles with 3 px between them (the mockup's) |
| Number | V, in the track's colour; INK on the selected track's band |
| Name | The sound (M) over its engine (S), from x 28 |
| Step cells | 16 cells of 6×16 px from x 100, a 2 px gap, 2 px more between beats; an empty step is a LINE block |
| Playhead | A 1 px line before the step playing |

**Still open:**
- the 20 MHz LCD clock, `LCD_BAUD=2`, to try on hardware.
