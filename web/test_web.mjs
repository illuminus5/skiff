// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer, the v5 drum grid in steps and user presets), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf, flashSpan, validateStockPackage } from "./fm1pkg.js";
import { Updater, pack7, unpack7, WriteMeter, packageFor, needsStockUpdate, stockRefusal } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;
/* what lives outside web/, each overridable (a web-only checkout passes them, or the checks skip):
   FELUCCA_DESC  the firmware's parameter / protocol table (tests/descdump.c -> build/host/desc.json)
   FELUCCA_TOOLS the firmware's tools (tools/fm1_sample_upload.py, the sample format's reference)
   FELUCCA_ROOT  the Felucca tree with a ./build.sh build (the package check) */
const DESC = process.env.FELUCCA_DESC || join(HERE, "../build/host/desc.json");
const TOOLS = process.env.FELUCCA_TOOLS || join(HERE, "../tools");
const FW_ROOT = process.env.FELUCCA_ROOT || join(HERE, "..");

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, songReply, req, Link, parseWav, resample, normalize, takeSample, kitZones, LANES, portPairs, autoTrim, zoomView, rootFromName, buildSlot, makeMockDevice, CMD, SMP,
   UP, bank, capturePatch, auditionPatch, lightRefuses, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, parseNotes, parseHits, hitsText, gridFromSteps, LANE_NOTE, LANE_OF, readDevicePreferences, devicePresetRows, TC_NAMES, engineOrder, ENGINE_ORDER, aliasOf, fmtValue, FM6, enumShown, F,
   FM4, fromDigital, fromPerc, DRUM_KIT_E, DX_KIT_DEF, DX_KITS, DRUMX_PKIT,
   PAL_BUILTIN, PAL_ROLES, colorWarnings, customTokens, hex565, from565hex, mix565, luma565,
   MENU: typeof MENU === "undefined" ? null : MENU, MENU_TABS: typeof MENU_TABS === "undefined" ? null : MENU_TABS,
   readDeviceMenu: typeof readDeviceMenu === "undefined" ? null : readDeviceMenu })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.nengines === 15 && info.engines[1] === "-" && info.engines[12] === "FM6" && info.engines[13] === "SLICE" && info.engines[14] === "DRUM-X"
 && info.engines[5] === "VOICE" && info.engines[6] === "TRIO" && info.engines[7] === "WHEEL" && info.engines[8] === "GRAIN" && info.engines[9] === "PHYS" && info.engines[10] === "DRUM" && info.engines[11] === "NOISE" && info.pcount === 143 && info.pe0 === 135 && info.engines[4] === "SAMPLE",
    "editor: INFO (Skiff 2.0: P_COUNT 143, P_E0 135 from its wide-ids tag)");
  {
    /* (Skiff 1.3) wide ids: a track id from 128 on ((2.0) E0..E7 since P_E0 135, after the lane pans 127..134) goes as
       scope 2 and comes back whole (GET, SET, DESC), in TRACK_PARAM's track byte (bit 6) and in a MOTION record's step
       byte (bit 6); 127 (a lane's PAN) is still scope 0 */
    const v0 = E.parse[E.CMD.GET](await rq(E.req.get(0, 138))).value;
    const a = E.req.set(0, 138, 10), s = E.parse[E.CMD.SET](await rq(a)), g = E.parse[E.CMD.GET](await rq(E.req.get(0, 138)));
    const d = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 142)));
    const tp = E.parse[E.CMD.TRACK_PARAM](await rq(E.req.trackParam(3, 137)));
    const e0 = E.req.get(0, 127);
    const mo = E.parse[E.CMD.MOTION](await rq(E.req.motion(0, 3, { step: 5, param: 139, value: 9 }, info.ntrk)));
    ok(a[1][0] === 2 && a[1][1] === 10 && s.scope === 0 && s.id === 138 && s.value === 10 && g.id === 138 && g.value === 10 &&
       d.scope === 0 && d.id === 142 && d.label === "KEYTRK" && tp.track === 3 && tp.id === 137 && e0[1][0] === 0 && e0[1][1] === 127 &&
       mo.events.some((e) => e.step === 5 && e.param === 139 && e.value === 9),
       "editor: wide ids: SET / GET 138 and DESC 142 as scope 2, TRACK_PARAM 137 (bit 6), a motion record of 139; 127 scope 0");
    await rq(E.req.motion(0, 2, {}, info.ntrk));
    await rq(E.req.set(0, 138, v0));
  }
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0; its descriptors: mockTables) is the
       track's: a factory preset keeps it, as ui.c apply_preset_to does (param_kept) */
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLICER,PATTERN,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT",
      "editor: SLICER parameters 45..48 over DESC");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 2 && off.p[46] === 7 && off.preset === 1, "editor: a factory preset keeps the SLICER (the track's)");
    await rq(E.req.set(0, 45, 0));
  }
  {
    /* the modulation matrix (core.h P_M1SRC..P_M4AMT = 49..60, just before P_E0 61): SRC / DST / AMT x 4, the
       DST names (20: more than 16) over DESC, every slot OFF after a factory preset (ui.c apply_preset_to) */
    const md = [];
    for (let i = 49; i < 61; i++) md.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(md.map((d) => d.label).join() === "SRC1,DEST1,AMT1,SRC2,DEST2,AMT2,SRC3,DEST3,AMT3,SRC4,DEST4,AMT4"
      && md[0].names.join() === "OFF,LFO,ENV,VEL,KEY,RAND,MODW,AT,EXPR,ENV2,S&H,SLEW" && md[1].names.length === 23
      && md[1].names[11] === "VIB" && md[1].names[19] === "E8" && md[1].names[22] === "TIME" && md[2].min === -64 && md[2].max === 63 && md.every((d) => d.def === 0),
      "editor: matrix parameters 49..60 over DESC (23 DST names: (1.3) DEPTH VEL TIME)");
    await rq(E.req.set(0, 52, 1));
    await rq(E.req.set(0, 53, 12));
    await rq(E.req.set(0, 54, -30));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 2));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[52] === 1 && on.p[53] === 12 && on.p[54] === -30 && off.p.slice(49, 61).every((v) => v === 0),
      "editor: a factory preset turns every matrix slot off");
  }
  {
    /* the chord keys (core.h P_CHRD, P_VOIC = 81, 82, just before P_E0 83): the track's, a factory preset keeps them */
    const c0 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 81)));
    const c1 = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 82)));
    ok(c0.label === "CHORD" && c0.names.join() === "OFF,DIA3,DIA7,MAJ,MIN,DOM7,MAJ7,MIN7,SUS4,POW" && c0.def === 0 &&
       c1.label === "VOICING" && c1.names.join() === "CLOSE,OPEN,INV1,INV2,+OCT" && c1.def === 0,
      "editor: the chord keys 81, 82 over DESC");
    await rq(E.req.set(0, 81, 2));
    await rq(E.req.set(0, 82, 4));
    await rq(E.req.preset(0, 3));
    const kept = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(kept.p[81] === 2 && kept.p[82] === 4 && kept.preset === 3, "editor: a factory preset keeps CHRD and VOIC (the track's)");
    await rq(E.req.set(0, 81, 0));
    /* the DRUM lane levels (core.h P_LN0..P_LN7 = 83..90, just before P_E0 91): percents, 100 % by default; the sound's,
       a factory preset sets them back */
    const lanes = [];
    for (let i = 83; i <= 90; i++) lanes.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(lanes.map((d) => d.label).join() === "KICK,SNARE,CLAP,CL HAT,OP HAT,TOM,RIM,BELL" &&
       lanes.every((d) => d.fmt === E.F.PCT && d.min === 0 && d.max === 127 && d.def === 127),
      "editor: the DRUM lane levels 83..90 over DESC (percent, 100 % default)");
    await rq(E.req.set(0, 84, 40));
    const lv = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 3));
    const lv2 = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(lv.p[84] === 40 && lv2.p[84] === 127, "editor: a lane level is the sound's: a factory preset sets it back to 100 %");
    await rq(E.req.set(0, 82, 0));
    /* (Skiff) a factory preset's other values (core.h preset_t more): ANALOG NEON LEAD (16) sets the LFO's RATE, FADE
       and PIT and GLIDE (9, 12, 13, 38); the next preset puts them back to their defaults */
    await rq(E.req.preset(0, 16));
    const nl = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 0));
    const sl = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(nl.preset === 16 && nl.p[9] === 86 && nl.p[12] === 92 && nl.p[13] === 11 && nl.p[38] === 28 &&
       sl.p[9] === 60 && sl.p[12] === 0 && sl.p[13] === 0 && sl.p[38] === 0,
      "editor: NEON LEAD sets its LFO and GLIDE, the next factory preset their defaults");
  }
  ok(E.CMD.SONG === 33 && E.CMD.UI_STATE === 34 && E.CMD.FAV_SET === 38, "editor: preferences preserve the existing SONG command");
  const baseInfo = [116,101,115,116,0, 0, 69, 27, 64, 61, 4];
  const untagged = E.parse[E.CMD.INFO]([...baseInfo, 15]);
  ok(!untagged.chainRows && !untagged.uiCaps, "editor: an untagged PR capability cannot masquerade as SONG or preferences");
  const oldSong = E.parse[E.CMD.INFO]([...baseInfo, 16]);
  ok(oldSong.chainRows === 16 && !oldSong.uiCaps, "editor: existing SONG firmware remains compatible without preference probes");
  const tagged = E.parse[E.CMD.INFO]([...baseInfo, 16, 0x55, 1, 11]);
  ok(tagged.chainRows === 16 && tagged.uiCaps === 11, "editor: tagged preference extension advertises its supported controls");
  const pending = E.parse[E.CMD.FAV_SET]([4, 0, 0, 64, 1]);
  ok(pending.rc === 4 && pending.on && pending.engine === 0, "editor: queued favorite response is applied while waiting for STOP");
  const names = [];
  for (let e = 0; e < info.nengines; e++) names.push(E.parse[E.CMD.NAMES](await rq(E.req.names(e))).names);
  let prefs = await E.readDevicePreferences(rq, info, names);
  ok(info.uiCaps === 25 && prefs.palettes.length === 6 && prefs.palettes[0] === "SKIFF" && prefs.palettes[1] === "HI-CON" && prefs.palettes[2] === "PAPER" && prefs.palettes[3] === "CRYPT" && prefs.palettes[4] === "NEON" && prefs.palettes[5] === "QUARK",
     "editor: preference capabilities (palette, favorites, (#27) COLORS) and palette names");
  const pal = E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 2)));
  ok(pal.rc === 0 && pal.palette === 2, "editor: display preference 0 (palette) round trip");
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 6))).rc === 1, "editor: out-of-range palette refused");   /* (5: QUARK) */
  ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(1, 1))).rc === 2, "editor: the retired font weight answers not supported");
  ok(E.parse[E.CMD.FAV_SET](await rq(E.req.favSet(info.nengines, 31, true))).rc === 1, "editor: empty user slot cannot be favorited");
  await rq(E.req.favSet(0, 0, true));
  await rq(E.req.uiSet(3, 1));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).length === 1 && prefs.favorites[0][0], "editor: favorites filter follows device state");
  m.state.favorites[0][0] = false; m.state.favorites[2][0] = true;
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[0][0] && prefs.favorites[2][0], "editor: panel-side favorite changes refresh");
  await rq(E.req.upStore(31, "FAVORITE"));
  await rq(E.req.favSet(info.nengines, 31, true));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(E.devicePresetRows(info, names, prefs).some((r) => r.user && r.preset === 31), "editor: saved user slot appears as favorite");
  await rq(E.req.upStore(31, "RENAMED"));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(prefs.favorites[info.nengines][31] && prefs.slots.slots[31].name === "RENAMED", "editor: overwrite retains star and refreshes name");
  await rq(E.req.upErase(31));
  prefs = await E.readDevicePreferences(rq, info, names, prefs);
  ok(!prefs.favorites[info.nengines][31] && !E.devicePresetRows(info, names, prefs).some((r) => r.user), "editor: erased slot disappears and loses star");
  {   /* the lists in the device's order (engines.c ENGINE_ORDER): FM6 second, DRUM then (Skiff) DRUM-X last, "-" never;
         the numbers stay */
    const shown = E.engineOrder(info.engines).map((i) => info.engines[i]);
    ok(shown.join() === "ANALOG,FM6,PHASE,LOFI,SAMPLE,VOICE,TRIO,WHEEL,GRAIN,PHYS,NOISE,SLICE,DRUM,DRUM-X" &&
       E.engineOrder(info.engines)[1] === 12 && E.engineOrder(info.engines)[12] === 10 && E.engineOrder(info.engines)[13] === 14,
       "editor: engines listed FM6 second, DRUM then DRUM-X last (indices kept)");
    ok(E.engineOrder(["ANALOG", "X", "-", "DRUM", "FM6"]).join() === "0,4,3,1", "editor: an unknown engine follows the known ones");
    m.state.favorites[10][0] = m.state.favorites[12][0] = true;
    await rq(E.req.uiSet(3, 0));
    prefs = await E.readDevicePreferences(rq, info, names, prefs);
    const rows = E.devicePresetRows(info, names, prefs).filter((r) => !r.user), eng = [...new Set(rows.map((r) => r.engine))];
    ok(eng[0] === 0 && eng[1] === 12 && eng[eng.length - 2] === 10 && eng[eng.length - 1] === 14,
       "editor: device presets in the device's engine order");
  }
  const none = await E.readDevicePreferences(() => { throw new Error("unexpected request"); }, { uiCaps: 0 }, []);
  ok(none === null, "editor: old firmware receives no unsupported preference requests");
  {   /* (Skiff #27) COLORS (39): THEME CUSTOM on the mock as the firmware has it (tests/editor_test.c custom_colors) */
    const set = E.PAL_BUILTIN[5].c.map((x, i) => (i === 0 ? 0x0841 : x));
    let c = E.parse[E.CMD.COLORS](await rq(E.req.colors()));
    ok(E.CMD.COLORS === 39 && c.rc === 0 && !c.stored && !c.active && c.c.length === 11,
       "editor: COLORS (#27): none stored, eleven colours (22 bytes in pack7)");
    c = E.parse[E.CMD.COLORS](await rq(E.req.colors(0, 5, set)));
    ok(c.rc === 0 && !c.stored && m.state.custom.preview && m.state.custom.preview.look === 5 && m.state.palette === prefs.state.palette,
       "editor: COLORS op 0 shows a set in a look, stores nothing");
    c = E.parse[E.CMD.COLORS](await rq(E.req.colors(1, 4, set)));
    ok(c.rc === 0 && c.stored && c.active && c.look === 4 && c.c.join() === set.join() && m.state.palette === 4,
       "editor: COLORS op 1 stores the set and uses it, in its look (NEON)");
    let pr = await E.readDevicePreferences(rq, info, names, prefs, true);
    ok(pr.palettes.length === 7 && pr.palettes[6] === "CUSTOM" && pr.state.palette === 6 && pr.colors.stored && pr.colors.look === 4,
       "editor: THEME CUSTOM listed last, in use after a store");
    await rq(E.req.uiSet(0, 1));
    pr = await E.readDevicePreferences(rq, info, names, pr);
    ok(pr.state.palette === 1 && pr.palettes.length === 7 && pr.colors.stored && !pr.colors.active,
       "editor: another THEME keeps the set (CUSTOM still listed)");
    ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 6))).palette === 6 && m.state.palette === 4,
       "editor: UI_SET 6 is CUSTOM again, in its look");
    const long = E.req.colors(1, 0, set);
    long[1].push(0);                                        /* (a byte more) */
    ok(E.req.colors(1, 0, set)[1].length === 28 && E.parse[E.CMD.COLORS](await rq([E.CMD.COLORS, [1, 0, 3]])).rc === 1 &&
       E.parse[E.CMD.COLORS](await rq(E.req.colors(1, 6, set))).rc === 1 &&
       E.parse[E.CMD.COLORS](await rq(long)).rc === 1 &&
       E.parse[E.CMD.COLORS](await rq([E.CMD.COLORS, [2, 0]])).rc === 1 && m.state.custom.flags === 3,
       "editor: a short or long COLORS, a look past the themes, op 2 with a byte more: rc 1, the set kept");
    c = E.parse[E.CMD.COLORS](await rq(E.req.colors(2)));
    pr = await E.readDevicePreferences(rq, info, names, pr, true);
    ok(c.rc === 0 && !c.stored && pr.palettes.length === 6 && pr.state.palette === 4,
       "editor: COLORS op 2 removes the set: CUSTOM gone, THEME the look's own");
    ok(E.parse[E.CMD.UI_SET](await rq(E.req.uiSet(0, 6))).rc === 1, "editor: no set, no CUSTOM (UI_SET 6: rc 1)");
    await rq(E.req.uiSet(0, prefs.state.palette));
    const hp = join(FW_ROOT, "build/gen/ui_palettes.h");   /* each theme's colours to start from == the firmware's */
    if (existsSync(hp)) {
      const h = readFileSync(hp, "utf8");
      const five = [...h.matchAll(/\{"([^"]+)", (0x[0-9a-f]{4}), (0x[0-9a-f]{4}), (0x[0-9a-f]{4}), (0x[0-9a-f]{4}), (0x[0-9a-f]{4})\}/g)]
        .map((x) => [x[1], ...x.slice(2).map(Number)]);
      const trk = [...h.matchAll(/\{((?:0x[0-9a-f]{4}, ){5}0x[0-9a-f]{4})\},\s+\/\* (\S+) \*\//g)].map((x) => [x[2], ...x[1].split(", ").map(Number)]);
      ok(five.length === E.PAL_BUILTIN.length && E.PAL_BUILTIN.every((b, i) => b.name === five[i][0] && b.name === trk[i][0] &&
         b.c.join() === [...five[i].slice(1), ...trk[i].slice(1)].join()), "editor: the themes' colours to start from == the firmware's");
    } else console.log(`${"editor: the themes' colours (no build/gen/ui_palettes.h)".padEnd(64)} skip`);
    ok(E.PAL_BUILTIN.every((b) => !E.colorWarnings(b.c).length) &&
       E.colorWarnings([0x0841, 0x18c3, 0x0861, ...E.PAL_BUILTIN[0].c.slice(3)]).some((x) => x.a === "TEXT" && x.b === "BG"),
       "editor: no theme of the device's gets a warning; TEXT on BG unreadable does");
    const tk = E.customTokens(E.PAL_BUILTIN[2].c);
    ok(tk["--bg"] === E.hex565(0xf77c) && tk["--t6"] === E.hex565(0x0aa8) && tk["--head"] === E.hex565(0x0aa8) &&
       tk["color-scheme"] === "light" && E.customTokens(E.PAL_BUILTIN[5].c)["color-scheme"] === "dark" &&
       [0, 0x0841, 0xffff, 0x4e9f, 0xf77c].every((x) => E.from565hex(E.hex565(x)) === x) && E.luma565(0xf77c) > 128 &&
       E.mix565(0x0000, 0xffff, 50) === 0x8410,
       "editor: CUSTOM's tokens (BG, the tracks, THEME for headings, light or dark), RGB565 <-> #rrggbb, gfx.c's mix and luma");
  }
  if (!E.readDeviceMenu) console.log(`${"editor: MENU settings (this editor has none)".padEnd(64)} skip`);
  else {   /* the MENU settings (1.0.4): INFO 4E 01 count, MENU_DESC (72), MENU_SET (73) */
    ok(info.menuCount === 18 && info.menuCount === E.MENU.length,
       "editor: INFO advertises the MENU settings (4E 01 18: Skiff's; STYLE and FX LATCH gone, (#27) PLAYHEAD, (2.0) RETURN)");
    const items = await E.readDeviceMenu(rq, info);
    const byName = (n) => items.find((d) => d.name === n);
    ok(items.length === 18 && items.map((d) => d.id).join() === E.MENU.map((x) => x.id).join() &&
       items.map((d) => d.name).join() === E.MENU.map((x) => x.name).join() &&
       items.every((d) => d.kind === 0 && d.min === 0 && d.names.length === d.max - d.min + 1) &&
       items[0].name === "THEME" && eq(items[0].names, prefs.palettes) && items[0].value === m.state.palette &&
       byName("LEDS").names.join() === "OFF,DIM LO,DIM HI,INV" && byName("LEDS").value === 2 && byName("HOLD").value === 1 &&
       byName("SCREEN OFF").names.join() === "NEVER,5 MIN,15 MIN,30 MIN,60 MIN" && byName("SCREEN OFF").value === 3,
       "editor: MENU_DESC lists every setting, its value names and value");
    ok(items.every((d, i) => d.tab === E.MENU[i].tab && d.tabName === E.MENU_TABS[d.tab]),
       "editor: MENU_DESC gives each setting's tab on the device, after its names");
    ok(E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(18))).id === 127, "editor: MENU_DESC past the list answers id 127");
    let r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(4, 3)));
    ok(r.rc === 0 && r.id === 4 && r.value === 3 && E.parse[E.CMD.MENU_DESC](await rq(E.req.menuDesc(3))).value === 3,
       "editor: MENU_SET round trip (LEDS INV)");
    const pal = m.state.palette;
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(0, 50)));
    ok(r.rc === 0 && r.value === 5 && E.parse[E.CMD.UI_STATE](await rq(E.req.uiState())).palette === 5,
       "editor: MENU_SET clamps (THEME 50 -> 5, QUARK), the same setting as UI_SET 0");
    r = E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(17, 9)));
    ok(r.rc === 0 && r.id === 17 && r.value === 4, "editor: MENU_SET clamps (SCREEN OFF 9 -> 60 MIN)");
    await rq(E.req.menuSet(17, 3));
    ok(E.parse[E.CMD.MENU_SET](await rq(E.req.menuSet(7, 1))).rc === 1, "editor: MENU_SET of a retired id (FX LATCH): rc 1");
    await rq(E.req.menuSet(0, pal)); await rq(E.req.menuSet(4, 2));
    const d = E.parse[E.CMD.MENU_DESC]([3, 20, 1, 5, 64, 0, 64, 100, 64, 88, 0, 109, 115, 0]);
    ok(d.kind === 1 && d.value === 5 && d.max === 100 && d.name === "X" && d.unit === "ms" && d.names === null && d.tab === -1,
       "editor: MENU_DESC kind 1 (a number): its unit");
    const d2 = E.parse[E.CMD.MENU_DESC]([3, 20, 1, 5, 64, 0, 64, 100, 64, 88, 0, 109, 115, 0, 2, 65, 0]);
    ok(d2.unit === "ms" && d2.tab === 2 && d2.tabName === "A", "editor: MENU_DESC kind 1 with its tab after the unit");
    const d3 = E.parse[E.CMD.MENU_DESC]([4, 4, 0, 2, 64, 0, 64, 1, 64, 76, 0, 65, 0, 66, 0]);
    ok(d3.names.join() === "A,B" && d3.tab === -1 && d3.tabName === "", "editor: MENU_DESC from 1.0.4 firmware (no tab): tab -1");
    ok((await E.readDeviceMenu(() => { throw new Error("unexpected request"); }, { menuCount: 0 })).length === 0,
       "editor: firmware without MENU settings is not asked");
  }
  await rq(E.req.uiSet(3, 0));
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"];
  ok(scale.label === "SCALE" && scale.max === 15 && eq(scale.names, scaleNames), "editor: all 16 scale names exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 15)));
  ok(scaleSet.value === 15, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  {
    /* GLO > SYSTEM ROUT (G_ROUTE 14): MIDI IN CH1-6 (0, the default; CH1-4 before six tracks) / SEL (1); the id and G_COUNT unchanged */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 14)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 5)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 14, 0)));
    ok(rd.label === "ROUTE" && rd.def === 0 && eq(rd.names, ["CH1-6", "SEL"]) && r1.value === 1 && r0.value === 0 && info.gcount === 27,
      "editor: MIDI IN routing (ROUT CH1-6 / SEL, global id 14)");
  }
  {
    /* FX > REVERB TYPE (G_RTYPE 24, the old drum channel's id): ROOM (0, the default) / SPRING (1); G_COUNT unchanged */
    const rd = E.parse[E.CMD.DESC](await rq(E.req.desc(1, 24)));
    const r1 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 10)));
    const r0 = E.parse[E.CMD.SET](await rq(E.req.set(1, 24, 0)));
    const inert = [25, 26].map(async (id) => E.parse[E.CMD.DESC](await rq(E.req.desc(1, id))));
    const [d25, d26] = await Promise.all(inert);
    ok(rd.label === "TYPE" && rd.def === 0 && eq(rd.names, ["ROOM", "SPRING", "HALL"]) && r1.value === 2 && r0.value === 0 &&
       d25.label === "MODE" && eq(d25.names, ["CHORUS", "FLANGER", "PHASER"]) && d26.label === "FEEDBACK" && d26.max === 127 &&
       info.gcount === 27,
      "editor: REVERB TYPE (ROOM / SPRING / HALL, global id 24); (phase 9) CHORUS MODE, FEEDBACK (25, 26)");
  }
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  {   /* (2.0.1, #50) the ports connect() tries, as the installer's: a generic name too, "Felucca" first, never other gear */
    const port = (name, state = "connected") => ({ name, state });
    const access = (ins, outs) => ({ inputs: new Map(ins.map((p, k) => [k, p])), outputs: new Map(outs.map((p, k) => [k, p])) });
    const names = (a) => E.portPairs(a).map(([i, o]) => `${i.name}|${o.name}`).join();
    const mac = names(access([port("USB Composite Device"), port("Arturia KeyStep 37")],
      [port("Arturia KeyStep 37"), port("USB Composite Device")]));
    const two = names(access([port("USB-MIDI"), port("Felucca 0"), port("Felucca 1", "disconnected"), port("SMK-37 Pro")],
      [port("USB-MIDI"), port("Felucca 0"), port("Felucca 1")]));
    ok(mac === "USB Composite Device|USB Composite Device" && two === "Felucca 0|Felucca 0,USB-MIDI|USB-MIDI",
      `editor: (#50) the unit's ports by the installer's names, "Felucca" first, other gear and lone or gone ports left out (${mac}; ${two})`);
  }
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  {
    /* (#21) SMP_MIX: the slot's sounds' LEVEL, REV, DLY as smpUpload / smpMixSet send them: in the slot's order (buildSlot
       sorts by root, each sound's mix with it), the rest as uploaded; an empty slot refuses (rc 1) */
    const two = E.buildSlot("two", [{ s, root: 64, mix: [64, 0, 90] }, { s, root: 60, mix: [127, 30, 0] }]);
    ok(two.zones.map((z) => z.root).join() === "60,64" && two.zones[0].mix.join() === "127,30,0" && two.zones[1].mix.join() === "64,0,90" &&
       E.buildSlot("two", [{ s, root: 64 }, { s, root: 60 }]).hdr.join() === two.hdr.join(),
       "samples: buildSlot keeps each sound's LEVEL / REV / DLY through its sort (the header the same)");
    let r = E.parse[E.CMD.SMP_MIX](await rq(E.req.smpMix(1)));
    ok((info.skiff & 32) && r.slot === 1 && !r.rc && r.keys.length === 1 && r.mix.length === 1 && r.mix[0].join() === "127,0,0",
       "editor: SMP_MIX reads a slot's sounds (as uploaded: 127, 0, 0)");
    r = E.parse[E.CMD.SMP_MIX](await rq(E.req.smpMix(1, [[100, 20, 30]])));
    const back = E.parse[E.CMD.SMP_MIX](await rq(E.req.smpMix(1)));
    ok(!r.rc && r.mix[0].join() === "100,20,30" && back.mix[0].join() === "100,20,30" && r.keys[0].join() === "0,127",
       "editor: SMP_MIX sets them, the device keeps them");
    const a = E.req.smpMix(0, [[1, 2, 3], [4, 5, 6]])[1];
    ok(a.length === 49 && a[1] === 1 && a[2] === 4 && a[3] === 127 && a[17] === 2 && a[18] === 5 && !a[19] && a[33] === 3 && a[34] === 6 && !a[35],
       "editor: SMP_MIX's 48 values: LEVEL x 16, REV x 16, DLY x 16 (the sounds after the list as uploaded)");
    r = E.parse[E.CMD.SMP_MIX](await rq(E.req.smpMix(2, [[1, 2, 3]])));
    ok(r.rc === 1 && !r.mix.length, "editor: SMP_MIX on an empty slot: refused (rc 1)");
  }
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

