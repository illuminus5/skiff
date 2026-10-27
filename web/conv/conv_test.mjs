// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// (Skiff 2.0, Tier B: design/TIER_B.md 10) The converter in the page (web/conv/conv.js, conv.wasm) on the archives
// tests/conv_test.c wrote: a 1.x archive needs nothing; Felucca's (13 objects, old projects and presets; Felucca 1.6's
// FUNA projects and presets of 1.2+) are converted, each object written back the same bytes as the native build made,
// the result a complete 2.0 archive.
//   node web/conv/conv_test.mjs build/conv/conv.wasm DIR     (DIR: conv_test's conv_1x.json, conv_old*, conv_funa*)
import { readFileSync } from "node:fs";
import { readBackup } from "../fm1backup.js";
import { loadConverter } from "./conv.js";

let bad = 0;
const check = (what, ok) => { console.log(`conv.wasm: ${what.padEnd(84)} ${ok ? "ok" : "FAIL"}`); if (!ok) bad++; };
const [wasmPath, dir] = process.argv.slice(2);
const conv = await loadConverter(new Uint8Array(readFileSync(wasmPath)));
const load = (name) => readBackup(readFileSync(`${dir}/${name}`, "utf8"));
const byId = (archive) => new Map(archive.objects.map((o) => [o.id, o]));

{
  const a = load("conv_1x.json"), r = conv.convert(a);
  check("a 1.x archive: nothing to write back, every object as it was",
        !r.writeBack.length && r.archive.objects.length === 29 && r.archive.objects.every((o, i) => o.data === a.objects[i].data) &&
        r.report.every((x) => x.what === "kept"));
}
{
  const a = load("conv_full.json"), r = conv.convert(a), line = r.report.find((x) => x.id === 2);
  check("(2.0) an old project no free pattern can take: kept as it was (the patterns full), not written back",
        line.was === "SKF1" && line.what === "kept as it was: the patterns are full" && !r.writeBack.includes(2) &&
        byId(r.archive).get(2).data === byId(a).get(2).data);
}
{
  const a = load("conv_old.json"), native = byId(load("conv_old_native.json")), r = conv.convert(a);
  const out = byId(r.archive), inp = byId(a), line = (id) => r.report.find((x) => x.id === id);
  check("Felucca's archive (13 objects): written back the music, slots 1-3, the presets, their patches, patterns",
        [0, 2, 3, 4, 6, 9].every((id) => r.writeBack.includes(id)) && r.writeBack.some((id) => id >= 16 && id < 32) &&
        !r.writeBack.includes(1) && !r.writeBack.includes(5) && !r.writeBack.some((id) => id >= 32));
  check("  each object written back = the native converter's, byte for byte",
        r.writeBack.every((id) => out.get(id).data === native.get(id).data));
  check("  the damaged project refused and kept as it was; Felucca's settings (PER4) kept",
        line(5).what.startsWith("refused") && out.get(5).data === inp.get(5).data && out.get(1).data === inp.get(1).data);
  check("  the report: FUN9 -> SKF4, SKF1 -> SKF4, SKF2 -> SKF4, presets v2 v3 v4 v5 -> v6 (one v5 kept)",
        line(2).was === "FUN9" && line(2).now === "SKF4" && line(3).was === "SKF1" && line(4).was === "SKF2" &&
        line(4).now === "SKF4" && /v6/.test(line(6).now) && /1 v5/.test(line(6).now) && line(16).what === "added");
  let ok;
  try { ok = readBackup(JSON.stringify(r.archive)).objects.length === 29; } catch (e) { ok = false; }
  check("  the result a complete 2.0 archive (fm1backup.js readBackup takes it)", ok);
  const again = conv.convert(r.archive);
  check("  converted again: nothing more to write back", !again.writeBack.length);
}
{
  const a = load("conv_funa.json"), native = byId(load("conv_funa_native.json")), r = conv.convert(a);
  const out = byId(r.archive), line = (id) => r.report.find((x) => x.id === id);
  check("Felucca 1.6's archive: written back the music, slots 1 and 2 (FUNA), the presets, the patch, patterns",
        [0, 2, 3, 6, 9].every((id) => r.writeBack.includes(id)) && r.writeBack.some((id) => id >= 16 && id < 32) &&
        !r.writeBack.includes(1) && !r.writeBack.includes(4));
  check("  each object written back = the native converter's, byte for byte",
        r.writeBack.every((id) => out.get(id).data === native.get(id).data));
  check("  the report: FUNA -> SKF4, presets 4 v4 1 v5 -> 5 v6; noted: filter TYPE, NUDGE, MIDI, categories",
        line(0).was === "FUNA" && line(0).now === "SKF4" && line(3).was === "FUNA" && line(3).now === "SKF4" &&
        line(6).was === "4 v4, 1 v5" && line(6).now === "5 v6" && /T1 ANALOG filter TYPE/.test(line(0).note) &&
        /T1 T2 NUDGE/.test(line(0).note) && /MIDI IN TRACK/.test(line(0).note) &&
        /U04 ANALOG filter TYPE/.test(line(6).note) && /categories: 2/.test(line(6).note) && !line(3).note &&
        !line(1).note);
  const again = conv.convert(r.archive);
  check("  converted again: nothing more to write back", !again.writeBack.length && again.report.every((x) => !x.note));
}
console.log(bad ? "CONVERTER (WASM) TEST FAILED" : "converter (wasm) test passed");
process.exit(bad ? 1 : 0);
