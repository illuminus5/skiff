// SPDX-License-Identifier: GPL-3.0-only
// (Skiff Studio) the live screen's copy (web/emu/cast_follow.c, build/cast/felucca_cast.wasm) on a recording of an
// FM-1's cast (DIR/backup.bin: the editor's backup; DIR/cast.bin: the records; DIR/info.json), fed as the
// editor's Screen tab feeds it: the backup into one instance's flash, a fresh instance from that flash, then the
// records 1 to 30 at a time with a cf_run after each lot, and the requests the recorder sent after the start (its
// PINGs, info.json) each before the records that came after it, which the copy runs at the unit's R. Prints what the
// copy found; exit 1 if any check of the music and UI was unlike the unit's.
//   node web/emu/cast_test.mjs build/cast/felucca_cast.wasm DIR
import { existsSync, readFileSync } from "node:fs";

const [wasmPath, dir] = process.argv.slice(2);
if (!wasmPath || !dir) { console.error("usage: node cast_test.mjs felucca_cast.wasm DIR"); process.exit(2); }
const wasm = readFileSync(wasmPath);
async function instance() {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  instance.exports._initialize();
  return instance.exports;
}

const a = await instance();                        // the backup into an empty unit's flash
a.web_nor_erase();
a.web_boot();
const bk = readFileSync(`${dir}/backup.bin`);      // per object: id u8, size u32, its bytes
for (let p = 0; p < bk.length;) {
  const id = bk[p], n = bk.readUInt32LE(p + 1);
  p += 5;
  new Uint8Array(a.memory.buffer, a.web_obj_buf(), n).set(bk.subarray(p, p + n));
  p += n;
  const rc = a.web_restore_object(id, n);
  if (rc !== 0) throw new Error(`object ${id} not written (${rc})`);
}
const nor = new Uint8Array(a.memory.buffer, a.web_nor(), a.web_nor_size()).slice();

const c = await instance();                        // a fresh one from that flash, then the records
c.web_nor_erase();
new Uint8Array(c.memory.buffer, c.web_nor(), c.web_nor_size()).set(nor);
const rec = readFileSync(`${dir}/cast.bin`);       // per record <d B 4I>: the PC's time, its number, the 4 words
const N = Math.floor(rec.length / 25), t0 = performance.now();
const sent = existsSync(`${dir}/info.json`)        // (the PINGs: cmd 25, no arguments, at the PC's time t)
  ? JSON.parse(readFileSync(`${dir}/info.json`, "utf8")).requests.filter((q) => q.cmd === 25 && q.t > 0) : [];
let si = 0;
for (let k = 0, lot = 0; k < N; k += lot) {
  lot = Math.min(N - k, 1 + (k * 7919) % 30);
  const last = rec.readDoubleLE((k + lot - 1) * 25);
  for (; si < sent.length && sent[si].t <= last; si++) {
    const f = [0xF0, 0x7D, 0x46, 0x4C, sent[si].cmd, 0xF7];
    new Uint8Array(c.memory.buffer, c.cf_req_buf(), f.length).set(f);
    if (c.cf_req_push(f.length) !== 0) throw new Error("the copy's request queue is full");
  }
  const buf = new Uint32Array(c.memory.buffer, c.cf_buf(), 256);
  for (let i = 0; i < lot; i++)
    for (let w = 0; w < 4; w++) buf[4 * i + w] = rec.readUInt32LE((k + i) * 25 + 9 + 4 * w);
  if (c.cf_push(lot) !== lot) throw new Error("the copy's queue is full");
  c.cf_run();
}
const s = (k) => c.cf_stat(k), fr = s(1);
console.log(`${N} records, ${(rec.readDoubleLE((N - 1) * 25) || 0).toFixed(1)} s of the unit, replayed in ` +
            `${((performance.now() - t0) / 1000).toFixed(1)} s; second core ${s(9)}`);
console.log(`music + UI checks: ${s(3) - s(4)} of ${s(3)} alike; frames: ${fr - s(2)} of ${fr} alike ` +
            `(${fr ? (100 * (fr - s(2)) / fr).toFixed(1) : "-"} %); waiting at the end ${s(5)}; ISR records late ${s(6)}`);
console.log(`requests: ${s(8)} handled by the unit (R), ${sent.length} sent; replayed ${s(8) - s(11)}, ` +
            `not in the log ${s(11)}, never reached the unit ${s(10)}`);
process.exit(s(4) || !s(3) ? 1 : 0);