async function editorSamplePresets() {
  const C = E.CMD;
  const { m, rq, done } = attachMock();
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const names = E.parse[C.NAMES](await rq(E.req.names(4)));
  await rq(E.req.preset(4, 0));
  const set = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  ok(eq(names.names.slice(0, 6), ["PIANO", "PIANO", "FLUTE", "SAX", "PIANO", "LOFI PIANO"]) && names.names.length === 10 &&
    eq(set.names.slice(0, 4), ["PIANO", "PIANO", "FLUTE", "SAX"]) && set.names[4] === "PIANO" &&
    eq(set.names.slice(5), ["USR1", "USR2", "USR3"]),
    "SAMPLE: TRANH and PERC removed, SET 1 and 4 kept as PIANO aliases, indices unchanged; (phase 9) the variations after");
  ok(E.aliasOf(names.names, 1) === 0 && E.aliasOf(names.names, 2) === 2 && E.aliasOf(set.names, 5) === 5 &&
     E.aliasOf(set.names, 4) === 0, "SAMPLE: an entry named like an earlier one is an alias of it");
  const alias = E.parse[C.PRESET](await rq(E.req.preset(4, 1)));
  const setAlias = E.parse[C.SET](await rq(E.req.set(0, info.pe0, 1)));
  const setPerc = E.parse[C.SET](await rq(E.req.set(0, info.pe0, 4)));
  ok(alias.preset === 0 && setAlias.value === 0 && setPerc.value === 0,
    "SAMPLE: preset 1 and SET 1 / 4 (once TRANH, PERC) land on PIANO");
  await rq(E.req.preset(10, 0));
  const kitD = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  const kitSet = [];
  for (const v of [1, 2, 3]) kitSet.push(E.parse[C.SET](await rq(E.req.set(0, info.pe0, v))).value);
  ok(eq(kitD.names, ["STD", "66", "10", "77", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC", "USR1", "USR2", "USR3"]) &&
     eq([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11].map((v) => E.aliasOf(kitD.names, v)), [0, 6, 5, 8, 4, 5, 6, 7, 8, 9, 10, 11]) &&
     eq(E.enumShown(kitD).filter((v) => E.aliasOf(kitD.names, v) === v).map((v) => kitD.names[v]),
        ["STD", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC", "USR1", "USR2", "USR3"]) &&
     eq(kitSet, [6, 5, 8]),
    "DRUM: KIT 1..3 (once HAND CYM H+CYM) named 66 10 77, aliases of 6 5 8: hidden, a SET lands there; ((2.0, #35) USR1..3 after)");
  const removed = E.parse[C.PRESET](await rq(E.req.preset(4, 4)));
  const kit = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(removed.engine === 10 && removed.preset === 0 && kit.engine === 10 && eq(kit.p.slice(info.pe0), E.DRUM_KIT_E),
    "SAMPLE: factory preset 4 (once PERC) loads DRUM's kit");
  await rq(E.req.preset(4, 0));
  m.state.p[info.pe0] = 4;                        /* an old PERC sound (SET 4, no loop: the editor cannot set it now) */
  m.state.p[info.pe0 + 3] = 0;
  await rq(E.req.stepSet(5, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 1, vel: 99 }));
  m.state.preset = 4;                             /* a project written before the factory preset was removed */
  const sound = [...m.state.p], steps = JSON.stringify(m.state.step);
  await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 });
  await rq(E.req.preset(4, 0));
  await rq(E.req.stepSet(5, emptyStep));
  await rq(E.req.project(0, 2), { timeout: 4000, retries: 0 });
  const loaded = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const tracks = E.parse[C.TRACK](await rq(E.req.track()));
  ok(loaded.engine === 10 && loaded.preset === 0 && tracks.tracks[0].engine === 10 && tracks.tracks[0].preset === 0
    && eq(loaded.p.slice(0, info.pe0), sound.slice(0, info.pe0)) && eq(loaded.p.slice(info.pe0), E.DRUM_KIT_E)
    && JSON.stringify(m.state.step) === steps,
    "SAMPLE: old PERC project loads as DRUM's kit, the rest of the sound and the steps kept");
  done();
}

/* the mock's tables == the firmware's (build/host/desc.json from tests/descdump.c, written by run_tests.sh):
   parameter descriptors, engines (titles, EDIT, presets), factory patterns, power-on sounds */
function mockTables() {
  {
    const m0 = E.makeMockDevice({ auto: false }), ph = m0.tables.ENG[9], dr = m0.tables.ENG[10];
    m0.stop();
    ok(ph.name === "PHYS" && ph.edit[0].names.join() === "MODAL,STRNG,MEMB,SYMP" && ph.edit[0].max === 3 &&
       ph.presets.length === 11 && !ph.presets.some((p) => p.name === "RAIN" || p.name === "DRUM KIT"),
       "editor: PHYS models MODAL STRNG MEMB SYMP (no DUST, no DRUM), 11 presets");
    ok(dr.name === "DRUM" && dr.edit.map((d) => d.label).join() === "KIT,PITCH,TONE,DECAY,SNAP,ACCENT,KICK,DRIVE" &&
       dr.presets.length === 13 && dr.presets[0].name === "DRUM KIT",
       "editor: DRUM engine 10 (KIT PITCH TONE DECAY SNAP ACCENT KICK DRIVE), DRUM KIT first of 13");
  }
  const dj = DESC;
  if (!existsSync(dj)) { console.log("editor: mock tables == firmware (no build/host/desc.json)        skip"); return; }
  const fw = JSON.parse(readFileSync(dj, "utf8"));
  ok(JSON.stringify(E.TC_NAMES) === JSON.stringify(fw.TC_NAME), "editor: the trig conditions (TC_NAMES) == the firmware's");
  const m = E.makeMockDevice({ auto: false }), T = m.tables;
  m.stop();
  const norm = (d) => ({ label: d.label, fmt: d.fmt, min: d.min, max: d.max, def: d.def, names: d.names || null, unit: d.unit || "" });
  const diffs = [];
  const cmp = (what, a, b) => { if (JSON.stringify(a) !== JSON.stringify(b)) diffs.push(`${what}: mock ${JSON.stringify(a)} != firmware ${JSON.stringify(b)}`); };
  for (const k of ["P_COUNT", "G_COUNT", "NSTEP", "P_E0", "G_ENGSEL", "P_SLCR", "NTRK"]) cmp(k, T[k], fw[k]);
  cmp("TP length", T.TP.length, fw.TP.length);
  fw.TP.forEach((d, i) => cmp(`TP[${i}]`, T.TP[i] && norm(T.TP[i]), d));
  cmp("GP length", T.GP.length, fw.GP.length);
  fw.GP.forEach((d, i) => cmp(`GP[${i}]`, T.GP[i] && norm(T.GP[i]), d));
  cmp("engines", T.ENG.map((e) => e.name), fw.ENG.map((e) => e.name));
  cmp("engine order (ENGINE_ORDER)", E.ENGINE_ORDER.filter((n) => fw.ENG.some((e) => e.name === n)), fw.ORDER);
  fw.ENG.forEach((fe, i) => {
    const me = T.ENG[i];
    if (!me) return;
    cmp(`${fe.name} titles`, me.titles, fe.titles);
    fe.edit.forEach((d, k) => cmp(`${fe.name} edit[${k}]`, norm(me.edit[k]), d));
    cmp(`${fe.name} presets`, me.presets.map((p) => p.name), fe.presets.map((p) => p.name));
    fe.presets.forEach((p, k) => {
      const mp = me.presets[k];
      if (mp) cmp(`${fe.name} ${p.name}`, { e: mp.e, env: mp.env, mono: mp.mono, more: mp.more }, { e: p.e, env: p.env, mono: p.mono, more: p.more });
    });
  });
  const m2 = E.makeMockDevice({ auto: false });   /* the mock's power-on sounds: its parts 1..4 */
  cmp("power-on sounds", m2.state.tracks.map((t) => [t.engine, t.preset]), fw.TRK_DEF.map((x) => x.slice(0, 2)));
  m2.stop();
  cmp("firmware: G_ENGSEL names == engine names", fw.GP[fw.G_ENGSEL].names, fw.ENG.map((e) => e.name));
  cmp("FM6 patches (init, factory, bank size)", T.FM6, fw.FM6);
  cmp("DRUM grid: the lanes' GM notes", [...E.LANE_NOTE], fw.LANE_NOTE);
  cmp("DRUM grid: the lane of GM 35..81", E.LANE_OF, fw.LANE_OF);
  cmp("DRUM-X factory kit", E.DX_KIT_DEF, fw.DX_KIT);
  cmp("DRUM-X preset kits (phase 9)", E.DX_KITS, fw.DX_KITS);
  cmp("DRUM-X preset -> kit", E.DRUMX_PKIT, fw.DRUMX_PKIT);
  const mj = join(DESC, "../menu.json");
  if (E.MENU && existsSync(mj)) {                   /* the MENU settings: tests/editor_test.c's MENU_DESC replies */
    const fm = JSON.parse(readFileSync(mj, "utf8"));
    cmp("MENU settings", E.MENU.map((x) => ({ id: x.id, kind: 0, min: 0, max: (x.names || m2.state.palettes).length - 1,
      value: x.def, name: x.name, names: x.names || m2.state.palettes, tab: x.tab, tabName: E.MENU_TABS[x.tab] })), fm);
  }
  diffs.slice(0, 20).forEach((d) => console.log("  " + d));
  ok(!diffs.length, `editor: mock tables == firmware (${diffs.length} differences)`);
  /* #31: percent values as the firmware formats them (param_format), SWG 0..100 shows its value */
  const pd = (fw.PCT || []).filter(([sc, i, v, , txt]) => E.fmtValue((sc ? T.GP : T.TP)[i], v)[0] !== txt);   /* [scope, index, value, max, text] */
  pd.slice(0, 5).forEach((x) => console.log("  PCT " + JSON.stringify(x)));
  ok(fw.PCT && fw.PCT.length && !pd.length, `editor: percent values == firmware (${fw.PCT ? fw.PCT.length : 0} values)`);
  /* #48: every list's order in the editor == the order the device's knobs step it (descdump SHOWN: param_turn);
     the note divisions longest first, with the triplets by their length */
  const shown = (d) => E.enumShown(d).filter((v) => E.aliasOf(d.names, v - d.min) === v - d.min).map((v) => d.names[v - d.min]);
  const sd = (fw.SHOWN || []).filter(([sc, i, names]) => JSON.stringify(shown((sc ? T.GP : T.TP)[i])) !== JSON.stringify(names));
  sd.slice(0, 5).forEach((x) => console.log("  SHOWN " + JSON.stringify(x) + " editor " + JSON.stringify(shown((x[0] ? T.GP : T.TP)[x[1]]))));
  ok(fw.SHOWN && fw.SHOWN.length > 20 && !sd.length, `editor: enum lists in the device's knob order (${fw.SHOWN ? fw.SHOWN.length : 0} lists)`);
  const div = T.TP.find((d) => d.label === "DIV"), slr = T.TP.filter((d) => d.label === "RATE" && d.fmt === E.F.ENUM);
  ok(shown(div).join() === "4BAR,2BAR,1/1,1/2,1/4,1/8,8T,1/16,16T,1/32" &&
     slr.some((d) => shown(d).join() === "1/8,8T,1/16,16T,1/32,32T") && div.names[2] === "1/16",
     "editor: #48 DIV / RATE / TIME longest first (values unchanged: 2 is still 1/16)");
  const swg = [T.TP, T.GP].flatMap((tb) => tb.filter((d) => d.label === "SWING" || d.label === "SWING+"));   /* (Skiff U4: GLOBAL's SWING+) */
  ok(swg.length === 3 && swg.every((d) => E.fmtValue(d, 100)[0] === "100" && E.fmtValue(d, 50)[0] === "50") &&
     E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 127)[0] === "100" && E.fmtValue({ fmt: swg[0].fmt, min: 0, max: 127 }, 64)[0] === "50",
     "editor: SWG 100 shows 100 % (not 79), 0..127 percents unchanged");
}

