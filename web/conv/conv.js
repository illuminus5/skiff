// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// (Skiff 2.0, Tier B: design/TIER_B.md 10) The converter in the page: an archive of before (web/fm1backup.js) into
// what 2.0 reads, by conv.wasm (web/conv/conv.c: the firmware's own code over a simulated flash).
//   const conv = await loadConverter(wasmBytesOrUrl);
//   const { archive, report, writeBack } = conv.convert(readBackup(file));
// archive: a complete 2.0 archive (29 objects); writeBack: the ids whose bytes changed, which 2.0 couldn't read as they
// were or the conversion filled (pattern chunks, the user presets' patches): the installer writes back only those;
// report: a line per object (what it was, what it is now, kept / converted / refused; note: what had no place in 2.0,
// felucca.c conv_note). Refused: damaged (the unit refuses it too: a restore leaves it out); "kept as it was: the
// patterns are full": an old project no free pattern could take, which a 2.0 unit keeps whole (OLD FORMAT) for later. An archive without the pattern bank is Felucca's (or Skiff's before it): its user presets of
// Felucca 1.2+ are mapped by name (conv_source).
import { BACKUP_IDS, bkCrc } from "../fm1backup.js";

const CONV_MAGICS = { 0x534B4634: "SKF4", 0x534B4633: "SKF3", 0x534B4632: "SKF2", 0x534B4631: "SKF1", 0x46554E41: "FUNA" };
for (let v = 1; v <= 9; v++) CONV_MAGICS[0x46554E30 + v] = `FUN${v}`;
const convU32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

/* what an object holds, in a word or two (the report) */
export function convKind(id, b) {
  if (!b || !b.length) return "empty";
  if (id >= 32) return "samples";
  if (id >= 16) return "patterns";
  if (id === 0 || id <= 5 && id >= 2) return CONV_MAGICS[convU32(b, 0)] || "unknown";
  if (id === 1) return { 0x534B5331: "SKS1", 0x50455234: "PER4" }[convU32(b, 0)] || "unknown";
  if (id === 6 || id === 7) {
    const v = {};
    for (let i = 0; i < 16; i++) if (b[8 + i * 192] === 0xA5) { const k = `v${b[9 + i * 192]}`; v[k] = (v[k] || 0) + 1; }
    return Object.keys(v).sort().map((k) => `${v[k]} ${k}`).join(", ") || "none used";
  }
  if (id === 8) return "FM6 bank (1.0-1.0.2)";
  return id === 9 ? "FM6 patches" : "unknown";
}
const sameBytes = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const convBase64 = (bytes) => { let s = ""; for (const x of bytes) s += String.fromCharCode(x); return btoa(s); };

export async function loadConverter(src) {
  const bytes = typeof src === "string" ? new Uint8Array(await (await fetch(src)).arrayBuffer()) : src;
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const x = instance.exports;
  x._initialize?.();
  const buf = () => new Uint8Array(x.memory.buffer, x.conv_buf(), 81920);
  return {
    convert(archive) {
      const input = new Map(archive.objects.map((o) => [o.id, o]));
      const bytesOf = (id) => input.get(id)?.bytes || new Uint8Array(0);
      const rc = new Map();
      x.conv_reset();
      x.conv_source(archive.objects.length < BACKUP_IDS.length ? 1 : 0);
      for (const id of [...archive.objects.slice(2).map((o) => o.id), 1, 0]) {   // (restoreBackup's order: the music last)
        const b = bytesOf(id);
        if (id >= 32 || !b.length) continue;
        buf().set(b);
        rc.set(id, x.conv_put(id, b.length));
      }
      x.conv_fix();
      const objects = [], report = [], writeBack = [];
      for (const id of BACKUP_IDS) {
        const was = bytesOf(id);
        let now = was, what = "kept";
        if (id < 32) {
          const conv = buf().slice(0, x.conv_get(id));
          const left = !!x.conv_needs(id, conv.length);   // (still a format of before: no pattern was free for its steps)
          buf().set(was);
          const needs = !!x.conv_needs(id, was.length), r = rc.get(id);
          if (needs && (r === 4 || (r === 0 && left))) what = "kept as it was: the patterns are full";
          else if (needs && r !== 0) what = "refused, kept as it was";
          else if (needs || (id === 8 || id === 9 || id >= 16) && !sameBytes(conv, was)) { now = conv; what = was.length ? "converted" : "added"; }
        }
        if (now !== was) writeBack.push(id);
        const n = x.conv_note(id), note = n ? String.fromCharCode(...buf().subarray(0, n)) : "";
        report.push({ id, was: convKind(id, was), now: convKind(id, now), what, ...(note ? { note } : {}) });
        const kept = now === was && input.get(id);
        objects.push(kept ? { id, size: kept.size, crc: kept.crc, data: kept.data, bytes: was }
                          : { id, size: now.length, crc: bkCrc(now), data: convBase64(now), bytes: now });
      }
      return {
        archive: { format: "felucca-backup", version: 1, firmware: archive.firmware, created: archive.created, objects },
        report, writeBack,
      };
    },
  };
}
