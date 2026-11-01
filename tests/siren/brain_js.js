const B = require(__dirname + '/brain.js'); const fs = require('fs');
const kits = JSON.parse(fs.readFileSync(__dirname + '/../../tools/siren/kits.json')); const pal = JSON.parse(fs.readFileSync(__dirname + '/pal.json'));
const STY = ['TECHNO', 'TRANCE', 'PSY', 'ACID', 'RAVE', 'HARD', 'DNB', 'HOUSE'], LN = ['DRUMS', 'BASS', 'ARP', 'HOOK', 'PAD', 'STAB', 'FX', 'SUB'];
const CUR = {}; for (const s of STY) { CUR[s] = {}; for (const L of LN) CUR[s][L] = Array.from({ length: pal.n[s][L] }, (_, i) => ({ i })); }
const [style, seed, steps, lock] = process.argv.slice(2).map(Number);
const b = new B.Brain(); b.setPalette({ CUR, DKITS: kits }); b.styleLock = !!lock; b.setSeed(seed >>> 0, STY[style]);
const KN = ['energy', 'density', 'tension', 'mutation'], SECS = ['INTRO', 'GROOVE', 'BUILD', 'DROP', 'BREAK', 'PEAK', 'OUTRO'];
const out = [];
for (let i = 0; i < steps; i++) {
  if (i % 5003 === 777) b.dive = i % 2 ? 4 : 16;
  if (i % 7919 === 1234) b.breakReq = true;
  if (i % 3001 === 2000) { b.knobs[KN[Math.floor(i / 3001) % 4]] = (i * 37) % 1001; if (KN[Math.floor(i / 3001) % 4] === 'density') b.motifs(); }
  if (i % 9001 === 4500) b.reroll(LN[1 + Math.floor(i / 9001) % 6]);
  if (i % 6007 === 3000) b.lock(LN[Math.floor(i / 6007) % 7], Math.floor(i / 6007) % 2);
  if (i % 4001 === 1500) b.dropReq = true;                                     // (0.5) the black keys
  if (i % 8009 === 2500) b.stay = !b.stay;
  if (i % 6997 === 3500) b.keyReq = true;
  if (i % 2003 === 1000) b.remotif(LN[Math.floor(i / 2003) % 7], Math.floor(i / 2003) % 3 === 0 ? -1 : 1);
  if (i % 11003 === 9000) b.nextTrack();
  if (i >= 12000 && i < 16000 && i % 53 === 7) { if (i % 2) b.breakReq = true; else b.dropReq = true; }   // (0.5) the plan full: the hands again and again
  const ev = b.step16();
  let l = `${i} ${ev.info.sec} ${STY.indexOf(b.style)} e${ev.info.e} b${ev.info.bpm} k${ev.info.key} c${ev.info.chord}`;
  for (const [L, n, v, len] of ev.notes) l += ` ${L}:${n}:${v}:${len}`;
  const cuts = {}; let wob = '';
  for (const [L, w, v] of ev.params) { if (w === 'cut') cuts[L] = v; else wob = ` w${v.rate3}/${v.shape}/${v.depth}/${v.growl}`; }
  for (const L of LN) if (cuts[L] != null) l += ` c${L}=${cuts[L]}`;
  l += wob + ` m${ev.master.hp}/${ev.master.duck}/${ev.master.gain}/${ev.master.verb}`;
  if (b.soundsChanged) { l += ' S' + LN.slice(0, 7).map((L) => (b.soundIdx[L] ?? -1) + ',').join('') + `k${b.sounds.DRUMS.kickN16},${b.sounds.DRUMS.kick2N16}`; b.soundsChanged = false; }
  out.push(l);
}
fs.writeSync(1, out.join('\n') + '\n');