/* ------------------------------------------------------------------ DIGITAL retired --- */
/* DIGITAL (engine 1, four-operator FM) was replaced by FM6 (src/fm4_convert.c): the editor's conversion == the
   firmware's (build/host/desc.json "FM4": its presets and 48 sounds), engine 1 is "-" with no presets, G_ENGSEL 1 and
   PRESET 1 k load the converted sound (FM6, its own patch), library files of DIGITAL sounds (91, 89 and 69 parameters,
   single-patch files too) import as FM6 with the converted patch, a converted sound put to the device keeps its
   DIGITAL values in an engine 1 slot (UP_LOAD converts it), the device's DIGITAL slot reads back as FM6 */
async function editorFm4() {
  const dj = DESC;
  if (existsSync(dj)) {
    const all = JSON.parse(readFileSync(dj, "utf8")), fw = all.FM4, fpe0 = all.P_E0;
    let bad = 0;
    for (const c of fw.cases) {
      const r = E.FM4.convert(c.p, fpe0);
      if (r.preset !== c.preset || !eq(r.p, c.out) || !eq(Array.from(r.voice), c.voice)) {
        if (bad++ < 3) console.log("  FM4: E", js(c.p.slice(fpe0)), "voice", js(Array.from(r.voice).map((x, i) => (x !== c.voice[i] ? `${i}:${x}/${c.voice[i]}` : "")).filter(Boolean)));
      }
    }
    ok(fw.cases.length === 48 && !bad, `DIGITAL -> FM6: the editor's conversion == the firmware's (${fw.cases.length} sounds, ${bad} differ)`);
    ok(eq(E.FM4.TO_FM6, fw.to_fm6) && fw.presets.length === E.FM4.PRESETS.length && fw.presets.every((p, k) => {
      const q = E.FM4.PRESETS[k];
      return q.name === p.name && eq(q.e, p.e) && eq(q.env, p.env) && q.fenv === p.fenv && q.mono === p.mono &&
        eq(q.fx, p.fx.map((x) => x - 1));
    }), "DIGITAL -> FM6: its presets and the FM6 presets covering them == the firmware's");
  } else {
    console.log("DIGITAL -> FM6: == the firmware's (no build/host/desc.json)        skip");
  }
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const n1 = E.parse[C.NAMES](await rq(E.req.names(1)));
  ok(info.engines[1] === "-" && !n1.names.length && n1.titles.join() === "-,-", "DIGITAL retired: engine 1 reserved (\"-\", no presets)");
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  const fm6Of = async () => E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0))).packed;
  /* the DIGITAL values of preset k on top of the track's: what the device converts */
  const digital = (k, base) => E.FM4.presetValues(base.map((v, i) => (i < info.pe0 && ![0, 39, 40].includes(i) && !(i >= 17 && i <= 32) &&
    !(i >= 45 && i <= 48) && i !== 81 && i !== 82 ? pdesc[i].def : v)), k, info.pe0);
  let d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(1, 20, 1));
  let d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const owned = (p) => p.map((v, i) => (i === info.pe0 + 7 ? 8 : v));   /* (1.0.3: the converted patch is the track's own: SLOT OWN) */
  let want = E.FM4.convert(digital(0, d0.p), info.pe0);
  ok(d.engine === 12 && d.preset === 0 && eq(d.p, owned(want.p)) && eq(await fm6Of(), E.FM6.pack(want.voice)),
    "DIGITAL retired: SET G_ENGSEL 1 -> FM6 with E.PIANO converted (its own patch, SLOT OWN, preset TINE EP)");
  d0 = d;
  await rq(E.req.preset(1, 5));
  d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  want = E.FM4.convert(digital(5, d0.p), info.pe0);
  ok(d.engine === 12 && d.preset === 4 && eq(d.p, owned(want.p)) && E.FM6.name(E.FM6.unpack(await fm6Of())) === "PAD",
    "DIGITAL retired: PRESET 1 5 (its PAD) -> FM6, the converted patch named PAD, SLOT OWN, preset FM6 PAD");
  /* library files of DIGITAL sounds: today's parameters (INFO's: (Skiff) DIR at 91, the ducking 92..94, phase 9's 95..116), 91 (P_E0 83: before the DRUM lane levels),
     89 (P_E0 81), 69 (P_E0 61, no OP ENV) */
  const PC = info.pcount, PE = info.pe0;
  const base = Array.from({ length: PC }, (_, i) => (i < PE ? pdesc[i].def : 0));
  const pad = E.FM4.presetValues(base.slice(), 5, PE);
  pad[61] = 20; pad[64] = 100;                      /* op 1 ATK, LVL: an OP ENV edit */
  const keys91 = [...keys.slice(0, 83), ...keys.slice(PE)];
  const keys89 = [...keys.slice(0, 81), ...keys.slice(PE)], keys69 = [...keys.slice(0, 61), ...keys.slice(PE)];
  const p91 = [...pad.slice(0, 83), ...pad.slice(PE)];
  const p89 = [...pad.slice(0, 81), ...pad.slice(PE)], p69 = [...pad.slice(0, 61), ...pad.slice(PE)];
  const noLanes = (q) => q.map((v, i) => (i >= 83 && i < PE ? null : v));
  const engines = info.engines.map((n, i) => (i === 1 ? "DIGITAL" : n));   /* (the files' firmware had DIGITAL) */
  const file = (pc, labels, p) => ({ format: "felucca-library", version: 1, kind: "library", pCount: pc, pE0: pc - 8, paramLabels: labels,
    engines, patches: [{ name: "OLD PAD", engine: 1, engineName: "DIGITAL", params: p, pattern: null, tags: [] }] });
  const ctx = { keys, engines: info.engines, pe0: info.pe0 };
  const r91 = E.readLibraryFile(file(PC, keys, pad), ctx).patches[0];
  const rOld = E.readLibraryFile(file(91, keys91, p91), ctx).patches[0];
  const r89 = E.readLibraryFile(file(89, keys89, p89), ctx).patches[0];
  const r69 = E.readLibraryFile(file(69, keys69, p69), ctx).patches[0];
  const one = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 1, engineName: "DIGITAL", p: p89 }, ctx).patches[0];
  const conv = (p) => E.FM4.convert(p, PE);
  const flat = pad.slice(); for (let i = 61; i <= 80; i++) flat[i] = i % 5 === 3 || i % 5 === 0 ? 127 : 0;   /* (61: ATK) */
  const isFm6 = (r, p) => r && r.engine === 12 && r.engineName === "FM6" && eq(r.fm6, E.FM6.pack(conv(p).voice)) &&
    eq(r.p.map((v) => v ?? 0), conv(p).p.map((v) => v ?? 0));
  ok(isFm6(r91, pad), `library file: a DIGITAL patch (${PC} parameters) imports as FM6 with the converted patch`);
  ok(isFm6(rOld, noLanes(pad)), "library file: .. of 91 parameters (1.0.x, no lane levels then)");
  ok(isFm6(r89, noLanes(pad).map((v, i) => (i === 81 || i === 82 ? null : v))) && isFm6(one, noLanes(pad).map((v, i) => (i === 81 || i === 82 ? null : v))),
    "library file: .. of 89 parameters (labelled, and a single-patch file): the OP ENV edit carried");
  ok(isFm6(r69, flat.map((v, i) => (i >= 61 && i < PE ? null : v))), "library file: .. of 69 parameters (no OP ENV then: the defaults)");
  const back = E.readLibraryFile(JSON.parse(js(E.libraryFile("library", [r91], ctx))), ctx).patches[0];
  ok(eq(back.fm6, r91.fm6) && eq(back.fm4, r91.fm4) && back.engineName === "FM6", "library file: the converted patch and its DIGITAL values round trip");
  /* a library stored on DIGITAL firmware, then a device without it: libAdopt converts its DIGITAL sounds (the layout
     changed or not), the others keep their engine */
  {
    const adopt = html.slice(html.indexOf("async function libAdopt(d)"), html.indexOf("const engineOk"));
    const L = vm.runInNewContext(proto + `
      let lib = [], libMeta = {}; const written = [];
      const store = { setMeta: async () => {} }, renderLib = () => {};
      async function libWrite(a) { written.push(...a); }
      ${adopt}
      ;({ libAdopt, set: (l, m) => { lib = l; libMeta = m; written.length = 0; }, lib: () => lib, written: () => written })`,
    { setTimeout, clearTimeout, setInterval, clearInterval, console });
    const dv = { keys, info: { engines: info.engines, pe0: info.pe0 } };
    const entry = () => [{ id: "a", name: "OLD PAD", engine: 1, engineName: "DIGITAL", p: pad.slice(), pattern: null, tags: ["x"],
      created: "2026-01-01T00:00:00.000Z", modified: "2026-01-01T00:00:00.000Z" },
      { id: "b", name: "BASS", engine: 0, engineName: info.engines[0], p: base.slice(), pattern: null, tags: [] }];
    L.set(entry(), { keys, engines });
    await L.libAdopt(dv);
    const [a1, b1] = L.lib();
    ok(isFm6(a1, pad) && a1.id === "a" && eq(a1.fm4, pad) && a1.tags.join() === "x" && b1.engine === 0 &&
      L.written().includes(a1), "library: libAdopt on a device without DIGITAL converts its DIGITAL sounds to FM6");
    L.set(entry(), { keys, engines: info.engines });       /* (adopted before: the layout is the device's already) */
    await L.libAdopt(dv);
    ok(isFm6(L.lib()[0], pad) && L.written().length === 1, "library: .. also when the layout is unchanged (only those written)");
    /* SAMPLE PERC (SET 4, retired after 1.0.2): libAdopt and a library file give DRUM's kit, the rest of the sound kept;
       another SAMPLE set stays; adopted again: nothing to write */
    const perc = base.slice(); perc[PE] = 4; perc[PE + 3] = 0; perc[0] = 77; perc[36] = 41;
    const flute = base.slice(); flute[PE] = 2;
    const isKit = (r) => r && r.engine === 10 && r.engineName === "DRUM" && eq(r.p.slice(0, PE), perc.slice(0, PE)) &&
      eq(r.p.slice(PE), E.DRUM_KIT_E);
    const sm = () => [{ id: "c", name: "OLD PERC", engine: 4, engineName: "SAMPLE", p: perc.slice(), pattern: null, tags: ["d"],
      created: "2026-01-01T00:00:00.000Z", modified: "2026-01-01T00:00:00.000Z" },
      { id: "e", name: "FLUTE", engine: 4, engineName: "SAMPLE", p: flute.slice(), pattern: null, tags: [] }];
    L.set(sm(), { keys, engines: info.engines });
    await L.libAdopt(dv);
    const [c1, e1] = L.lib();
    ok(isKit(c1) && c1.id === "c" && c1.tags.join() === "d" && e1.engine === 4 && eq(e1.p, flute) &&
      L.written().length === 1 && L.written()[0] === c1, "library: libAdopt turns its SAMPLE PERC sounds into DRUM's kit");
    await L.libAdopt(dv);
    ok(isKit(L.lib()[0]) && L.written().length === 1, "library: .. adopted again: as it is (nothing more written)");
    const fileP = { format: "felucca-library", version: 1, kind: "library", pCount: PC, pE0: PE, paramLabels: keys,
      engines: info.engines, patches: [{ name: "OLD PERC", engine: 4, engineName: "SAMPLE", params: perc, pattern: null, tags: [] },
        { name: "FLUTE", engine: 4, engineName: "SAMPLE", params: flute, pattern: null, tags: [] }] };
    const rp = E.readLibraryFile(fileP, ctx).patches;
    const one = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 4, engineName: "SAMPLE", p: perc }, ctx).patches[0];
    ok(isKit(rp[0]) && rp[1].engine === 4 && eq(rp[1].p, flute) && isKit(one),
      "library file: a SAMPLE PERC patch imports as DRUM's kit (a single-patch file too)");
  }
  /* put to the device: its DIGITAL values as engine 1 (the device converts them on load); read back as FM6 */
  let rc = await E.bank.put(rq, 9, { ...r91, engine: 1, p: r91.fm4 });
  const u = await E.bank.get(rq, info, 9);
  await rq(E.req.upLoad(9));
  d = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && u.engine === 1 && eq(u.p, pad) && d.engine === 12 && eq(await fm6Of(), r91.fm6),
    "device slot of a DIGITAL sound (engine 1, its values kept): UP_LOAD plays the converted FM6 patch");
  done();
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PITCH" && keys[13] === "PITCH#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 0 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 0 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, an empty sequencer, then UP_LOAD brings the sound back, not the stored pattern */
  await rq(E.req.preset(2, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  await rq(E.req.set(0, 17, 3));                 /* the track's own: ARP MODE */
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 0 && eq(d2.p.filter((_, i) => i !== 17), cap.p.filter((_, i) => i !== 17)) && d2.p[17] === 3
    && st.every((x) => !x.n) && cap.pattern, "librarian: UP_LOAD applies the sound only (steps and the track's ARP stay)");
  await rq(E.req.set(0, 17, 0));
  for (let i = 0; i < 16; i++) await rq(E.req.stepSet(i, E.stepsFromPattern(cap.pattern)[i]));   /* (for the captures below) */

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a PHASE patch that holds a pattern; the sound only, the sequence (with notes) stays */
  const bass = await E.bank.get(rq, info, 2);   /* PHASE RESO, with the ACID pattern */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await rq(E.req.set(0, 29, 24));               /* the track's own: LEN 24, ARP MODE 2 */
  await rq(E.req.set(0, 17, 2));
  await rq(E.req.set(0, 81, 1));                /* CHRD DIA3: the track's too */
  const seqBefore = [];
  for (let i = 0; i < 24; i++) seqBefore.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const flashBefore = js(m.state.bank);
  await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 24; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const own = new Set([0, 39, 40, ...Array.from({ length: 16 }, (_, i) => 17 + i), 45, 46, 47, 48, 81, 82]);
  ok(d3.engine === bass.engine && eq(d3.p.filter((_, i) => !own.has(i)), bass.p.filter((_, i) => !own.has(i)))
    && d3.p[29] === 24 && d3.p[81] === 1 && d3.p[17] === 2 && js(st3) === js(seqBefore) && st3[20].notes[0] === 50 && js(m.state.bank) === flashBefore,
    "librarian: audition = G_ENGSEL + SETs of the sound; steps, LEN and ARP stay; no flash write");
  /* the next audition: the same rule */
  const bell = await E.bank.get(rq, info, 1);
  await E.auditionPatch(rq, info, bell, { gEng: 20 });
  const s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(d4.engine === bell.engine && bell.engine === 12 && s20.notes[0] === 50 && d4.p[29] === 24 && eq(d4.p.filter((_, i) => !own.has(i)), bell.p.filter((_, i) => !own.has(i))),
    "librarian: a second audition keeps the sequence too");
  await rq(E.req.set(0, 29, 16));
  await rq(E.req.set(0, 17, 0));
  await rq(E.req.set(0, 81, 0));

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 }, PC = info.pcount, PE = info.pe0;
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === PC && file.paramLabels.length === PC && file.paramLabels[81] === "CHORD" && file.paramLabels[82] === "VOICING" &&
    file.paramLabels[83] === "KICK" && file.paramLabels[90] === "BELL" && file.paramLabels[91] === "DIR" && file.paramLabels[92] === "BY" && file.engines.length === 15,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "PHASE",
    "library file: write -> read round trip");
  {   /* (Skiff U4, the words) a file of before: its keys LVL ATK .. (the labels then) still find their values, by id */
    const OLD = ["LVL", "ATK", "DEC", "SUS", "REL", "FLT", "PIT", "SHP", "FX", "RATE", "WAVE", "PHS", "FADE", "PIT", "FLT",
      "SHP", "AMP", "MODE", "RATE", "OCT", "GATE", "SWG", "PROB", "HOLD", "ORD", "ROOT", "SCL", "QNT", "TRN", "LEN", "DIV", "SWG",
      "GATE", "DST", "CHO", "DLY", "REV", "VCE", "GLD", "PAN", "MUTE", "GLMOD", "PRIO", "ALLOC", "DTUNE", "SLCR", "PAT", "RATE",
      "DEPTH", "SRC1", "DST1", "AMT1", "SRC2", "DST2", "AMT2", "SRC3", "DST3", "AMT3", "SRC4", "DST4", "AMT4",
      "ATK", "DEC", "SUS", "REL", "LVL", "ATK", "DEC", "SUS", "REL", "LVL", "ATK", "DEC", "SUS", "REL", "LVL", "ATK", "DEC", "SUS",
      "REL", "LVL", "CHRD", "VOIC", "KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL", "DIR", "BY", "DEPTH", "REL"];
    const oldKeys = E.paramKeys(OLD.map((label) => ({ label })), OLD.length, OLD.length).concat(keys.slice(OLD.length));   /* ((phase 9) 95..: today's) */
    const lp = E.readLibraryFile({ ...file, paramLabels: oldKeys }, ctx).patches[0].p;
    ok(oldKeys[0] === "LVL" && keys[0] === "LEVEL" && eq(lp, cap.p), "library file: a file keyed by the labels before U4 (LVL ATK ..) maps by id");
  }
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["PHASE", "ANALOG", "SAMPLE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === PC + 1 && p0[5] === null && p0[6] === cap.p[5] && p0[PC] === cap.p[PC - 1]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "ANALOG", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  {   /* files from the 69-parameter firmware (P_E0 61): the engine's 8 land on E0..E7, FM op ENV, the
         chord keys, the lane levels, DIR and the ducking stay unset */
    const p69 = [...cap.p.slice(0, 61), ...cap.p.slice(PE, PE + 8)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p69 }, ctx).patches[0].p;
    const keys69 = [...keys.slice(0, 61), ...keys.slice(PE)];
    const lp = E.readLibraryFile({ ...file, pCount: 69, paramLabels: keys69, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p69 }] }, ctx).patches[0].p;
    const good = (q) => q.length === PC && eq(q.slice(0, 61), cap.p.slice(0, 61)) && eq(q.slice(PE), cap.p.slice(PE)) && q.slice(61, PE).every((v) => v === null);
    ok(good(sp) && good(lp) && good(nk), "library file: 69-parameter files (patch, labelled, unlabelled) map the engine's 8 to E0..E7");
  }
  {   /* files from the 89-parameter firmware (P_E0 81, before the chord keys): the engine's 8 land on E0..E7, the FM
         op ENV in place, CHRD VOIC unset (left as the track has them), the lane levels unset */
    const p89 = [...cap.p.slice(0, 81), ...cap.p.slice(PE, PE + 8)];
    const keys89 = [...keys.slice(0, 81), ...keys.slice(PE)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p89 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 89, pE0: 81, paramLabels: keys89, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p89 }] }, ctx).patches[0].p;
    const good = (q) => q.length === PC && eq(q.slice(0, 81), cap.p.slice(0, 81)) && q.slice(81, PE).every((v) => v === null) && eq(q.slice(PE), cap.p.slice(PE));
    ok(keys89[81] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 89-parameter files (patch, labelled, unlabelled) map the engine's 8 to E0..E7, the chord keys unset");
  }
  {   /* files from 1.0.x (91 parameters, P_E0 83, before the DRUM lane levels): the engine's 8 land on E0..E7, the
         chord keys in place, the lane levels unset (a sound load sets them to 100 %) */
    const p91 = [...cap.p.slice(0, 83), ...cap.p.slice(PE, PE + 8)];
    const keys91 = [...keys.slice(0, 83), ...keys.slice(PE)];
    const sp = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: cap.engine, engineName: info.engines[cap.engine], p: p91 }, ctx).patches[0].p;
    const lp = E.readLibraryFile({ ...file, pCount: 91, pE0: 83, paramLabels: keys91, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const nk = E.readLibraryFile({ ...file, paramLabels: undefined, patches: [{ ...file.patches[0], params: p91 }] }, ctx).patches[0].p;
    const good = (q) => q.length === PC && eq(q.slice(0, 83), cap.p.slice(0, 83)) && q.slice(83, PE).every((v) => v === null) && eq(q.slice(PE), cap.p.slice(PE));
    ok(keys91[83] === "E0" && good(sp) && good(lp) && good(nk),
      "library file: 91-parameter files (1.0.x: patch, labelled, unlabelled) map the engine's 8 to E0..E7, the lane levels unset");
  }
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 0, preset: 4, engineName: "ANALOG", presetName: "ACID", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

/* v5: the DRUM grid. Steps carry lane hits and accents (3 bytes after vel); a user preset of a DRUM track holds a
   16-step grid (UP_GET / UP_PUT kind 1); the steps page writes them as lane names */
async function editorGrid() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const h = E.parseHits("kick >snare HATCL cym");
  ok(h && h.hit === (1 | 2 | 8 | 128) && h.acc === 2 && E.hitsText(h.hit, h.acc) === "KICK >SNARE HATCL BELL" &&
     E.parseHits("KICK FOO") === null && E.parseHits(" ").hit === 0, "grid: hits as text (lanes, > accent, CYM / CONGA aliases)");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [41, 0, 0, 0], time: 0, flags: 0, vel: 100, hit: 0x81, acc: 0x80 })));
  ok(s.hit === 0x81 && s.acc === 0x80 && s.notes[0] === 41, "grid: STEP_SET / STEP_GET carry the hits (lane 8 too)");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [42, 0, 0, 0], time: 0, flags: 0, vel: 100 })));
  ok(s.hit === 0x81 && s.notes[0] === 42, "grid: a STEP_SET without hits (an older editor) keeps them");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 4, { n: 0, notes: [0, 0, 0, 0], time: 0, flags: 0, vel: 0, hit: 0x12, acc: 0x02 })));
  ok(w.hit === 0x12 && w.acc === 0x02 && E.parse[C.TRACK_STEP]([2, 4, 0, 0, 0, 0, 0, 0, 0, 0]).hit === 0,
    "grid: TRACK_STEP carries the hits (an older firmware's reply: none)");
  await rq(E.req.preset(10, 0));                   /* the selected track a DRUM kit, its 16 steps a grid */
  for (let i = 0; i < 16; i++) {
    await rq(E.req.stepSet(i, { n: i === 2 ? 1 : 0, notes: [i === 2 ? 49 : 0, 0, 0, 0], time: i % 4 === 0 || i === 2 ? 0 : 2, flags: 0,
      vel: 0, hit: i % 4 === 0 ? 1 | (i === 0 ? 128 : 0) : 0, acc: i === 0 ? 128 : 0 }));
  }
  const cap = await E.capturePatch(rq, info, "MY BEAT");
  ok(cap.patch.grid && !cap.patch.pattern && cap.patch.grid[0][0] === 0x81 && cap.patch.grid[0][1] === 0x80 &&
     cap.patch.grid[2][0] === 0x80 && cap.patch.grid[1][0] === 0 && cap.patch.grid[4][0] === 1,
     "grid: capture of a DRUM track = its grid (a crash 49 on BELL)");
  const rc = await E.bank.put(rq, 14, cap.patch);
  const g = await E.bank.get(rq, info, 14);
  ok(rc === 0 && g.grid && !g.pattern && JSON.stringify(g.grid) === JSON.stringify(cap.patch.grid), "grid: UP_PUT kind 1 -> UP_GET round trip");
  const put = E.req.upPut(14, cap.patch), saved = JSON.stringify(m.state.bank[14]);
  const base = put[1].length - 17;
  for (const bytes of [put[1].slice(0, -1), [...put[1], 0], [...put[1].slice(0, base), 2, ...Array(16).fill(0)]]) {
    const rejected = E.parse[C.UP_PUT](await rq([C.UP_PUT, bytes]));
    ok(rejected.rc === 1 && JSON.stringify(m.state.bank[14]) === saved,
       "grid: malformed preset extension rejected without changing saved data");
  }
  const ctx = { keys: null, engines: info.engines };
  const back = E.readLibraryFile(JSON.parse(JSON.stringify(E.libraryFile("bank", [{ ...g, slot: 14 }], ctx))), ctx);
  ok(JSON.stringify(back.patches[0].grid) === JSON.stringify(g.grid) && !back.patches[0].pattern, "grid: a library file keeps the grid");
  await rq(E.req.upStore(15, "STORED"));
  const g2 = await E.bank.get(rq, info, 15);
  ok(g2.grid && g2.grid[0][0] === 0x81 && g2.grid[0][1] === 0x80, "grid: UP_STORE of a DRUM track stores its grid");
  const p3 = await E.bank.get(rq, info, 2);
  ok(p3.pattern && p3.grid === null, "grid: a note pattern stays a pattern (kind 0)");
  done();
}

