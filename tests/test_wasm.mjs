// test_wasm.mjs - the browser build of the engine (docs/assets/c12.wasm) against the native one.
// Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only
//
// Run by `make test-wasm` under node. It calls the module exactly as the project page does (the same
// exports, in the same order) and checks, for the module that visitors will run:
//   - it instantiates with no imports, and its style and kit names are the ones the page shows;
//   - the generator and mutator reproduce, byte for byte, the native ones (build/patterns.bin, from
//     tests/export_patterns.c): 99 patterns;
//   - the audio of the scenario in tests/scenario.inc (generated pattern, a designed kit loaded
//     mid-render, a mutation, 5 s at 32 kHz) matches the native C build's render (build/test/equiv_c.raw)
//     to better than 60 dB signal/difference. The two use different libm (CLATTER's shim in the module,
//     the platform's natively), so they cannot be bit-identical;
//   - the sequencer reports a moving position while playing, 255 when stopped;
//   - it also runs at 48 kHz (a browser that will not give an audio context at 32 kHz).
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
let run = 0, failed = 0;
const check = (ok, msg) => { run++; if (!ok) { failed++; console.log('FAIL: ' + msg); } };

const bytes = readFileSync(join(root, 'docs/assets/c12.wasm'));
const mod = await WebAssembly.compile(bytes);
check(WebAssembly.Module.imports(mod).length === 0, 'the module needs no imports: ' + JSON.stringify(WebAssembly.Module.imports(mod)));
const inst = async () => (await WebAssembly.instantiate(mod, {})).exports;
const cstr = (x, p) => { const m = new Uint8Array(x.memory.buffer); let e = p; while (m[e]) e++; return new TextDecoder().decode(m.subarray(p, e)); };
const io = x => new Uint32Array(x.memory.buffer, x.c12w_io(), 448);

// ---- names: the page's defaults (docs/assets/site.js) must equal the engine's ----
const site = readFileSync(join(root, 'docs/assets/site.js'), 'utf8');
const listOf = key => JSON.parse(site.match(new RegExp(key + ' = (\\[[^\\]]*\\])'))[1].replace(/'/g, '"'));
const g = await inst();
const styles = [...Array(11).keys()].map(i => cstr(g, g.c12w_style_name(i)));
const kits = [...Array(16).keys()].map(i => cstr(g, g.c12w_kit_name(i)));
check(JSON.stringify(styles) === JSON.stringify(listOf('STYLES')), 'style names differ from site.js: ' + styles.join(','));
check(JSON.stringify(kits) === JSON.stringify(listOf('KITS')), 'kit names differ from site.js: ' + kits.join(','));
console.log(`  names: ${styles.length} styles, ${kits.length} kits match the page`);

// ---- generator and mutator against the native ones ----
const ref = readFileSync(join(root, 'build/patterns.bin'));
const meta = JSON.parse(readFileSync(join(root, 'build/patterns_meta.json'), 'utf8'));
let same = 0, total = 0, at = 0;
const velVar = c => [c & 0xff, ((c >>> 8) & 0xff) >= 128 ? ((c >>> 8) & 0xff) - 128 : 0];
for (let style = 0; style < meta.styles.length; style++) for (let seed = 1; seed <= meta.seeds; seed++) {
  io(g).fill(0);
  g.c12w_generate(seed * 7919, 50, 50, style, 4);          // the exporter's settings: 64 steps, density and complexity 50
  for (let m = 0; m < meta.chain; m++) {
    if (m > 0) g.c12w_mutate(((seed << 16) | m) >>> 0, 1, 4);
    let ok = true;
    const cells = io(g);
    for (let i = 0; i < 448; i++) {
      const [v, w] = velVar(cells[i]);
      if (v !== ref[at + 2 * i] || w !== ref[at + 2 * i + 1]) ok = false;
    }
    at += 896; total++; same += ok;
  }
}
console.log(`  generator and mutator: ${same}/${total} patterns identical to the native build`);
check(same === total && total === 99, `generator: ${same}/${total}`);

// ---- audio against the native C render of the same scenario ----
async function render(rate, seconds, setup) {
  const x = await inst();
  check(x.c12w_init(rate, 1234) === 0, 'engine init at ' + rate);
  const out = new Int16Array(rate * seconds * 2);
  setup.start(x);
  for (let n = 0; n + 128 <= rate * seconds; n += 128) {
    setup.at?.(x, n);
    x.c12w_render(128);
    out.set(new Int16Array(x.memory.buffer, x.c12w_pcm(), 256), n * 2);
  }
  return { x, out };
}
const scenario = {
  start(x) {
    io(x).fill(0);
    x.c12w_generate(42, 60, 50, 2, 2);                      // tests/scenario.inc: seed 42, density 60, complexity 50, style 2, 32 steps
    x.c12w_commit(2);
    x.c12w_tempo(128);
    x.c12w_play(1);
  },
  at(x, n) {
    if (n === 32000 * 2) x.c12w_kit(5, 77, 60);
    if (n === 32000 * 3) { x.c12w_mutate(99, 2, 2); x.c12w_commit(2); }
  },
};
const { out } = await render(32000, 5, scenario);
const nat = readFileSync(join(root, 'build/test/equiv_c.raw'));
const ref16 = new Int16Array(nat.buffer, nat.byteOffset, nat.length / 2);
check(ref16.length === out.length, `render lengths ${ref16.length} / ${out.length}`);
let es = 0, ee = 0, ident = 0, peak = 0;
for (let i = 0; i < out.length; i++) { const d = out[i] - ref16[i]; es += ref16[i] * ref16[i]; ee += d * d; ident += d === 0; peak = Math.max(peak, Math.abs(out[i])); }
const snr = 10 * Math.log10(es / (ee || 1e-30));
console.log(`  5 s render: ${(100 * ident / out.length).toFixed(1)}% of samples identical to native, signal/difference ${snr.toFixed(1)} dB, peak ${peak}`);
check(peak > 3000, 'audible');
check(snr > 60, `signal/difference ${snr.toFixed(1)} dB`);

// ---- sequencer position ----
{
  const x = await inst(); x.c12w_init(32000, 1);
  io(x).fill(0); io(x)[0] = 110; x.c12w_commit(1); x.c12w_tempo(120);
  check(x.c12w_position() === 255, 'position 255 while stopped');
  x.c12w_play(1);
  const seen = new Set();
  for (let n = 0; n < 32000 * 2; n += 128) { x.c12w_render(128); seen.add(x.c12w_position()); }
  console.log(`  sequencer: ${seen.size} different positions in 2 s at 120 BPM`);
  check(seen.size >= 12, 'position moves: ' + [...seen].join(','));
  x.c12w_play(0); x.c12w_render(128); x.c12w_render(128);
  check(x.c12w_position() === 255, 'position 255 after stop');
  x.c12w_play(1); x.c12w_cell(0, 4, 100); x.c12w_trigger(0, 100, 0); x.c12w_pages(1);
}

// ---- other sample rates ----
for (const rate of [44100, 48000]) {
  const r = await render(rate, 1, { start(x) { io(x).fill(0); io(x)[0] = 120; x.c12w_commit(1); x.c12w_tempo(120); x.c12w_play(1); } });
  let pk = 0, bad = 0;
  for (const v of r.out) { pk = Math.max(pk, Math.abs(v)); }
  console.log(`  ${rate} Hz: 1 s rendered, peak ${pk}`);
  check(pk > 2000 && !bad, `${rate} Hz audible`);
}

console.log(`wasm: ${run} checks, ${failed} failed`);
process.exit(failed ? 1 : 0);
