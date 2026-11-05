// test_wasm.mjs - the project page's module (docs/assets/c12panel.wasm), under node, called as the
// page calls it: the real panel and engine with the simulator's Plinky API, compiled to WebAssembly.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
import { readFileSync } from 'node:fs';

let run = 0, failed = 0;
const check = (ok, msg) => { run++; if (!ok) { failed++; console.log('FAIL ' + msg); } };

const bytes = readFileSync(new URL('../docs/assets/c12panel.wasm', import.meta.url));
const mod = await WebAssembly.compile(bytes);
check(WebAssembly.Module.imports(mod).length === 0, 'the module imports nothing');
const x = (await WebAssembly.instantiate(mod, {})).exports;
x.__wasm_call_ctors();
check(x.c12p_init() === 0, 'the engine comes up');

const mem = () => x.memory.buffer;
const str = (p) => { const b = new Uint8Array(mem(), p); let n = 0; while (b[n]) n++; return new TextDecoder().decode(b.subarray(0, n)); };
const leds = () => new Uint32Array(mem(), x.c12p_leds(), 256).slice();
const bright = (c) => ((c >>> 16) & 255) + ((c >>> 24) & 255) + ((c >>> 8) & 255);
let peak = 0, frames = 0;
const frame = (n = 1) => {
  for (let i = 0; i < n; i++) {
    x.c12p_frame();
    const pcm = new Int16Array(mem(), x.c12p_pcm(), 256);
    for (const s of pcm) peak = Math.max(peak, Math.abs(s));
    frames++;
  }
};
const tap = (px, py, p = 120, ms = 40) => { x.c12p_touch(px, py, p); frame(ms / 4); x.c12p_touch(px, py, 0); frame(3); };

frame(10);
let L = leds();
check(bright(L[0]) > 0 && bright(L[1]) <= 3, 'the default beat is lit (kick on step 1)');
check(L[15 * 16 + 0] === (15 << 24 | 15 << 16 | 15 << 8), 'SEQ is the lit page pad');

// play: audio
peak = 0;
tap(15, 15);
check(x.c12p_playing() === 1, 'PLAY');
frame(500);
check(peak > 3000, `audible (peak ${peak})`);
tap(14, 15);

// a step, a variation through the held step
tap(3, 0, 200);
L = leds();
check(bright(L[3]) > 30, 'a tapped step lights');
x.c12p_touch(3, 0, 120); frame(90); tap(1, 8); x.c12p_touch(3, 0, 0); frame(3);
check(bright(leds()[3]) > 30, 'the held, edited step stays on');

// every page: reachable from row 15, and every pad lit on it has a description
const pages = ['SEQ', 'SOUND', 'GEN', 'MIX', 'FX', 'PERFORM', 'REPEAT'];
const described = (label) => {
  const L = leds();
  const missing = [];
  for (let i = 0; i < 256; i++) {
    if (bright(L[i]) === 0) continue;
    const t = str(x.c12p_help(i % 16, (i / 16) | 0));
    if (!t || t.split('\n').length < 3) missing.push(`${i % 16},${(i / 16) | 0}`);
  }
  check(missing.length === 0, `${label}: every lit pad is described (missing: ${missing.slice(0, 8).join(' ')})`);
};
for (let p = 0; p < pages.length; p++) {
  tap(p, 15);
  frame(2);
  check(x.c12p_page() === p, `row 15 pad ${p} opens ${pages[p]}`);
  described(pages[p]);
}
tap(7, 15);
frame(2);
check(x.c12p_page() === 7, 'row 15 pad 7 opens SAVE');
x.c12p_set_page(4);
frame(2);
tap(9, 0); // FX: the COLOUR bank
frame(2);
described('FX COLOUR');
tap(8, 0);
// the tempo overlay, held
x.c12p_set_page(0);
frame(2);
x.c12p_touch(8, 15, 150); frame(100);
check(bright(leds()[1 * 16 + 2]) > 0, 'holding TEMPO shows the overlay');
described('TEMPO overlay');
x.c12p_touch(8, 15, 0); frame(3);
// tap tempo: four taps 400 ms apart
for (let i = 0; i < 4; i++) { x.c12p_touch(8, 15, 150); frame(10); x.c12p_touch(8, 15, 0); frame(90); }
check(x.c12p_tempo() === 150, `tap tempo (${x.c12p_tempo()})`);
x.c12p_set_tempo(120);
tap(0, 15);

// save, change, load
const n = x.c12p_save();
const saved = str(x.c12p_text());
check(n > 2000 && saved.startsWith('{"clatter12":"'), `a save (${n} bytes)`);
const before = leds();
tap(9, 15); // GEN (SEQ's row 15)
frame(4);
const after = leds();
check(after.some((c, i) => c !== before[i]), 'GEN changes the grid');
new Uint8Array(mem(), x.c12p_text(), saved.length + 1).set(new TextEncoder().encode(saved + '\0'));
check(x.c12p_load(saved.length) === 0, 'the save loads');
frame(4);
const back = leds();
let same = 0;
for (let i = 0; i < 7 * 16; i++) same += (back[i] === before[i]) ? 1 : 0;
check(same >= 7 * 16 - 2, `the steps come back (${same} of 112 pads)`);
new Uint8Array(mem(), x.c12p_text(), 20).set(new TextEncoder().encode('{"x":1}\0'));
check(x.c12p_load(7) === 1, 'a foreign file is refused');

// tempo, as Plinky's left side buttons set it
x.c12p_set_tempo(133);
frame(2);
check(x.c12p_tempo() === 133, 'tempo');

console.log(`wasm: ${frames} frames (${(frames * 4 / 1000).toFixed(1)} s); ${run} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