/* (Skiff W1) parameter locks (1.1, INFO 4C 01 01): the editor reads the kinds (op 7) and edits a lock with op 5 (op 3,
   an automation event's, made it one); a step's locks cleared (op 6); firmware of before: no kinds, op 7 refused */
/* (Skiff W4) a light track (T5, T6: INFO 54 01) and the engines it may not play: refused before anything is written */
async function editorLight() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const phys = info.engines.indexOf("PHYS"), before = E.parse[C.DUMP](await rq(E.req.dump()), info);
  let threw = "";
  try { await E.auditionPatch(rq, info, { engine: phys, p: [1, 2, 3] }, { gEng: 20, track: 4 }); } catch (e) { threw = e.message; }
  const after = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(E.lightRefuses(info, 4, phys) && E.lightRefuses(info, 5, phys) && !E.lightRefuses(info, 0, phys) &&
     !E.lightRefuses(info, 4, info.engines.indexOf("ANALOG")) && /T5/.test(threw) && JSON.stringify(after) === JSON.stringify(before),
     "light: T5 may not play PHYS: Audition refuses before writing (the sound as it was)");
  done();
}

async function editorLocks() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.locks === true && info.stepx === E.TC_NAMES.length, "locks: INFO 4C 01 01 (then 54, 43)");
  await rq(E.req.motion(0, 2, {}, info.ntrk));
  await rq(E.req.motion(0, 3, { step: 2, param: 17, value: 40 }, info.ntrk));
  await rq(E.req.motion(0, 5, { step: 4, param: 17, value: 90 }, info.ntrk));
  let m = E.parse[C.MOTION](await rq(E.req.motion(0, 7, {}, info.ntrk)));
  ok(m.kinds && m.count === 2 && !m.events[0].lock && m.events[1].lock && m.events[1].value === 90,
     "locks: op 7 reads the kinds after the records (an event, a lock)");
  m = E.parse[C.MOTION](await rq(E.req.motion(0, 5, { step: 4, param: 17, value: 70 }, info.ntrk)));
  ok(m.kinds && m.events[1].lock && m.events[1].value === 70, "locks: a lock's value edited with op 5 stays a lock");
  m = E.parse[C.MOTION](await rq(E.req.motion(0, 3, { step: 4, param: 17, value: 60 }, info.ntrk)));
  ok(!m.kinds && m.count === 2, "locks: op 3 replies as before 1.1 (no kinds)");
  m = E.parse[C.MOTION](await rq(E.req.motion(0, 7, {}, info.ntrk)));
  ok(!m.events[1].lock, "locks: op 3 on a lock made it automation (why the editor uses op 5)");
  await rq(E.req.motion(0, 5, { step: 4, param: 17, value: 60 }, info.ntrk));
  m = E.parse[C.MOTION](await rq(E.req.motion(0, 6, { step: 4 }, info.ntrk)));
  ok(m.kinds && m.count === 1 && !m.events[0].lock, "locks: op 6 clears a step's locks, its automation stays");
  done();
  const o = attachMock({ noLocks: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const om = E.parse[C.MOTION](await o.rq(E.req.motion(0, 7, {}, oi.ntrk)));
  ok(!oi.locks && om.rc === 1 && !om.kinds, "locks: firmware of before (no 4C): no kinds, op 7 refused");
  o.done();
}

/* (Skiff W1) a step's FLAM, COND, NUDGE after its ratchet (INFO 43 01 count): read, written, cleared; a write without
   them (an editor of before, or one that only changes a note) keeps them, FLAM too */
async function editorStepExtras() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.stepx === E.TC_NAMES.length && info.light.tracks === 0x30, "step extras: INFO 43 01 12 after the light tracks");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(5, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 100, ratchet: 1,
    flam: true, cond: 3, nudge: -9 })));
  ok(s.flam === 1 && s.cond === 3 && s.nudge === -9 && s.ratchet === 1, "step extras: STEP_SET writes FLAM, COND 1:2, NUDGE -9");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(5, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 100, ratchet: 1 })));
  ok(s.flam === 1 && s.cond === 3 && s.nudge === -9 && s.notes[0] === 62, "step extras: a write without them (x1) keeps FLAM, COND, NUDGE");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(5, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 100, ratchet: 2,
    flam: true, cond: 3, nudge: -9 })));
  ok(!s.flam && s.ratchet === 2, "step extras: FLAM only at x1 (a ratchet ends it)");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0, hit: 0, acc: 0, chance: 100, ratchet: 1,
    flam: 0, cond: 0, nudge: 0 })));
  ok(!s.flam && !s.cond && !s.nudge, "step extras: a Clear leaves no COND or NUDGE behind");
  ok(E.req.stepSet(5, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, vel: 100, cond: 2 })[1].length === 9,
     "step extras: never sent without the chance and the ratchet before them");
  ok(E.parse[C.STEP_GET]([1, 1, 60, 0, 0, 0, 0, 0, 96, 0, 0, 0, 100, 1]).cond === 0, "step extras: older firmware's steps read none");
  done();
  const o = attachMock({ noStepx: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(!oi.stepx && oi.ratchet === 4, "step extras: firmware without them (no 43 01): the editor shows none");
  o.done();
}

/* (2.0, #25, INFO 57 01 bit 6) a step's ties after its NUDGE (bit k: note k goes on from the step before): written to
   the step's notes, none with hits, none after a write without them (an editor of before); older firmware's steps read
   none; "(C4)" in the step table; a library pattern's TIE */
async function editorStepTies() {
  const C = E.CMD, x = { time: 0, flags: 0, vel: 100, chance: 100, ratchet: 1, flam: 0, cond: 0, nudge: 0 };
  const { rq, done } = attachMock({});
  ok(E.parse[C.INFO](await rq(E.req.info())).skiff === 127, "ties: INFO 57 01 127 (bit 6)");
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { ...x, n: 2, notes: [60, 64, 0, 0], ties: 15 })));
  ok(s.ties === 3 && E.parse[C.STEP_GET](await rq(E.req.stepGet(6))).ties === 3, "ties: STEP_SET writes them, to the step's 2 notes");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { ...x, n: 2, notes: [60, 64, 0, 0] })));
  ok(s.ties === 0, "ties: a write without them (an editor of before) leaves none");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { ...x, n: 1, notes: [60, 0, 0, 0], hit: 1, acc: 1, ties: 1 })));
  ok(s.ties === 0 && s.hit === 1 && s.acc === 1, "ties: none on a step with hits (its accents kept)");
  s = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 7, { ...x, n: 1, notes: [62, 0, 0, 0], ties: 1 })));
  ok(s.ties === 1 && E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 7))).ties === 1, "ties: TRACK_STEP on another track");
  ok(E.parse[C.STEP_GET]([1, 1, 60, 0, 0, 0, 0, 0, 96, 0, 0, 0, 100, 1, 0, 0, 32]).ties === 0, "ties: older firmware's steps read none");
  ok(E.req.stepSet(5, { ...x, n: 1, notes: [60, 0, 0, 0], ties: 1 })[1].length === 18 &&
     E.req.stepSet(5, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 100, ratchet: 1, ties: 1 })[1].length === 14,
     "ties: sent only after FLAM, COND and NUDGE");
  const p = E.parseNotes("(C4) E4");
  ok(JSON.stringify(p) === "[60,64]" && p.ties === 1 && E.parseNotes("C4").ties === 0 && E.parseNotes("(C4 E4") === null,
     "ties: \"(C4) E4\" in the step table: C4 going on");
  ok(JSON.stringify(E.patternFromSteps([{ n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0 }, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, ties: 1 },
     { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 0, ties: 1 }]).slice(0, 3)) === JSON.stringify([[60, 0], [0, 4], [67, 0]]),
     "ties: a library pattern: a step going on a TIE, else its first note starting");
  done();
  const o = attachMock({ noTies: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  s = E.parse[C.STEP_SET](await o.rq(E.req.stepSet(6, { ...x, n: 2, notes: [60, 64, 0, 0], ties: 1 })));
  ok(oi.skiff === 63 && s.ties === 0, "ties: firmware without them (57 01 63): none");
  o.done();
}

/* (Skiff W3) the DRUM-X kit (INFO 4B 01 08 after the step extras): KIT_GET reads a lane, KIT_PUT sets one, whole */
async function editorKit() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.kitLanes === 8 && info.stepx === E.TC_NAMES.length, "kit: INFO 4B 01 08 after the step extras");
  let k = E.parse[C.KIT_GET](await rq(E.req.kitGet(6)));
  ok(k.lane === 6 && k.wave === 3 && k.filt === 2 && k.snap && k.pmod === 0 && !k.drive && k.a.join() === "63,10,14,16,46" &&
     k.b.join() === "70,24,20,30,60", "kit: KIT_GET reads the factory RIM (BELL, HP, SNAP)");
  const x = { lane: 2, wave: 3, filt: 1, snap: true, pmod: 2, drive: true, a: [10, 20, 30, 40, 50], b: [60, 70, 80, 90, 127] };
  ok(E.req.kitPut(x)[1].join() === "2,3,1,1,2,1,10,20,30,40,50,60,70,80,90,127", "kit: KIT_PUT sends the lane and its 15 values");
  k = E.parse[C.KIT_PUT](await rq(E.req.kitPut(x)));
  ok(JSON.stringify(k) === JSON.stringify(x), "kit: KIT_PUT replies with the lane as set");
  k = E.parse[C.KIT_GET](await rq(E.req.kitGet(2)));
  ok(JSON.stringify(k) === JSON.stringify(x) && E.parse[C.KIT_GET](await rq(E.req.kitGet(3))).wave === 2,
     "kit: KIT_GET reads it back, the other lanes as they were");
  done();
  const o = attachMock({ noKit: true });
  ok(!E.parse[C.INFO](await o.rq(E.req.info())).kitLanes, "kit: firmware without it (no 4B): the editor shows none");
  o.done();
}

