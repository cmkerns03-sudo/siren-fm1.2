// SPDX-License-Identifier: GPL-3.0-only
// ABYSS web: the brain the site runs (engines/abyss-brain.wasm) gives the FM-1's events, note for note: the same dump
// as the firmware's host test (tests/abyss/brain_test.c ... dump), compared by tools/abyss/identity.sh.
//   node brain_identity.mjs WASM WORLD SEED PIECES
import fs from 'node:fs';
const [wasm, w0, code, pieces] = [process.argv[2], +process.argv[3], process.argv[4], +process.argv[5]];
const b = new WebAssembly.Instance(await WebAssembly.compile(fs.readFileSync(wasm)), {}).exports;
const LN = ['DRUMS', 'DRONE', 'PAD', 'LOW', 'MELO', 'BELL', 'SWELL', 'SUB'];
const B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
let seed = 0; for (const ch of code.toUpperCase()) { const v = B32.indexOf(ch); if (v >= 0) seed = (seed * 32 + v) >>> 0; }
for (let w = 0; w < 3; w++) { for (let L = 0; L < 8; L++) b.w_pal(w, L, 4); b.w_pal(w, 0, 2); }
b.w_init(); b.w_seed(seed, w0);
const p0 = b.w_b(2), out = [];
let steps = 0;
while (b.w_b(2) < p0 + pieces && steps < 16 * 4000) {
  b.w_step(); steps++;
  const n = b.w_n();
  for (let i = 0; i < n; i++) { const x = b.w_note(i); out.push(`${b.w_ev(10)}.${b.w_ev(11)} ${LN[x >> 16]} ${(x >> 8) & 255} ${x & 255} ${b.w_len(i)}`); }
}
process.stdout.write(out.join('\n') + '\n');