/* (Skiff W3) TRANSPORT (INFO 57 01 03 after the kit): PLAY, STOP, REC as the device's button; PROJ_NAME: the names,
   SAVE AS (upper case), changed since, RENAME (the music's too), an empty slot refused */
async function editorTransportNames() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok((info.skiff & 3) === 3 && info.kitLanes === 8, "transport: INFO 57 01 caps (TRANSPORT, PROJ_NAME) after the kit");
  let x = E.parse[C.TRANSPORT](await rq(E.req.transport()));
  ok(!x.playing && !x.rec && !x.pending, "transport: the state (stopped, none armed)");
  ok(E.parse[C.TRANSPORT](await rq(E.req.transport(1))).playing && !E.parse[C.TRANSPORT](await rq(E.req.transport(2))).playing,
     "transport: PLAY, STOP");
  x = E.parse[C.TRANSPORT](await rq(E.req.transport(4)));
  ok(x.rec === 1 << x.sel && x.playing, "transport: REC arms the selected track and starts (as its button)");
  ok(E.parse[C.TRANSPORT]([0, 1, 0, 2, 5, 1]).pending === 2 && E.req.transport()[1].length === 0, "transport: a pending STOP read; the state asks nothing");
  let p = E.parse[C.PROJ_NAME](await rq(E.req.projName()));
  ok(p.cur === -1 && p.slots[0].used && !p.slots[1].used && p.slots[1].name === "", "projects: the names (A used, none loaded)");
  p = E.parse[C.PROJ_NAME](await rq(E.req.projName(1, 1, "my song")));
  ok(!p.rc && p.op === 1 && p.cur === 1 && !p.changed && p.name === "MY SONG" && p.slots[1].used && p.slots[1].name === "MY SONG",
     "projects: SAVE AS (upper case), the music's now");
  await rq(E.req.set(0, 0, 3));
  ok(E.parse[C.PROJ_NAME](await rq(E.req.projName())).changed, "projects: changed since saved");
  p = E.parse[C.PROJ_NAME](await rq(E.req.projName(2, 1, "take 2")));
  ok(!p.rc && p.name === "TAKE 2" && p.slots[1].name === "TAKE 2" && p.changed, "projects: RENAME (the music's name too, still changed)");
  ok(E.parse[C.PROJ_NAME](await rq(E.req.projName(2, 3, "X"))).rc === 1, "projects: an empty slot can't be renamed");
  const a = E.req.projName(1, 0, "  ab\u00e9cdefghijklmnop ")[1];
  ok(String.fromCharCode(...a.slice(2, -1)) === "ABCDEFGHIJKL" && a[a.length - 1] === 0, "projects: a name cleaned (upper case, printable, 12)");
  done();
  const o = attachMock({ noProj: true });
  ok(!E.parse[C.INFO](await o.rq(E.req.info())).skiff, "projects: firmware without them (no 57): the editor shows none");
  o.done();
}

/* (Skiff W3) POOL, LIST, PERFORM (INFO 57 01 31): the pool, the lists, the scenes and staged mutes, as the device's
   controls: SET, REPS, PLAY (at once stopped, staged playing), CLEAR (refused playing), MUTE staged, CANCEL, COPY, the
   pool's CLEAR (only unused), STORE, RECALL, LAUNCH, SONG */
async function editorLists() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.skiff === 127, "song: INFO 57 01 127 (POOL, LIST, PERFORM, (#21) SMP_MIX, (2.0) ties too)");
  let q = E.parse[C.POOL](await rq(E.req.pool()));
  ok(q.pats.length === 64 && q.pats[0].len === 16 && q.pats[0].refs === 1 && q.pats[3].len === 8 && !q.pats[3].refs && !q.pats[63].len,
     "song: POOL: each pattern's LEN and uses");
  let l = E.parse[C.LIST](await rq(E.req.list()));
  ok(l.tracks.length === 6 && l.tracks[0].cur === 0 && l.tracks[0].pat === 0 && l.tracks[0].ent[0].pat === 1 && l.tracks[0].ent[1].pat === 2 &&
     l.tracks[1].cur === -1 && l.tracks[1].pat === -1 && l.tracks[0].next === -1 && !l.song, "song: LIST: the entry each track is on, its pattern, the entries");
  l = E.parse[C.LIST](await rq(E.req.list(1, 1, 0, 2)));
  ok(!l.rc && l.tracks[1].ent[0].pat === 2, "song: SET T2 A 02");
  ok(E.parse[C.LIST](await rq(E.req.list(1, 1, 1, 9))).rc === 1, "song: SET a pattern not kept refused");
  l = E.parse[C.LIST](await rq(E.req.list(1, 1, 9, 65)));
  ok(!l.rc && l.tracks[1].ent[9].pat === 65 && E.parse[C.LIST](await rq(E.req.list(2, 1, 9))).rc === 1 &&
     !E.parse[C.LIST](await rq(E.req.list(3, 1, 9))).rc, "song: (#28) SET T2 J REST (65), PLAY it; COPY of a REST refused");
  l = E.parse[C.LIST](await rq(E.req.list(5, 1, 0, 4)));
  ok(l.tracks[1].ent[0].reps === 4 && E.parse[C.LIST](await rq(E.req.list(5, 1, 5, 2))).rc === 1, "song: REPS (an empty entry refused)");
  l = E.parse[C.LIST](await rq(E.req.list(3, 1, 0)));
  ok(l.tracks[1].cur === 0 && l.tracks[1].pat === 1, "song: PLAY stopped: at once");
  await rq(E.req.transport(1));
  l = E.parse[C.LIST](await rq(E.req.list(3, 0, 1)));
  ok(l.tracks[0].next === 1 && l.tracks[0].cur === 0, "song: PLAY playing: staged");
  ok(E.parse[C.LIST](await rq(E.req.list(4, 0, 1))).rc === 3, "song: CLEAR refused while playing");
  l = E.parse[C.LIST](await rq(E.req.list(4, 1, 9)));
  ok(!l.rc && !l.tracks[1].ent[9].pat, "song: (#28) CLEAR a REST: at once, playing too");
  let p = E.parse[C.PERFORM](await rq(E.req.perform(3, 2)));
  ok(p.tracks[2].staged === 2 && !p.tracks[2].muted, "song: MUTE playing: staged");
  p = E.parse[C.PERFORM](await rq(E.req.perform(4)));
  ok(!p.tracks[2].staged && E.parse[C.LIST](await rq(E.req.list())).tracks[0].next === -1, "song: CANCEL: nothing staged");
  await rq(E.req.transport(2));
  l = E.parse[C.LIST](await rq(E.req.list(2, 0, 0)));
  ok(!l.rc && l.tracks[0].ent[0].pat === 5 && l.tracks[0].pat === 4, "song: COPY: a new pattern (05), the track on it");
  q = E.parse[C.POOL](await rq(E.req.pool(3)));
  ok(!q.rc && !q.pats[3].len && E.parse[C.POOL](await rq(E.req.pool(1))).rc === 2, "song: POOL CLEAR an unused one; one in use refused");
  p = E.parse[C.PERFORM](await rq(E.req.perform(1, 4)));
  ok(p.scenes === 16 && p.last === 4 && E.parse[C.PERFORM](await rq(E.req.perform(2, 5))).rc === 1, "song: STORE a scene; RECALL an empty one said");
  p = E.parse[C.PERFORM](await rq(E.req.perform(3, 0)));
  ok(p.tracks[0].muted && !p.tracks[0].staged, "song: MUTE stopped: at once");
  p = E.parse[C.PERFORM](await rq(E.req.perform(2, 4)));
  ok(!p.rc && !p.tracks[0].muted, "song: RECALL the scene (its mutes)");
  ok(E.parse[C.PERFORM](await rq(E.req.perform(5, 2))).launch === 2 && E.parse[C.LIST](await rq(E.req.list(6, 1))).song,
     "song: LAUNCH BEAT, SONG on");
  ok(E.req.list(6, 0)[1].join() === "6,0" && E.req.list(1, 2, 3, 9)[1].join() === "1,2,3,9" && E.req.list(3, 2, 3)[1].join() === "3,2,3" &&
     E.req.perform(4)[1].join() === "4" && E.req.pool()[1].length === 0 && E.req.pool(7)[1].join() === "1,7", "song: the requests' bytes");
  done();
  const o = attachMock({ noSong: true });
  ok(E.parse[C.INFO](await o.rq(E.req.info())).skiff === 3, "song: firmware without them (57 01 03): no Song tab");
  o.done();
}

/* (2.0, INFO 58 01 01) LIST2 (SONG ONCE, the chains as kept), LIST's 5 0 CHAIN, 6 2 ONCE, 7 NEW, 8 COPY+ (a chain: all
   of it), POOL's 2 EMPTY and 3 CLEAR UNUSED; LIST's own reply as before (ONCE: on, a CHAIN: 1x) */
async function editorSong2() {
  const C = E.CMD, L2 = (op, k, e, v) => E.req.list(op, k, e, v, true), P2 = (x) => E.parse[C.LIST2](x, true);
  const { rq, done } = attachMock({});
  ok(E.parse[C.INFO](await rq(E.req.info())).song2 === 3, "song2: INFO 58 01 03 after the wide ids ((#30) bit 1 the entries' sounds)");
  let l = P2(await rq(L2(5, 0, 0, 0)));
  ok(!l.rc && l.tracks[0].ent[0].chain && l.tracks[0].ent[0].reps === 1 && !l.tracks[0].ent[1].chain, "song2: REPS 0: T1 A on CHAIN");
  l = E.parse[C.LIST](await rq(E.req.list()));
  ok(!l.tracks[0].ent[0].chain && l.tracks[0].ent[0].reps === 1, "song2: LIST's reply (an editor before): the CHAIN as 1x");
  ok(P2(await rq(L2(5, 0, 4, 0))).rc === 1 && P2(await rq(L2(1, 0, 15, 3))).tracks[0].ent[15].pat === 3 &&
     P2(await rq(L2(5, 0, 15, 0))).rc === 1, "song2: CHAIN refused on an empty entry and on P (none after it)");
  await rq(L2(4, 0, 15));
  l = P2(await rq(L2(6, 2)));
  ok(l.song === 2 && E.parse[C.LIST](await rq(E.req.list())).song === 1, "song2: SONG 2 ONCE (LIST's reply: on)");
  l = P2(await rq(L2(7, 0, 2)));
  let q = E.parse[C.POOL](await rq(E.req.pool()));
  ok(!l.rc && l.tracks[0].ent[2].pat === 5 && q.pats[4].len === 16 && q.pats[4].refs === 1, "song2: NEW into T1 C: 05, the first free");
  l = P2(await rq(L2(8, 0, 1)));
  const t = l.tracks[0].ent;
  ok(!l.rc && t[3].pat === 6 && t[3].chain && t[4].pat === 7 && !t[4].chain && t[0].pat === 1 && t[1].pat === 2,
     "song2: COPY+ of the chain A-B (from B): D-E, 06 07 (the first free after each), a chain again");
  ok(P2(await rq(L2(8, 0, 5))).rc === 1 && P2(await rq(L2(1, 0, 5, 65))).tracks[0].ent[5].pat === 65 &&
     P2(await rq(L2(8, 0, 5))).rc === 1, "song2: COPY+ of an empty entry and of a REST refused");
  q = E.parse[C.POOL](await rq(E.req.pool(0, 2)));
  ok(!q.rc && q.op === 2 && q.pats[0].len === 16 && q.pats[0].refs === 1 && E.parse[C.POOL](await rq(E.req.pool(63, 2))).rc === 1,
     "song2: POOL EMPTY of a used one (it and its entries stay); an empty one refused");
  q = E.parse[C.POOL](await rq(E.req.pool(null, 3)));
  ok(!q.rc && q.op === 3 && !q.pats[3].len && q.pats[0].len && E.parse[C.POOL](await rq(E.req.pool(null, 3))).rc === 1,
     "song2: CLEAR UNUSED (04 goes, the used ones stay); then nothing to clear");
  await rq(L2(7, 0, 2));                          /* (05 unused then) */
  await rq(E.req.transport(1));
  ok(E.parse[C.POOL](await rq(E.req.pool(null, 3))).rc === 3 && E.parse[C.POOL](await rq(E.req.pool(0, 2))).rc === 3,
     "song2: CLEAR UNUSED, EMPTY refused while playing");
  await rq(E.req.transport(2));
  ok(js(L2(5, 1, 2, 0)) === js([82, [5, 1, 2, 0]]) && js(L2(6, 2)) === js([82, [6, 2]]) && js(L2(8, 0, 4)) === js([82, [8, 0, 4]]) &&
     js(E.req.list(7, 0, 4)) === js([79, [7, 0, 4]]) && js(E.req.pool(9, 2)) === js([78, [2, 9]]) && js(E.req.pool(null, 3)) === js([78, [3]]),
     "song2: the requests' bytes");
  {   /* ((2.0, #30, INFO 58 01 bit 1) LIST2: each entry's sound after its REPS; op 9 puts one on (an engine + 1 and its
         preset; 64 + a user slot), 0 0 takes it off; rc 1 for an empty entry or a sound the track can't carry; CLEAR takes
         it, COPY+ copies it */
    const snd = (k, e) => js(l.tracks[k].ent[e].snd);
    await rq(E.req.upStore(3, "MOCK SND"));                               /* (U04: the track's sound) */
    l = P2(await rq(L2(9, 0, 0, [1, 2])));
    ok(!l.rc && snd(0, 0) === "[1,2]" && snd(0, 1) === "[0,0]", "song2 (#30): op 9 puts ANALOG's preset 3 on T1 A");
    l = P2(await rq(L2(9, 0, 1, [64 + 3, 0])));
    const user = !l.rc && snd(0, 1) === "[67,0]";
    l = P2(await rq(L2(9, 0, 6, [1, 0])));
    let refused = l.rc === 1 && snd(0, 6) === "[0,0]";                   /* (G: empty) */
    for (const s of [[63, 0], [2, 0], [1, 127], [64 + 20, 0]]) refused &&= P2(await rq(L2(9, 0, 0, s))).rc === 1;
    await rq(L2(1, 4, 0, 65));                                             /* (T5 A: a REST, a light track) */
    l = P2(await rq(L2(9, 4, 0, [10, 0])));                                /* (PHYS) */
    refused &&= l.rc === 1 && snd(4, 0) === "[0,0]" && snd(0, 0) === "[1,2]";
    ok(refused, "song2 (#30): refused: an empty entry, no such engine, DIGITAL's, no such preset or user sound, PHYS on T5");
    l = P2(await rq(L2(8, 0, 0)));                                         /* (COPY+ of A (01): into the first empty after) */
    const f = l.tracks[0].ent.findIndex((en, j) => j > 0 && js(en.snd) === "[1,2]");
    l = P2(await rq(L2(4, 0, f)));
    ok(!l.rc && f > 0 && snd(0, f) === "[0,0]", "song2 (#30): COPY+ copies an entry's sound, CLEAR takes it");
    l = P2(await rq(L2(9, 0, 0, [0, 0])));
    ok(!l.rc && snd(0, 0) === "[0,0]" && js(L2(9, 0, 1, [67, 0])) === js([82, [9, 0, 1, 67, 0]]),
       "song2 (#30): 0 0 takes it off; the request's bytes");
    ok(user, "song2 (#30): a user sound (U04) as 64 + its slot");
    const r = L2(1, 0, 2, 3), a = await rq(r), dv = { info: { song2: 3 } };
    ok(js(E.songReply(dv, r, a)) === js(P2(a)) && E.songReply(dv, r, a).tracks.length === 6 &&
       js(E.songReply(dv, E.req.list(), await rq(E.req.list()))) === js(E.parse[C.LIST](await rq(E.req.list()))),
       "song2 (#30): a Song tab change's reply read with the entries' sounds (songReply), LIST's without");
  }
  done();
  {
    const o = attachMock({ noSnd: true });
    const info = E.parse[C.INFO](await o.rq(E.req.info()));
    const l1 = E.parse[C.LIST2](await o.rq(L2(0, 0, 0)));
    const err = await o.rq(L2(9, 0, 0, [1, 2]), { timeout: 100, retries: 0 }).then(() => null, (e) => e.message);
    const r = L2(1, 0, 2, 3), l3 = E.songReply({ info }, r, await o.rq(r));
    ok(info.song2 === 1 && l1.tracks.length === 6 && l1.tracks[0].ent[0].pat === 1 && /^timeout/.test(err || "") &&
       l3.tracks.length === 6 && l3.tracks[0].ent[2].pat === 3,
       "song2 (#30): firmware before the entry sounds (58 01 01): LIST2's entries two bytes, op 9 unanswered");
    o.done();
  }
  const o = attachMock({ noSong2: true });
  const info = E.parse[C.INFO](await o.rq(E.req.info()));
  const err = await o.rq(L2(0, 0, 0), { timeout: 100, retries: 0 }).then(() => null, (e) => e.message);
  ok(info.skiff === 127 && !info.song2 && /^timeout/.test(err || ""), "song2: firmware before (no 58 01): song2 0, LIST2 unanswered");
  o.done();
}

/* RATCH: a step's ratchet (1..4) after its chance, INFO 52 01 04; older firmware (no tag): none, replies read x1 */
async function editorRatchet() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  if (info.ratchet === undefined) {               /* (an editor without it) */
    console.log(`${"ratchet (this editor has none)".padEnd(64)} skip`);
    done();
    return;
  }
  let s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 1, vel: 100, chance: 70, ratchet: 3 })));
  ok(info.ratchet === 4 && s.ratchet === 3 && s.chance === 70 && s.flags === 1, "ratchet: INFO 52 01 04, STEP_SET x3 after the chance");
  s = E.parse[C.STEP_SET](await rq(E.req.stepSet(6, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 100, chance: 70 })));
  ok(s.ratchet === 3 && s.notes[0] === 62, "ratchet: a STEP_SET without it (an older editor) keeps the step's");
  ok(E.req.stepSet(6, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0, vel: 100, ratchet: 2 })[1].length === 9,
    "ratchet: never sent without the chance before it");
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(1, 2, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90, chance: 100, ratchet: 4 })));
  ok(w.ratchet === 4 && E.parse[C.TRACK_STEP](await rq(E.req.trackStep(1, 2))).ratchet === 4, "ratchet: TRACK_STEP x4 on another track");
  const fw = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3, 0x52, 1, 4]);
  const f105 = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0, 0x53, 1, 3,
    0x50, 1, 3, 0x4E, 1, 12, 0x52, 1, 4]);         /* 1.0.5: FM6 v2, MENU settings, then RATCH */
  ok(f105.ratchet === 4 && f105.fm6.caps === 3, "ratchet: INFO 52 01 04 after 50 01 03 and 4E 01 count");
  const old = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3]);
  ok(fw.ratchet === 4 && fw.syncCaps === 3 && old.ratchet === 0 && E.parse[C.STEP_GET]([1, 1, 60, 0, 0, 0, 0, 0, 96, 0, 0, 0, 100]).ratchet === 1,
    "ratchet: the firmware's INFO trailer; 1.0's has none and its steps read x1");
  const steps = E.stepsFromPattern([[60, 1 | 2 << 3], [0, 4], [62, 3 << 3], ...Array(13).fill([0, 0])]);
  ok(steps[0].ratchet === 3 && steps[0].flags === 1 && steps[2].ratchet === 4 && steps[1].time === 1 &&
     JSON.stringify(E.patternFromSteps(steps).slice(0, 3)) === JSON.stringify([[60, 17], [0, 4], [62, 24]]),
     "ratchet: a user preset's pattern keeps it in its flags (8 | 16)");
  done();
  const o = attachMock({ noSync: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  s = E.parse[C.STEP_GET](await o.rq(E.req.stepGet(0)));
  ok(oi.ratchet === 0 && s.ratchet === 1 && s.chance === 100, "ratchet: firmware without it: no tag, steps x1");
  o.done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 143 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(info.syncCaps === 3 && ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: INFO 53 01 03, no RELOAD echo of an editor PRESET, nothing after its STEP_SET");
  {
    const o = attachMock({ watchMs: 250, noSync: true });
    const oi = E.parse[C.INFO](await o.rq(E.req.info()));
    await E.startWatch(o.rq);
    await o.rq(E.req.preset(1, 2));
    await sleep(10);
    ok(oi.syncCaps === 0 && oi.fm6 && o.ev.pushes.filter((f) => f.cmd === C.RELOAD).length === 1,
      "live: firmware before the sync tag echoes the editor's PRESET as RELOAD (the editor skips it)");
    o.done();
    const fw = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 27, 0x53, 1, 3]);
    ok(fw.backupCaps === 3 && fw.fm6 && fw.fm6.bank === 27 && !fw.fm6.caps && fw.syncCaps === 3,
      "live: the INFO trailer of 1.0.2: backup, FM6 (a bank), then the sync tag; no FM6 v2");
    const f3 = E.parse[C.INFO]([88, 0, 0, 91, 27, 64, 83, 4, 16, 0x55, 1, 9, 0x4d, 1, 64, 1, 0x42, 1, 3, 0x46, 1, 8, 0, 0x53, 1, 3, 0x50, 1, 3]);
    ok(f3.fm6 && f3.fm6.bank === 0 && f3.fm6.caps === 3 && f3.syncCaps === 3, "live: the INFO trailer of 1.0.3: FM6 v2 after the sync tag (no bank, preset patches)");
  }

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

async function preferenceReplies() {
  const C = E.CMD, unknown = [];
  const link = new E.Link(() => {}, { onUnknown: f => unknown.push(f) });
  for (const [request, stale, reply] of [
    [E.req.uiSet(0, 4), [0, 1, 1], [0, 0, 4]],
    [E.req.uiSet(0, 4), [0, 0, 3], [0, 0, 4]],
    [E.req.favGet(0, 2, 1), [0, 0, 3, 64, 1, 1], [0, 0, 2, 64, 1, 1]],
    [E.req.favSet(0, 2, true), [4, 0, 2, 64, 0], [4, 0, 2, 64, 1]],
  ]) {
    const pending = link.request(request);
    link.receive(E.frame(request[0], stale));
    ok(link.cur !== null, 'preferences: a reply for another setting or favorite cannot finish the request');
    link.receive(E.frame(request[0], reply));
    ok(eq(await pending, reply), 'preferences: the matching reply completes the request');
  }
  ok(unknown.length === 4 && link.idle, 'preferences: stale replies are routed away without leaving requests busy');
  link.close();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 6 && tr.sel === 0 && tr.ntrk === 6 && tr.tracks[0].engine === 0 && tr.tracks[1].engine === 12
    && tr.tracks[3].engine === 10 && tr.tracks[3].preset === 0 && tr.tracks[4].engine === 4 && tr.tracks[4].preset === 0
    && tr.tracks[5].engine === 10, "tracks: INFO NTRK, TRACK lists 6 parts (track 4: DRUM KIT; 5 PIANO, 6 DRUM KIT)");
  {   /* (Skiff phase 6) INFO 54: the light tracks and the engines they may not play; MOTION's track bound is INFO's */
    const heavy = 1 << info.engines.indexOf("GRAIN") | 1 << info.engines.indexOf("PHYS");
    let threw = false;
    try { E.req.motion(6, 0, {}, info.ntrk); } catch (_) { threw = true; }
    ok(info.light.tracks === 0x30 && info.light.engines === heavy && E.req.motion(5, 0, {}, info.ntrk)[1][0] === 5 && threw,
      "tracks: INFO 54: T5, T6 light (no GRAIN, no PHYS there); MOTION asks for T6, not a seventh");
  }
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === 12 && d0.engine === 0 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute, track 4 as any other */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.tracks[3].p[0] === 64 && tr2.tracks[3].level === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (track 4: its own LEVEL)");
  /* track 4 has a sound: stored and loaded as any other */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  const dl = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(dd.engine === 10 && us.rc === 0 && ul.rc === 0 && dl.engine === 12, "tracks: track 4 selected -> DUMP engine DRUM, UP_STORE / UP_LOAD rc 0");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep every track */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 6 && m0.tracks.length === 6 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === 10
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of tracks that are not selected (clamped) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.tracks[3].p[0] === 127 && m1.tracks[3].level === 127
    && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 0 && chd.id === 0 && chd.value === 90, "mixer: track 4's level on the device pushes its LEVEL (P_LEVEL)");
  /* track 4's demo beat: the DRUM kit's lane hits and accents */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  ok(st.every((s) => s.n === 0) && st[0].hit === 1 && st[0].acc === 1 && st[1].hit === 8
    && st[4].hit === 2 && st[4].acc === 2 && st[14].hit === 16,
    "mixer: the mock's track 4 holds the BEAT grid (KICK CHH ... SNARE ... OHH)");
  ok(JSON.stringify(E.parseNotes("C4 E4 G4")) === "[60,64,67]" && JSON.stringify(E.parseNotes("36 38")) === "[36,38]" && E.parseNotes("KICK") === null,
    "mixer: notes parse as numbers or note names (no GM drum names)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is FM6 PAD: ALG 0..32 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 32 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  {   /* (Skiff W3) the mixer's REV (P_REV 36): read with the pans, set as they are (its own key), kept over a light read */
    const REV = 36, r2 = await E.mixer.setPan(rq, 2, 0, REV, 90, true, "rev:2");
    const mr = await E.mixer.read(rq, info, { pan: PAN, rev: REV });
    const ml = await E.mixer.read(rq, info, { pan: PAN, rev: REV }, mr.tracks);
    ok(r2 === 90 && mr.tracks[2].rev === 90 && mr.tracks[2].pan === -40 && ml.tracks[2].rev === 90 && ml.tracks[2].pan === -40,
       "v4: the mixer's REV over TRACK_PARAM, read with the pans (kept by a light read)");
  }
  const bad = await rq(E.req.trackParam(6, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 7 (past the six): no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------------------------ FM6 patches --- */
/* the 6-operator patch formats (FM6 in editor.html == eng_fm6.c / fm6_test.c), the SysEx files, the protocol
   (cmds 68..71) against the mock, PTCH, the macros at the pe0 INFO gives */
async function editorFm6() {
  const F6 = E.FM6;
  ok(F6.FACTORY_PK.every((pk) => eq(F6.pack(F6.unpack(pk)), pk)) && eq(F6.pack(F6.unpack(F6.INIT_PK)), F6.INIT_PK),
    "FM6: pack(unpack(x)) == x for the factory patches and the init voice");
  let inrange = true, seed = 7;
  for (let k = 0; k < 500; k++) {
    const pk = Array.from({ length: 128 }, () => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 16 & 255);
    const v = F6.unpack(pk);
    for (let i = 0; i < F6.SIZE; i++) inrange &&= i >= F6.NAME ? v[i] >= 32 && v[i] <= 126 : v[i] <= F6.max(i);
  }
  ok(inrange, "FM6: any 128 bytes unpack into range");
  const voices = F6.FACTORY_PK.map((pk) => F6.unpack(pk));
  const bank = F6.bankSysex(voices), one = F6.singleSysex(voices[3]);
  ok(bank.length === 4104 && bank[0] === 0xF0 && bank[3] === 9 && bank[4103] === 0xF7 && one.length === 163 && one[5] === 0x1B,
    "FM6: a 32-voice bank SysEx is 4104 bytes, a single voice 163");
  let r = F6.parseSysex(bank);
  ok(r.voices.length === 32 && !r.badSum && r.voices.slice(0, 8).every((x, k) => eq(F6.pack(x.v), F6.FACTORY_PK[k])) &&
     r.voices[8].name === "INIT VOICE" && r.voices[1].name === "GLASS BELL", "FM6: bank SysEx round trip (names, checksum)");
  r = F6.parseSysex(Uint8Array.from([...one, ...one]));
  ok(r.voices.length === 2 && eq(F6.pack(r.voices[1].v), F6.FACTORY_PK[3]), "FM6: two single-voice messages in one file");
  const bad = Uint8Array.from(one); bad[161] ^= 1;
  ok(F6.parseSysex(bad).badSum === 1, "FM6: a wrong checksum is reported (the voice still read)");
  ok(F6.parseSysex(bank.subarray(6, 4102)).voices.length === 32 && F6.parseSysex(one.subarray(6, 161)).voices.length === 1,
    "FM6: raw 4096 / 155-byte files read too");
  fm6Tolerant(F6, voices, bank, one);
  ok(F6.carriers(0).join() === "1,3" && F6.carriers(31).length === 6 && F6.feedbackOp(0) === 6 && F6.feedbackOp(1) === 2,
    "FM6: carriers and the feedback operator of an algorithm");

  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 200 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (x) => link.request(x), C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.fm6 && info.fm6.factory === 8 && info.fm6.bank === 0 && info.fm6.caps === 3, "FM6: INFO tag (8 factory, no bank; FM6 v2 caps 3)");
  let list = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(list.factory === 8 && list.bank === 0 && list.slots.length === 8 && list.slots[0].name === "TINE EP", "FM6: LIST names the factory patches, nbank 0");
  const mine = F6.setName(F6.factory(2), "my bass");
  let p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(1, 4, F6.pack(mine))));
  let g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(1, 4)));
  const e0 = E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4)));
  ok(p.rc === 3 && g.rc === 3 && !g.packed && e0.rc === 3, "FM6: the bank (target 1): GET / PUT / ERASE answer rc 3, no bank");
  /* the selected track to FM6; send a voice: the track's own patch, SLOT OWN; F2 and back to OWN */
  const eng = info.engines.indexOf("FM6"), slotId = info.pe0 + 7;
  await rq(E.req.set(1, 20, eng));
  p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(0, 0, F6.pack(mine))));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  let sv = E.parse[C.GET](await rq(E.req.get(0, slotId))).value;
  ok(!p.rc && F6.name(F6.unpack(g.packed)) === "MY BASS" && sv === 8, `FM6: send to the track: its own patch, SLOT (P_E0 + 7 = ${slotId}) OWN`);
  await rq(E.req.set(0, slotId, 1));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  ok(eq(g.packed, F6.FACTORY_PK[1]), "FM6: SLOT F2 loads the factory patch");
  await rq(E.req.set(0, slotId, 8));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  ok(F6.name(F6.unpack(g.packed)) === "MY BASS", "FM6: SLOT back to OWN brings the own patch back");
  const edited = F6.unpack(g.packed); edited[F6.VI.ALG] = 31;
  p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(0, 0, F6.pack(edited))));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  ok(!p.rc && F6.unpack(g.packed)[F6.VI.ALG] === 31, "FM6: PUT to the track: its own patch changed");
  /* a user preset carries it (target 3, the librarian's bank.get / put) */
  E.parse[C.UP_STORE](await rq(E.req.upStore(20, "KEEP ME")));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(3, 20)));
  ok(!g.rc && eq(g.packed, F6.pack(edited)), "FM6: UP_STORE of an FM6 track keeps its patch (FM6_GET user 21)");
  await rq(E.req.fm6Put(0, 0, F6.FACTORY_PK[0]));
  await rq(E.req.upLoad(20));
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(0, 0)));
  sv = E.parse[C.GET](await rq(E.req.get(0, slotId))).value;
  ok(eq(g.packed, F6.pack(edited)) && sv === 8, "FM6: UP_LOAD plays it again, SLOT OWN");
  const u = await E.bank.get(rq, info, 20);
  ok(u.used && eq(u.fm6, F6.pack(edited)), "FM6: the librarian reads a user preset with its patch");
  const other = F6.pack(F6.setName(F6.factory(5), "OTHER"));
  let rc = await E.bank.put(rq, 21, { ...u, name: "COPY", fm6: other }, info);
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(3, 21)));
  ok(rc === 0 && !g.rc && eq(g.packed, other), "FM6: the librarian writes a user preset with its patch (UP_PUT, then FM6_PUT user)");
  rc = await E.bank.put(rq, 22, { ...u, name: "OLD FW", fm6: other }, { ...info, fm6: { ...info.fm6, caps: 0 } });
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(3, 22)));
  ok(rc === 0 && g.rc === 2, "FM6: firmware without preset patches (1.0.2): UP_PUT only");
  const lf = E.readLibraryFile(JSON.parse(JSON.stringify(E.libraryFile("library", [{ ...u, fm6: other }], { engines: info.engines }))), { engines: info.engines });
  ok(lf.patches.length === 1 && eq(lf.patches[0].fm6, other) && !("fm4" in lf.patches[0]), "FM6: a library file keeps an FM6 sound's patch (fm6)");
  g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(3, 0)));
  p = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(3, 0, other)));
  ok(g.rc === 2 && p.rc === 1, "FM6: a user preset of another engine: GET rc 2, PUT rc 1");
  p = E.parse[C.FM6_PUT](await rq([C.FM6_PUT, [0, 9, 1, 2, 3]]));
  ok(p.rc === 1, "FM6: a short record or a fifth track: rc 1");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, noFm6: true });
  const l2 = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (ev) => l2.receive(ev.data);
  ok(E.parse[C.INFO](await l2.request(E.req.info())).fm6 === null, "FM6: firmware without it: no tag (the tab shows a hint)");
  l2.close(); old.stop();
}

/* the variants real 6-operator voice files have (parseSysex), every one built here byte by byte */
function fm6Tolerant(F6, voices, bank, one) {
  const U = (...xs) => Uint8Array.from(xs.flatMap((x) => Array.from(x)));
  const names = (r) => r.voices.map((x) => x.name);
  const bankOk = (r, n = 32) => r.voices.length === n && r.voices.slice(0, 8).every((x, k) => eq(F6.pack(x.v), F6.FACTORY_PK[k]))
    && r.voices[1].name === "GLASS BELL" && r.voices[8].name === "INIT VOICE";
  const msg = (hdr, n, fill = 0) => { const d = new Array(n).fill(fill); return U(hdr, d, [F6.checksum(d), 0xF7]); };
  const otherMaker = U([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0, 0x7F, 0, 0x41, 0xF7]);
  let r = F6.parseSysex(U(bank.subarray(0, 4102), [0xF7]));
  ok(bankOk(r) && !r.badSum && !r.short, "FM6 import: bank without its checksum (4103 bytes)");
  r = F6.parseSysex(bank.subarray(0, 4102));
  ok(bankOk(r) && !r.short, "FM6 import: bank without checksum and F7 (EOF)");
  r = F6.parseSysex(U(bank.subarray(0, 4103), [0x00], [0xF7]));
  ok(bankOk(r) && !r.badSum, "FM6 import: bank with a stray byte before F7 (4105 bytes)");
  r = F6.parseSysex(U(bank.subarray(0, 4103), one));
  ok(r.voices.length === 33 && bankOk({ voices: r.voices.slice(0, 32) }) && r.voices[32].name === "BRASS SECT",
    "FM6 import: bank without F7, a single voice right after");
  r = F6.parseSysex(bank.subarray(0, 4000));
  ok(r.voices.length === 31 && r.short === 1 && r.voices[30].name === F6.name(F6.init()), "FM6 import: bank cut at EOF: its 31 whole voices");
  r = F6.parseSysex(one.subarray(0, 6 + 150));
  ok(r.voices.length === 1 && r.short === 1 && r.voices[0].name === "BRASS", "FM6 import: single voice cut in its name");
  r = F6.parseSysex(one.subarray(0, 6 + 120));
  ok(r.voices.length === 0, "FM6 import: single voice cut before its voice bytes: none");
  const b10 = Uint8Array.from(bank); b10[4] = 0x10; b10[2] = 0x05;
  r = F6.parseSysex(b10);
  ok(bankOk(r), "FM6 import: byte count written 10 00, device 6");
  const one0 = Uint8Array.from(one); one0[4] = 0; one0[5] = 0;
  ok(names(F6.parseSysex(one0)).join() === "BRASS SECT", "FM6 import: single voice with an odd byte count");
  r = F6.parseSysex(U([0x00, 0x13, 0x55, 0xF7, 0x80], otherMaker, bank, [0xFE, 0x00, 0x00], one, [0x0A, 0x0D]));
  ok(r.voices.length === 33 && r.skipped === 1 && r.kinds.join() === "maker:41", "FM6 import: junk and another maker's message around the voices");
  r = F6.parseSysex(U(bank, bank));
  ok(r.voices.length === 64 && bankOk({ voices: r.voices.slice(32) }), "FM6 import: two banks in one file: 64 voices");
  const raw = bank.subarray(6, 4102);
  r = F6.parseSysex(U(raw, raw, raw));
  ok(r.voices.length === 96 && bankOk({ voices: r.voices.slice(64) }) && !r.sysex, "FM6 import: raw 3 x 4096 bytes: 96 voices");
  ok(names(F6.parseSysex(Uint8Array.from(F6.FACTORY_PK[1]))).join() === "GLASS BELL", "FM6 import: raw 128-byte packed voice");
  /* a file of the format's second generation: the supplement bank (format 6, 1120 bytes), the voices, a performance (format 1, 94 bytes),
     a universal LM block: only the voices kept */
  const lm = U([0xF0, 0x43, 0x00, 0x7E, 0x01, 0x28], Array.from("LM  8973PM", (c) => c.charCodeAt(0)), new Array(30).fill(0), [0, 0xF7]);
  r = F6.parseSysex(U(msg([0xF0, 0x43, 0, 6, 0x08, 0x60], 1120), bank, msg([0xF0, 0x43, 0, 1, 0, 0x5E], 94), lm));
  ok(bankOk(r) && r.skipped === 3 && r.kinds.join() === "other43", "FM6 import: supplement / performance blocks skipped, voices kept");
  r = F6.parseSysex(msg([0xF0, 0x43, 0, 4, 0x20, 0], 4096));
  ok(!r.voices.length && r.kinds.join() === "fm4", "FM6 import: a 4-operator 32-voice bank: none, named as such");
  r = F6.parseSysex(otherMaker);
  ok(!r.voices.length && r.kinds.join() === "maker:41" && r.sysex, "FM6 import: another maker's SysEx: none, its id");
  r = F6.parseSysex(U([0xF0, 0x00, 0x20, 0x29, 0x02, 0xF7]));
  ok(r.kinds.join() === "maker:00 20 29", "FM6 import: a 3-byte maker id");
  r = F6.parseSysex(U([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7]));
  ok(!r.voices.length && r.kinds.join() === "universal", "FM6 import: a universal message: none");
  r = F6.parseSysex(Uint8Array.from({ length: 1000 }, (_, k) => k & 0x7F));
  ok(!r.voices.length && !r.sysex && !r.kinds.length, "FM6 import: 1000 bytes, no SysEx: none");
  r = F6.parseSysex(msg([0xF0, 0x43, 0, 9, 0x20, 0], 4096, 0x7F));
  let inrange = r.voices.length === 32;
  for (const { v } of r.voices) for (let i = 0; i < F6.SIZE; i++) inrange &&= i >= F6.NAME ? v[i] >= 32 && v[i] <= 126 : v[i] <= F6.max(i);
  ok(inrange, "FM6 import: every voice sanitized (all 7F bank)");
}

/* the 6-OP FM tab without the bank (1.0.3): import -> pick -> edit -> send to track; factory patches into the editor */
function fm6TabNoBank() {
  const tab = html.slice(html.indexOf('<section class="panel" id="p-fm6"'), html.indexOf("</section>", html.indexOf('id="p-fm6"')));
  const code = html.slice(html.indexOf("let fm6v = FM6.init();"), html.indexOf('$("connect").addEventListener'));
  ok(["fm6import", "fm6file", "fm6imported", "fm6send", "fm6read", "fm6slot", "fm6load", "fm6keep"].every((id) => tab.includes(`id="${id}"`))
    && !["fm6store", "fm6erase", "fm6bankexp"].some((id) => html.includes(`"${id}"`)), "FM6 tab: import, voices, send, factory load and the hint; no store / erase / bank export");
  ok(!/TARGET\.BANK|fm6Erase|fm6Store|defaultSlot/.test(code), "FM6 tab: no bank requests");
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const T = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")"), en = T.en, ja = T.ja;
  ok(/send it to a track, then save a user preset \(SAVE\) or a project/.test(en.fm6Keep) && ja.fm6Keep && !/B1|B27|bank/i.test(en.fm6Help + en.fm6NeedDevice),
    "FM6 tab: the hint (send to a track, then SAVE on the device), no B slots in the help");
}

/* ------------------------------------------------- editor tabs and strings --- */
async function editorSong() {
  const m = E.makeMockDevice({ auto: false });
  const link = new E.Link((d) => [...m.access.outputs.values()][0].send(d), { timeout: 100 });
  [...m.access.inputs.values()][0].onmidimessage = (e) => link.receive(e.data);
  const rq = (r) => link.request(r);
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.chainRows === 16, "SONG: INFO capability appended");
  const ask = async (op = 0, rows = []) => E.parse[C.SONG](await rq(E.req.song(op, rows)));
  const rows = [{ slot: 0, repeat: 4 }, { slot: 1, repeat: 2 }, { slot: 0, repeat: 1 }];
  let r = await ask(1, rows);
  ok(r.rc === 0 && JSON.stringify(r.rows) === JSON.stringify(rows), "SONG: rows and repeats round trip");
  r = await ask(2);
  ok(r.rc === 4 && !r.playing, "SONG: missing source rejected before PLAY");
  await rq(E.req.project(1, 1));
  r = await ask(2);
  ok(!r.rc && r.playing && r.row === 0 && r.remaining === 4, "SONG: PLAY starts from row 1");
  r = await ask(1, [{ slot: 0, repeat: 1 }]);
  ok(r.rc === 2 && r.count === 3, "SONG: editing during PLAY refused, rows kept");
  await ask(3);
  r = E.parse[C.SONG](await rq([C.SONG, [1, 1, 0, 0]]));
  ok(r.rc === 1 && r.count === 3, "SONG: zero repeats rejected without changing rows");
  r = E.parse[C.SONG](await rq([C.SONG, [1, 2, 0, 1]]));
  ok(r.rc === 1 && r.count === 3, "SONG: short row list rejected");
  await rq(E.req.project(1, 2));
  await ask(1, []);
  await rq(E.req.project(0, 2));
  r = await ask();
  ok(JSON.stringify(r.rows) === JSON.stringify(rows) && !r.playing, "SONG: project save/load recalls the chain");
  await ask(1, []);
  r = await ask(2);
  ok(r.rc === 1 && !r.playing, "SONG: empty chain does not PLAY");
  link.close(); m.stop();
  const old = E.makeMockDevice({ auto: false, v5: true });
  const l = new E.Link((d) => [...old.access.outputs.values()][0].send(d));
  [...old.access.inputs.values()][0].onmidimessage = (e) => l.receive(e.data);
  ok(E.parse[C.INFO](await l.request(E.req.info())).chainRows === 0, "SONG: older firmware has no capability");
  l.close(); old.stop();
}

/* Run the page's actual session helpers and connect-load path with delayed replies. */
async function editorSessions() {
  const state = html.slice(html.indexOf("let busy = 0;"), html.indexOf("let dragging", html.indexOf("let busy = 0;")));
  const requests = html.slice(html.indexOf("function assertSession(d)"), html.indexOf("/* the whole connect sequence */"));
  const loading = html.slice(html.indexOf("async function load(d)"), html.indexOf("async function getDescOrNull"));
  const library = html.slice(html.indexOf("async function libOp(fn)"), html.indexOf("const checkRc", html.indexOf("async function libOp(fn)")));
  let shown = 0, adopted, adoptStarted;
  const nodes = new Map();
  const noop = () => {};
  const S = vm.runInNewContext(`let dev = null, lastDump = 0, libBusy = false; ${state} ${requests} ${loading} ${library}
    ;({ beginBusy, resetBusy, sessionRequest, load, libOp,
       setDevice: d => { dev = d; }, count: () => busy })`, {
    Date, Error,
    $: id => { if (!nodes.has(id)) nodes.set(id, {}); return nodes.get(id); },
    CMD: { INFO: 1, PROJECT: 2, SMP_INFO: 3, DUMP: 4 },
    parse: { 1: r => r, 2: () => ({ used: false }), 3: () => ({}), 4: r => r },
    req: { info: () => "info", project: () => "project", smpInfo: () => "sample", dump: () => "dump" },
    paramKeys: noop, libAdopt: () => { adoptStarted?.(); return adopted || Promise.resolve(); },
    buildUI: () => { shown++; }, renderPanels: noop, loadSteps: noop, loadStepsIfShown: noop, readMenu: noop, readTransport: noop, readProj: noop,
    startWatch: async () => false, renderLive: noop, renderLib: noop,
    renderBank: noop, renderSysinfo: noop, sayK: noop, report: noop, syncPreferences: noop,
  });
  const oldRelease = S.beginBusy();
  S.resetBusy();
  const newRelease = S.beginBusy();
  oldRelease();
  ok(S.count() === 1, "editor: old completion cannot release new connection's busy count");
  newRelease(); newRelease(); oldRelease();
  ok(S.count() === 0, "editor: operation release is idempotent and never goes negative");

  let resolve;
  const d = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(d);
  const stale = S.sessionRequest(d, "dump").then(() => null, e => e);
  S.setDevice({}); resolve({});
  ok((await stale)?.message === "closed", "editor: late reply from an unplugged device is rejected");
  let sent = 0;
  d.link.request = async () => { sent++; };
  const wrong = await S.sessionRequest(d, "write").then(() => null, e => e);
  ok(wrong?.message === "closed" && sent === 0, "editor: captured request cannot write through a replacement connection");

  const old = { link: { request: () => new Promise(r => { resolve = r; }) } };
  S.setDevice(old);
  const loadingOld = S.load(old).then(() => null, e => e);
  S.resetBusy(); S.setDevice({});
  const currentRelease = S.beginBusy();
  resolve({});
  ok((await loadingOld)?.message === "closed" && !shown && S.count() === 1,
     "editor: interrupted initial load cannot paint or unlock a new session");
  currentRelease();

  adopted = new Promise(r => { resolve = r; });
  const reachedAdoption = new Promise(r => { adoptStarted = r; });
  const next = { pdesc: [], gdesc: [], names: [], slotUsed: [], link: { request: async kind =>
    kind === "info" ? { pcount: 0, gcount: 0, nengines: 0 } : {} } };
  S.setDevice(next);
  const loadingNext = S.load(next).then(() => null, e => e);
  await reachedAdoption;
  S.resetBusy(); S.setDevice({}); resolve();
  ok((await loadingNext)?.message === "closed" && !shown && S.count() === 0,
     "editor: unplug during library adoption aborts the old load");
  adopted = null;
  S.setDevice(next);
  await S.load(next);
  ok(shown === 1 && S.count() === 0, "editor: a current connection still completes its initial load");

  const savedDevice = { link: { request: async () => { sent++; } } };
  sent = 0;
  S.setDevice(savedDevice);
  const storageWait = new Promise(r => { resolve = r; });
  const copying = S.libOp(async (device, request) => { await storageWait; await request("save"); });
  S.resetBusy(); S.setDevice({}); resolve();
  await copying;
  ok(!sent && S.count() === 0, "editor: library storage delay cannot send a save to a new device");
}

function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 9 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------------------ the Learn page --- */
/* web/learn/index.html (one page, every chapter) against steps.js (from the video captures) and ../panel.js */
function learnPage() {
  const page = readFileSync(join(HERE, "learn/index.html"), "utf8");
  const LEARN = vm.runInNewContext(readFileSync(join(HERE, "learn/steps.js"), "utf8") + ";LEARN");
  const win = {};
  vm.runInNewContext(readFileSync(join(HERE, "panel.js"), "utf8"), { window: win });
  const P = win.PANEL;
  const steps = [...page.matchAll(/<li class="step" id="([^"]+)"/g)].map((m) => m[1]);
  const ids = new Set([...page.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));
  const sections = [...page.matchAll(/<section class="chapter[^"]*" id="([^"]+)"/g)].map((m) => m[1]);
  ok(steps.length === new Set(steps).size && steps.every((id) => sections.includes(id.split("/")[0])),
    `learn: ${steps.length} step ids, unique, each "<chapter>/<step>" of its section`);
  const stray = Object.keys(LEARN.steps).filter((k) => !steps.includes(k));
  ok(!stray.length, `learn: every step in steps.js is on the page${stray.length ? " (not: " + stray + ")" : ""}`);
  const part = Object.keys(LEARN.chapters).filter((c) => steps.some((s) => s.startsWith(c + "/") && !LEARN.steps[s]));
  ok(!part.length, `learn: a chapter with video data has all its steps' data${part.length ? " (not: " + part + ")" : ""}`);
  const noImg = Object.keys(LEARN.steps).filter((k) => !existsSync(join(HERE, "learn/img", k + ".png")));
  ok(!noImg.length, `learn: every step with data has its still${noImg.length ? " (missing: " + noImg + ")" : ""}`);
  const bad = [...page.matchAll(/href="#([^"]*)"/g)].map((m) => m[1]).filter((h) => h && !h.includes("${") && !ids.has(h));   // (not the script's templates)
  ok(!bad.length, `learn: every # link has its target${bad.length ? " (not: " + bad + ")" : ""}`);
  const keys = new Set(Array.from({ length: 27 }, (_, k) => P.keyName(k)));
  const known = new Set([...Object.keys(P.B), ...P.KNOBS, "SHIFT"]);
  const kbd = [...page.matchAll(/<kbd>([^<]*)<\/kbd>/g)].map((m) => m[1].replace("−", "-"));
  const odd = kbd.filter((k) => !known.has(k) && !keys.has(k.replace("#", "♯")));
  ok(kbd.length && !odd.length, `learn: every <kbd> is a control of the panel (${kbd.length}${odd.length ? ", unknown: " + odd : ""})`);
  const vids = [...page.matchAll(/data-video="([^"]*)"/g)].map((m) => m[1]);
  ok(vids.length === sections.filter((s) => !["glossary", "troubleshooting", "reference"].includes(s)).length && vids.every((v) => v === "" || /^[\w-]{11}$/.test(v)),
    "learn: each chapter has data-video, empty or a YouTube id");
  const css = page.slice(page.indexOf("<style>"), page.indexOf("</style>"));
  ok(!/#[0-9a-f]{3,6}\b/i.test(css.replace(/:root[^}]*\}/g, "")), "learn: no colours beyond the :root tokens in the styles");
  const script = page.slice(page.lastIndexOf("<script>") + 8, page.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "learn: page script compiles" + (err ? ` (${err})` : ""));
  const site = readFileSync(join(HERE, "make_site.py"), "utf8");
  ok(/"learn"/.test(site) && /panel\.js/.test(site), "learn: make_site.py ships the page and panel.js");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  if (existsSync(join(TOOLS, "fm1_sample_upload.py"))) {
    execFileSync("python3", [join(TOOLS, "fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
    const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
    ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
  } else console.log("samples: editor == sampleio.py (no FELUCCA_TOOLS)                skip");
  /* recording / trimming: takeSample (a cut of the raw input, faded at the cuts, normalised), autoTrim */
  const R = E.SMP.RATE, raw = new Float64Array(R);       /* 1 s: silence, a tone from 0.25 to 0.6 s, silence */
  for (let i = Math.round(R * 0.25); i < Math.round(R * 0.6); i++) raw[i] = Math.sin(i * 0.2) * 0.3;
  ok(eq(E.takeSample(raw, 0, raw.length), E.normalize(raw)) && eq(E.takeSample(raw, -9, 1e9), E.normalize(raw)),
    "samples: the whole input == normalize (files load as before); the ends are clamped");
  const [a, b] = E.autoTrim(raw), on = Math.round(R * 0.25), off = Math.round(R * 0.6);
  ok(a <= on && on - a <= Math.round(R * 0.005) + 2 && b >= off - 2 && b - off <= Math.round(R * 0.02) + 1,
    `samples: autoTrim finds the sound (${a}..${b} for ${on}..${off}: 5 ms before, 20 ms after)`);
  ok(E.autoTrim(new Float64Array(500)).join() === "0,500", "samples: autoTrim of silence keeps all");
  const cut = E.takeSample(raw, on + 1000, on + 3000);
  let pk = 0;
  for (const v of cut) pk = Math.max(pk, Math.abs(v));
  ok(cut.length === 2000 && cut[0] === 0 && Math.abs(cut[1999]) < 1000 && pk === 30000 &&
     Math.abs(cut[22]) < Math.abs(cut[22 + 63]) + 30000 * 0.6,
    "samples: a cut: its length, faded to 0 at both cut ends, peak normalised");
  /* (#35) a DRUM KIT: each file on its lane's key only, one gain for the kit (the files' levels kept, times lvl) */
  {
    const tone = (amp) => Float64Array.from({ length: 4000 }, (_, i) => Math.sin(i * 0.3) * amp);
    const rows = E.LANES.map(() => null);
    rows[0] = { raw: tone(0.5), a: 0, b: 4000, lvl: 1 };
    rows[3] = { raw: tone(0.5), a: 0, b: 4000, lvl: 0.5 };
    rows[6] = { raw: tone(0.25), a: 0, b: 2000, lvl: 1 };
    const zs = E.kitZones(rows), pk = (s) => s.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    const slot = E.buildSlot("KIT", zs).zones;
    ok(zs.length === 3 && zs.map((z) => [z.root, z.lo, z.hi].join("/")).join() === "36/36/36,42/42/42,37/37/37" &&
       pk(zs[0].s) === 30000 && Math.abs(pk(zs[1].s) - 15000) <= 2 && Math.abs(pk(zs[2].s) - 15000) <= 2 && zs[2].s.length === 2000 &&
       slot.map((z) => `${z.lo}-${z.hi}`).join() === "36-36,37-37,42-42",
      "samples: (#35) a drum kit: each lane's file on its note only, one gain (levels kept, times the lane's level)");
  }

  /* the trimming view: zoom at the pointer, at least 256 samples, inside the input; pan; out to all */
  {
    const n = 22050;
    let v = E.zoomView(n, 0, n, 0.5, 0.5, 0);
    const z1 = v[1] - v[0] === 11025 && v[0] === 5513;                     /* x2 around the middle */
    v = E.zoomView(n, 0, n, 0.1, 0.25, 0);
    const under = Math.abs((v[0] + 0.1 * (v[1] - v[0])) - 0.1 * n) <= 1;    /* the pointer's sample stays */
    let w = [0, n];
    for (let i = 0; i < 60; i++) w = E.zoomView(n, w[0], w[1], 0.97, 0.8, 0);
    const minOk = w[1] - w[0] === 256 && w[1] <= n;
    const p = E.zoomView(n, 1000, 3000, 0.5, 1, 0.1), edge = E.zoomView(n, 21000, 22000, 0.5, 1, 1);
    const out = E.zoomView(n, 1000, 3000, 0.5, 100, 0);
    ok(z1 && under && minOk && p[0] === 1200 && p[1] === 3200 && edge[1] === n && out.join() === `0,${n}`,
      "samples: trim view: zoom at the pointer, 256 samples at least, inside the input, pan, out to all");
  }
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(FW_ROOT, "build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_7\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");   /* Skiff's identities (Felucca's: FM-1_9xx) */
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  /* writeReads: the loader's [addr, len] reads (default: six of 512 B); drops: request counts at which it drops
     off the bus and comes back in update mode 200 ms later (a pulled cable, a power cut, a loader reset) */
  constructor(image, { unplugAfter = Infinity, back = ["FM-1_900", "Felucca"], writeReads = null, drops = [] } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0; this.back = back;
    this.writeReads = writeReads; this.drops = new Set(drops);
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot("FM-1_015", "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...(this.writeReads || Array.from({ length: 6 }, (_, k) => [k * 512, 512])), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (this.drops.delete(this.served)) {
        this.input.state = this.output.state = "disconnected";
        setTimeout(() => this.boot("ota-" + this.back[0], "Felucca Update"), 200);
        return;
      }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_900", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(...this.back), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access, null, { resumeWaitMs: 500 }).resume(image, null, "FM-1_900");
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write, never back -> stops after the wait");
  /* resume() checks the loader's model against the package, as install() checks the running firmware */
  const smkOta = new FakeFM1(image);
  smkOta.boot("ota-SMK-37 Pro_900", "Felucca Update");
  const m1 = await new Updater(smkOta.access).resume(image, null, "FM-1_900").then(() => null, (x) => x);
  ok(m1 && m1.code === "model" && smkOta.served === 0, "fm1ota.js: an SMK-37 Pro in update mode is never given an FM-1 package ('model')");
  const fm1Ota = new FakeFM1(image);
  fm1Ota.boot("ota-FM-1_900", "Felucca Update");
  const m3 = await new Updater(fm1Ota.access).resume(image, null, "SMK-37 Pro_900").then(() => null, (x) => x);
  ok(m3 && m3.code === "model" && fm1Ota.served === 0, "fm1ota.js: an FM-1 in update mode is never given an SMK package ('model')");
  const noPkg = new FakeFM1(image);
  noPkg.boot("ota-FM-1_900", "Felucca Update");
  const m2 = await new Updater(noPkg.access).resume(image).then(() => null, (x) => x);
  ok(m2 && m2.code === "model" && noPkg.served === 0, "fm1ota.js: resume without the package's identity writes nothing");
  const elite = new FakeFM1(image);
  elite.boot("ota-SMK-37 Elite_900", "Felucca Update");
  const m4 = await new Updater(elite.access).resume(image, null, "SMK-37 Pro_900").then(() => null, (x) => x);
  ok(m4 && m4.code === "model" && elite.served === 0, "fm1ota.js: an SMK-37 Elite in update mode is never given an SMK-37 Pro package ('model')");
  const smkPro = new FakeFM1(image, { back: ["SMK-37 Pro_900", "Felucca"] });
  smkPro.boot("ota-SMK-37 Pro_900", "Felucca Update");
  const r3 = await new Updater(smkPro.access).resume(image, null, "SMK-37 Pro_900");
  ok(r3 === true && smkPro.served > 0 && smkPro.bad === 0, "fm1ota.js: resume: an SMK-37 Pro in update mode takes an SMK-37 Pro package");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  /* (launch hardening) a drop during the write waits for the same model's loader and resumes, up to resumeMax */
  const flaky = new FakeFM1(image, { drops: [2, 5] });
  flaky.boot("ota-FM-1_900", "Felucca Update");
  const fsteps = [];
  const fr = await new Updater(flaky.access, null, { resumeWaitMs: 3000 }).resume(image, (k, a) => fsteps.push(k === "resume" ? `resume ${a}` : k), "FM-1_900");
  ok(fr === true && flaky.bad === 0 && fsteps.filter((k) => k === "dropped").length === 2 && fsteps.includes("resume 3") && fsteps.at(-1) === "done",
    "fm1ota.js: dropped twice during the write -> waits, resumes, finishes");
  const always = new FakeFM1(image, { drops: Array.from({ length: 40 }, (_, i) => i + 1) });
  always.boot("ota-FM-1_900", "Felucca Update");
  const asteps = [];
  const ar = await new Updater(always.access, null, { resumeWaitMs: 3000, resumeMax: 5 }).resume(image, (k) => asteps.push(k), "FM-1_900");
  ok(ar === false && asteps.filter((k) => k === "dropped").length === 4, "fm1ota.js: dropping on every try -> gives up after 5 tries");
  /* an FM-1 and an SMK-37 Pro on one computer, the SMK listed first and running: an FM-1 package goes to the FM-1
     and its return is the FM-1's, not the SMK's identity */
  const twoSmk = new FakeFM1(image), twoFm1 = new FakeFM1(image);
  twoSmk.boot("SMK-37 Pro_016", "SMK-37 Pro");
  const both = { get inputs() { return new Map([...twoSmk.access.inputs, ...twoFm1.access.inputs]); },
                 get outputs() { return new Map([...twoSmk.access.outputs, ...twoFm1.access.outputs]); } };
  const tw = await new Updater(both).install(image, "FM-1_900").then((v) => v, (x) => x);
  ok(tw === "FM-1_900" && twoFm1.bad === 0 && twoFm1.served > 0 && twoSmk.served === 0,
    "fm1ota.js: two units, the other one listed first: the package's own unit and its return");
  const flaky2 = new FakeFM1(image, { drops: [9] });
  const ir = await new Updater(flaky2.access, null, { resumeWaitMs: 3000 }).install(image, "FM-1_900");
  ok(ir === "FM-1_900" && flaky2.bad === 0, "fm1ota.js: install() resumes a dropped write too");

  /* the write's progress moves forward only: Skiff's loader (check pass, write pass, header sector last) and
     M-VAVE's (one pass, top down), counted in flash.bin bytes (a synthetic image with a file list) */
  const encIt = (b, off, size) => { let key = 0xFFFF; for (let i = 0; i < size; i++) { b[off + i] ^= key & 0xFF; key = ((key * 2) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } };
  const big = Uint8Array.from({ length: 0x20000 }, (_, i) => (i * 13) & 0xFF);
  const hdr = new Uint8Array(0x40), ent = new Uint8Array(0x50), lo = 0x400, size = 0x20000 - 0x400;
  hdr[8] = 1; ent[8] = lo & 0xFF; ent[9] = lo >> 8; ent[12] = size & 0xFF; ent[13] = (size >> 8) & 0xFF; ent[14] = size >> 16;
  encIt(hdr, 0, 0x40); encIt(ent, 0, 0x50); big.set(hdr, 0); big.set(ent, 0x40);
  const span = flashSpan(big);
  ok(span && span.off === lo && span.size === size, "fm1pkg.js: flashSpan finds flash.bin in the file list");
  const range = (a, b, s) => { const r = []; for (let x = a; s > 0 ? x < b : x > b; x += s) r.push(x); return r; };
  const ours = [[lo + 0x4000, 32], ...range(0, size, 512).map((o) => [lo + o, 512]),
    ...range(0x5000, size, 512).map((o) => [lo + o, 512]), ...range(0x4000, 0x5000, 512).map((o) => [lo + o, 512])];
  const mvave = [[0, 64], ...range(size - 512, -1, -512).map((o) => [lo + o, 512])];
  for (const [name, reads] of [["Skiff's loader: check pass, write pass, header last", ours], ["M-VAVE's loader: one pass, top down", mvave]]) {
    const m = new WriteMeter(big), v = reads.map(([a, n]) => m.add(a, n));
    ok(v.every((x, i) => i === 0 || x >= v[i - 1]) && v.at(-1) >= 90 && v.at(-1) <= 99, `fm1ota.js: progress forward only, to ${v.at(-1)} %: ${name}`);
  }
  const pdev = new FakeFM1(big, { writeReads: ours });
  pdev.boot("ota-FM-1_900", "Felucca Update");
  const pv = [];
  await new Updater(pdev.access).resume(big, (k, a) => { if (k === "write") pv.push(a); }, "FM-1_900");
  ok(pv.length > 400 && pv.every((x, i) => i === 0 || x >= pv[i - 1]) && pdev.bad === 0, "fm1ota.js: the write's step('write', %) moves forward only");

  /* the site's packages: one per model; a stock SMK older than V016 is sent to M-VAVE's updater first */
  const pk = [{ product: "FM-1_714", pkg: "a" }, { product: "SMK-37 Pro_714", pkg: "b" }, { product: "SMK-37 Elite_714", pkg: "c" }, { product: "MKE-P37_714", pkg: "d" }];
  ok(packageFor(pk, "FM-1").pkg === "a" && packageFor(pk, "ota-SMK-37 Pro").pkg === "b" && packageFor(pk, "SMK-37 Elite").pkg === "c"
     && packageFor(pk, "MKE-P37").pkg === "d" && packageFor(pk, "STARRYKEY-37 PLAY") === null && packageFor(pk, "SMK-37") === null,
    "installer: the package for the connected model, never another model's");
  const idOf = (txt) => { const m = /^([^_]+)_(\d+)$/.exec(txt); return { text: txt, model: m[1], version: parseInt(m[2], 10) }; };
  ok(needsStockUpdate(idOf("SMK-37 Pro_012")) && needsStockUpdate(idOf("SMK-37 Elite_015")) && needsStockUpdate(idOf("MKE-P37_013"))
     && !needsStockUpdate(idOf("SMK-37 Pro_016")) && !needsStockUpdate(idOf("SMK-37 Pro_714")) && !needsStockUpdate(idOf("SMK-37 Pro_900"))
     && !needsStockUpdate(idOf("SMK-37 Pro_000")) && !needsStockUpdate(idOf("FM-1_015")), "installer: a stock SMK below V016 is updated with M-VAVE's tool first");
  const v016 = process.env.SMK_STOCK_DIR && join(process.env.SMK_STOCK_DIR, "SMK-37_Pro_016.fwsc");
  if (v016 && existsSync(v016)) {
    const st = await validateStockPackage(new Uint8Array(readFileSync(v016)));
    ok(st.product === "SMK-37 Pro_016" && st.image.length > 0x9D000, "fm1pkg.js: the official SMK-37 Pro V016 is accepted for the return to stock");
  } else console.log("official SMK-37 Pro V016: skipped (SMK_STOCK_DIR not set)");
  const notStock = await validateStockPackage(new Uint8Array(705252)).then(() => null, (x) => x);
  ok(notStock && /official firmware/.test(notStock.message), "fm1pkg.js: a file of V016's size that is not V016 is refused");
  /* (2.0.2, #64) the official FM-1 V15 and V16 (FM1_STOCK_DIR: FM-1.fwsc, FM-1_016.fwsc), each accepted as itself */
  for (const [f, product, fl] of [["FM-1.fwsc", "FM-1_015", 0x93000], ["FM-1_016.fwsc", "FM-1_016", 0xA9000]]) {
    const p = process.env.FM1_STOCK_DIR && join(process.env.FM1_STOCK_DIR, f);
    if (p && existsSync(p)) {
      const st = await validateStockPackage(new Uint8Array(readFileSync(p)));
      ok(st.product === product && st.image.length > fl, `fm1pkg.js: the official ${product} is accepted for the return to stock`);
    } else console.log(`official ${product}: skipped (FM1_STOCK_DIR not set)`);
  }
  const notV16 = await validateStockPackage(new Uint8Array(718196)).then(() => null, (x) => x);
  ok(notV16 && /official firmware/.test(notV16.message), "fm1pkg.js: a file of V16's size that is not V16 is refused");
  const stopped = (served, code = "stopped") => Object.assign(new Error(code), { code, served });
  ok(stockRefusal("FM-1_015", stopped(4)) === "stockNeedsV16" && stockRefusal("FM-1_016", stopped(4)) === "stockNeedsV15"
     && stockRefusal("FM-1_016", stopped(35)) === "stockNeedsV15" && stockRefusal("FM-1_016", stopped(2)) === "stockSkiffOld"
     && stockRefusal("FM-1_015", stopped(3)) === null && stockRefusal("FM-1_015", stopped(36)) === null
     && stockRefusal("SMK-37 Pro_016", stopped(4)) === null && stockRefusal("FM-1_015", stopped(4, "lost")) === null,
    "installer: where a running Skiff stopped reading an official FM-1 file names the version the unit needs, or a Skiff too old for V16");
}

await editorMock();
await editorSamplePresets();
mockTables();
await editorLibrarian();
await editorGrid();
await editorRatchet();
await editorStepExtras();
await editorStepTies();
await editorKit();
await editorTransportNames();
await editorLists();
await editorSong2();
await editorLocks();
await editorLight();
await editorLive();
await preferenceReplies();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorSong();
await editorSessions();
await editorFm6();
fm6TabNoBank();
await editorFm4();
editorTabs();
editorIcons();
learnPage();
samplesMatch();
await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
