/* CLATTER 12 project page: the panel's interface (panel/clatter12.cpp) as a working model, wired to
   the real engine. The sound, the sequencer and CLATTER's generator are the panel's own code built for
   WebAssembly (assets/c12.wasm, core/c12_wasm.c). The audio runs in an AudioWorklet
   (assets/c12-worklet.js); a second instance on this thread only runs the generator for GEN and MUT.
   Pad behaviour mirrors the panel; where it cannot (a mouse has no pressure) the page says so.
   Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only */
(() => {
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const NT = 7, NS = 64;

/* names as the engine reports them (tests/test_wasm.mjs checks these against the module) */
const STYLES = ['MACHINE', 'ELECTRO', 'BREAKS', 'JUNGLE', 'HALFTIME', 'BROKEN', 'GARAGE', 'INDUSTRIAL', 'GLITCH', 'POLYMETER', 'DUB'];
const KITS = ['CLASSIC', 'ANALOG', 'DIGITAL', 'INDUSTRIAL', 'MINIMAL', 'GLITCH', 'TECHNO', 'ELECTRO', 'DUB', 'NOISE', 'METALLIC', 'SOFT', 'BROKEN', 'ACID', 'LO-FI', 'CHAOS'];

/* LED colours of the panel (panel/clatter12.cpp track_col), 5-bit values scaled to 8-bit */
const TRACKS = [
  ['DRUM 1', [255, 16, 0]], ['DRUM 2', [255, 98, 0]], ['DRUM 3', [230, 213, 0]], ['SNARE', [33, 255, 16]],
  ['CYMBAL', [0, 213, 197]], ['CL HAT', [16, 66, 255]], ['OP HAT', [180, 16, 255]],
];
const VARS = ['none', 'A', 'B', 'second sound', 'second sound A', 'second sound B'];
const lerp = (a, b, t) => a.map((v, i) => Math.round(v + (b[i] - v) * t));
const scale = (c, f) => c.map(v => Math.min(255, Math.round(v * f)));
const add = (a, b) => a.map((v, i) => Math.min(255, v + b[i]));
const hue = (h, l) => { const a = (h % 16) / 16, k = n => (n + a * 12) % 12, m = Math.min(l, 1 - l); const f = n => l - m * Math.max(-1, Math.min(k(n) - 3, 9 - k(n), 1)); return [f(0), f(8), f(4)].map(v => Math.round(v * 255)); };
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const velOf = pct => Math.round(32 + pct * 0.95);

/* ---- state: the pattern is the sequencer's own 32-bit cells (velocity, tone, condition) ---- */
const S = {
  cell: new Uint32Array(NT * NS), pages: 1, view: 0, playing: false, pos: -1, bpm: 120,
  style: 0, kit: 0, kitSet: false, genSeed: 1, mutN: 0, undo: null, sticky: -1,
  pressure: 85, face: true, clrT0: 0, bpmUntil: 0, say: '', sayUntil: 0, sel: -1,
};
const V = i => S.cell[i] & 0xff;
const TONE = i => (S.cell[i] >>> 8) & 0xff;
const VR = i => { const t = TONE(i); return t >= 128 ? t - 128 : 0; };
const mk = (v, variation) => (v | ((variation ? 128 + variation : 0) << 8)) >>> 0;

/* ---- the engine ---- */
const E = { mod: null, bytes: null, gen: null, loading: null, ctx: null, node: null, gain: null, state: 'off', why: '', rate: 0, peak: 0, muted: false, queue: [], timer: 0 };
function loadEngine() {
  E.loading = (async () => {
    const r = await fetch('assets/c12.wasm');
    if (!r.ok) throw new Error('c12.wasm: HTTP ' + r.status);
    E.bytes = await r.arrayBuffer();
    E.mod = await WebAssembly.compile(E.bytes);
    E.gen = (await WebAssembly.instantiate(E.mod, {})).exports;
  })().catch(err => { E.why = 'the engine did not load (' + (err.message || err) + ')'; sound(); });
}
function post(m, transfer) {
  if (E.state === 'on') E.node.port.postMessage(m, transfer || []);
  else if (E.state === 'starting' && m.t === 'trigger') E.queue.push(m);
}
function syncPattern() { const b = S.cell.slice().buffer; post({ t: 'pattern', cells: b, pages: S.pages }, [b]); }
function pushAll() {
  syncPattern();
  post({ t: 'tempo', bpm: S.bpm });
  if (S.kitSet) post({ t: 'kit', character: S.kit, seed: S.kit * 37, wild: 50 });
  if (S.playing) post({ t: 'play', on: 1 });
}
function startAudio() {
  if (E.state !== 'off') return;
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC || !window.WebAssembly) { E.state = 'failed'; E.why = 'this browser has no Web Audio or WebAssembly'; sound(); return; }
  E.state = 'starting'; sound();
  let ctx; /* created inside the user's tap: browsers refuse to start audio otherwise */
  try { ctx = new AC({ sampleRate: 32000, latencyHint: 'interactive' }); } catch (_) { ctx = new AC({ latencyHint: 'interactive' }); }
  E.ctx = ctx; ctx.resume();
  (async () => {
    await E.loading;
    if (!E.mod) throw new Error(E.why || 'no engine');
    if (!ctx.audioWorklet) throw new Error('AudioWorklet needs https or localhost');
    await ctx.audioWorklet.addModule('assets/c12-worklet.js');
    const opts = { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] };
    let node;
    try { node = new AudioWorkletNode(ctx, 'c12', { ...opts, processorOptions: { module: E.mod } }); }
    catch (_) { node = new AudioWorkletNode(ctx, 'c12', { ...opts, processorOptions: { bytes: E.bytes } }); }
    node.port.onmessage = onEngine;
    node.onprocessorerror = () => { E.state = 'failed'; E.why = 'the audio worklet stopped'; sound(); };
    E.gain = ctx.createGain(); node.connect(E.gain); E.gain.connect(ctx.destination); E.node = node;
  })().catch(err => { E.state = 'failed'; E.why = String(err.message || err); sound(); });
}
function onEngine(e) {
  const m = e.data;
  if (m.t === 'ready') {
    if (!m.ok) { E.state = 'failed'; E.why = 'the engine would not start at ' + m.rate + ' Hz'; sound(); return; }
    E.state = 'on'; E.rate = m.rate; clearInterval(E.timer); E.timer = 0; if (raf) { cancelAnimationFrame(raf); raf = 0; }
    pushAll(); for (const q of E.queue) post(q); E.queue = []; S.pos = -1; sound(); render();
  } else if (m.t === 'pos') { S.pos = m.pos === 255 ? -1 : m.pos; render(); }
  else if (m.t === 'level') { E.peak = m.peak; meter(); }
}
function sound() {
  const el = $('#snd'), chip = $('#sound');
  const txt = { off: 'SOUND OFF', starting: 'STARTING...', on: E.muted ? 'MUTED' : 'SOUND ON', failed: 'NO SOUND' }[E.state];
  if (el) el.textContent = txt;
  if (chip) { chip.setAttribute('aria-pressed', E.state === 'on' && !E.muted); chip.textContent = E.state === 'failed' ? 'NO SOUND' : (E.state === 'on' && !E.muted ? 'SOUND ON' : (E.state === 'on' ? 'SOUND OFF' : 'START SOUND')); }
  if (E.state === 'failed') say('NO SOUND: ' + E.why.toUpperCase().slice(0, 60));
  render();
}
let lastMeter = -1;
function meter() { const w = Math.round(clamp(Math.pow(E.peak, .5), 0, 1) * 20) * 5; if (w !== lastMeter) { lastMeter = w; $('#meter').style.width = w + '%'; } }
function audition(track, vel, variation) { post({ t: 'trigger', track, velocity: vel, variation }); }

/* ---- the pads ---- */
const pads = $('#pads'), els = [];
const CTRL = [['P1', 'PAGE 1'], ['P2', 'PAGE 2'], ['P3', 'PAGE 3'], ['P4', 'PAGE 4'], ['16', 'LENGTH 16'], ['32', 'LENGTH 32'], ['48', 'LENGTH 48'], ['64', 'LENGTH 64'],
  ['GEN', 'GENERATE'], ['MUT', 'MUTATE'], ['KIT', 'NEXT KIT'], ['STY', 'STYLE'], ['UND', 'UNDO'], ['CLR', 'CLEAR'], ['STP', 'STOP'], ['PLY', 'PLAY']];
for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
  const b = document.createElement('button');
  b.type = 'button'; b.className = 'pad'; b.dataset.x = x; b.dataset.y = y; b.tabIndex = (x === 0 && y === 0) ? 0 : -1;
  const t = document.createElement('span'); t.className = 't';
  if (y === 15) t.textContent = CTRL[x][0];
  b.appendChild(t);
  b.setAttribute('aria-label', describe(x, y).name);
  pads.appendChild(b); els.push(b);
}
const rowsEl = $('#rows');
for (let y = 0; y < 16; y++) {
  const s = document.createElement('span');
  if (y < 7) { s.textContent = TRACKS[y][0]; s.style.setProperty('--k', `rgb(${TRACKS[y][1]})`); }
  else if (y === 7) { s.textContent = 'PLAYHEAD'; s.style.setProperty('--k', '#e8e8e8'); }
  else if (y < 15) { s.textContent = TRACKS[y - 8][0] + ' VAR'; s.style.setProperty('--k', `rgb(${scale(TRACKS[y - 8][1], .35)})`); }
  else { s.textContent = 'CONTROLS'; s.style.setProperty('--k', '#ff9a1f'); }
  rowsEl.appendChild(s);
}

/* ---- the bpm number, drawn on the grid as the Plinky does for a moment ---- */
const GLYPH = ['111101101101111', '010110010010111', '111001111100111', '111001111001111', '101101111001001',
  '111100111001111', '111100111101111', '111001001010010', '111101111101111', '111101111001111'];
function bpmPixel(x, y) {
  const s = String(S.bpm), w = s.length * 4 - 1, x0 = Math.floor((16 - w) / 2), y0 = 5;
  const gx = x - x0, gy = y - y0;
  if (gy < 0 || gy > 4 || gx < 0 || gx >= w || gx % 4 === 3) return null;
  return GLYPH[+s[Math.floor(gx / 4)]][gy * 3 + (gx % 4)] === '1' ? [230, 230, 230] : null;
}

/* ---- what a pad shows (the panel's draw()) ---- */
function led(x, y, now) {
  if (S.bpmUntil > now) return { c: bpmPixel(x, y) };
  const head = S.playing && S.pos >= 0 ? S.pos : -1;
  const inLen = S.view < S.pages;
  if (y < 7 || (y > 7 && y < 15)) {
    const t = y < 7 ? y : y - 8, s = S.view * 16 + x, i = t * NS + s, v = V(i), at = head === s;
    const tint = S.face ? scale(TRACKS[t][1], .065) : null;
    if (y < 7) {
      if (v) { let c = scale(TRACKS[t][1], (64 + Math.min(v, 127) * 192 / 127) / 256); if (!inLen) c = scale(c, .25); if (at) c = add(c, [115, 115, 115]); return { c }; }
      if (at) return { c: [33, 33, 33] };
      return { c: x % 4 === 0 ? [10, 10, 10] : null, tint };
    }
    let c = null;
    if (v) {
      const k = VR(i), base = k >= 3 ? [255, 16, 164] : TRACKS[t][1];
      c = k ? lerp(base, [165, 165, 165], [0, 90, 180, 0, 90, 180][k] / 256) : scale(TRACKS[t][1], .25);
      if (at) c = add(c, [33, 33, 33]);
    }
    return { c, tint };
  }
  if (y === 7) {
    const at = head >= 0 && Math.floor(head / 16) === S.view && head % 16 === x;
    return { c: at ? [225, 225, 225] : (x % 4 === 0 ? [10, 10, 10] : null) };
  }
  const playPage = head >= 0 ? Math.floor(head / 16) : -1;
  if (x < 4) {
    let c = x === S.view ? [225, 225, 225] : (x < S.pages ? [33, 33, 33] : [8, 8, 8]);
    if (x === playPage && x !== S.view) c = [0, 147, 33];
    if (x === S.sticky || heldPages().some(p => p !== x)) c = add(c, [82, 41, 0]);
    return { c };
  }
  if (x < 8) return { c: (x - 4) < S.pages ? [197, 98, 0] : [25, 8, 0] };
  if (x === 8) return { c: [115, 255, 0] };
  if (x === 9) return { c: [0, 197, 148] };
  if (x === 10) return { c: hue(S.kit, .5) };
  if (x === 11) return { c: hue(S.style * 3, .42) };
  if (x === 12) return { c: S.undo ? [82, 82, 82] : [8, 8, 8] };
  if (x === 13) { let c = [66, 0, 0]; if (S.clrT0) c = lerp(c, [255, 0, 0], clamp((now - S.clrT0) / 500, 0, 1)); return { c }; }
  if (x === 14) return { c: S.playing ? [82, 0, 0] : [25, 0, 0] };
  return { c: S.playing ? [0, 255, 0] : [0, 49, 0] };
}
function paint(b, o) {
  const c = o.c, key = c ? c.join() : (o.tint ? 't' + o.tint.join() : '');
  if (b._k === key) return; b._k = key;
  if (!c) { b.style.background = o.tint ? `rgb(${o.tint})` : ''; b.style.boxShadow = ''; b.style.setProperty('--lab', 'rgba(255,255,255,.5)'); return; }
  const m = Math.max(...c) / 255;
  b.style.background = `rgb(${key})`;
  b.style.boxShadow = m > .45 ? `0 0 ${Math.round(5 + m * 12)}px rgba(${key},${(m * .5).toFixed(2)})` : '';
  b.style.setProperty('--lab', (c[0] * .3 + c[1] * .59 + c[2] * .11) > 120 ? 'rgba(0,0,0,.78)' : 'rgba(255,255,255,.62)');
}
let lastView = -1;
function render() {
  const now = performance.now();
  if (lastView !== S.view) { lastView = S.view; for (let i = 0; i < 256; i++) { const y = (i / 16) | 0; if (y < 7 || (y > 7 && y < 15)) els[i].setAttribute('aria-label', describe(i % 16, y).name); } }
  for (let i = 0; i < 256; i++) paint(els[i], led(i % 16, (i / 16) | 0, now));
  lcd(now);
}
function lcd(now) {
  const el = $('#lcd'), snd = { off: 'SOUND OFF', starting: 'STARTING...', on: E.muted ? 'MUTED' : 'SOUND ON', failed: 'NO SOUND' }[E.state];
  const html = S.sayUntil > now
    ? `<b>${S.say}</b>`
    : `<span>STYLE <b>${STYLES[S.style]}</b></span><span>KIT <b>${S.kitSet ? KITS[S.kit] : 'INIT'}</b></span><span><b>${S.bpm}</b> BPM</span><span>LENGTH <b>${S.pages * 16}</b></span><span>PAGE <b>${S.view + 1}</b>/${S.pages}</span><span id="snd">${snd}</span>`;
  if (el._h !== html) { el._h = html; el.innerHTML = html; }
}
function say(msg) { S.say = msg; S.sayUntil = performance.now() + 2600; setTimeout(render, 2650); render(); }

/* ---- descriptions (the readout) ---- */
function describe(x, y) {
  if (y < 7) return { name: `${TRACKS[y][0]} · STEP ${S.view * 16 + x + 1}`, kind: `STEP PAD · ${TRACKS[y][0]}`,
    does: 'Tap an empty pad to switch the step on, as loud as you press. Tap a lit pad to switch it off. Hold a lit pad for over 0.3 s to keep it and re-take its velocity from the pressure. Stopped, a new step plays its hit.',
    tryit: 'Try it: tap a pad, then press PLY. Move the pressure slider and tap another.' };
  if (y === 7) return { name: 'PLAYHEAD', kind: 'DISPLAY ROW', does: 'One white pad marks the step playing now. It follows the sequencer, so it only moves while the transport is running.', tryit: 'Try it: press PLY.' };
  if (y < 15) return { name: `${TRACKS[y - 8][0]} VARIATION`, kind: `VARIATION PAD · ${TRACKS[y - 8][0]}`,
    does: `Under a lit step, tap to cycle its variation: none, A, B${(y - 8 === 2 || y - 8 === 3) ? ', then its second sound (percussion on Drum 3, clap on Snare) and that sound’s A and B' : ''}. A variation is the same LXR voice with its parameters moved for that one hit. Under an empty step nothing happens.`,
    tryit: 'Try it: tap under a lit kick, then again, with the beat playing.' };
  const T = [
    ['PAGE 1', 'Tap to view steps 1 to 16. Hold it and tap another page pad to copy this page onto that one.'],
    ['PAGE 2', 'Tap to view steps 17 to 32. Hold another page pad and tap this to paste onto it.'],
    ['PAGE 3', 'Tap to view steps 33 to 48.'], ['PAGE 4', 'Tap to view steps 49 to 64.'],
    ['LENGTH 16', 'The pattern is 16 steps long (one page). Lit amber pads show how many pages play.'],
    ['LENGTH 32', 'The pattern is 32 steps long (two pages).'], ['LENGTH 48', 'The pattern is 48 steps long (three pages).'], ['LENGTH 64', 'The pattern is 64 steps long (four pages).'],
    ['GENERATE', 'Make a new pattern in the current style, with CLATTER’s generator running live on this page. It keeps your length. Undo brings the old pattern back.'],
    ['MUTATE', 'Change the current pattern a little, with CLATTER’s mutator: it works on whatever is on the grid, your own edits included.'],
    ['NEXT KIT', 'Step through CLATTER’s 16 designed kits. The sound changes at once. (The panel stays quiet when you do this; this page also plays a kick, hat and snare when stopped, so you can hear the kit.)'],
    ['STYLE', 'Cycle the generator’s rhythm style (11). The next GEN uses it.'],
    ['UNDO', 'Reverse the last generate, mutate, clear or page copy. Press again to redo.'],
    ['CLEAR', 'Hold for half a second to empty every step on every page. The pad fills red while you hold. Undo brings it back.'],
    ['STOP', 'Stop the transport.'], ['PLAY', 'Start or stop the transport. Tempo follows the two left side buttons.'],
  ][x];
  return { name: T[0], kind: 'CONTROL PAD · BOTTOM ROW', does: T[1], tryit: '' };
}
function show(info) {
  $('#ro-name').textContent = info.name; $('#ro-kind').textContent = info.kind;
  $('#ro-does').textContent = info.does; $('#ro-try').textContent = info.tryit || '';
  $('#ro-try').style.display = info.tryit ? '' : 'none';
}
function select(b) {
  if (S.sel >= 0) els[S.sel].classList.remove('sel');
  if (b) { S.sel = +b.dataset.y * 16 + +b.dataset.x; b.classList.add('sel'); show(describe(+b.dataset.x, +b.dataset.y)); } else S.sel = -1;
}
show({ name: 'READOUT', kind: '', does: 'Hover or tap a pad. The first tap switches the sound on.', tryit: '' });

/* ---- editing: every change goes to the sequencer too ---- */
const act = new Map();
const heldPages = () => [...act.values()].filter(a => a.kind === 'page').map(a => a.x);
function setCell(i, c) { S.cell[i] = c >>> 0; post({ t: 'cell', track: (i / NS) | 0, step: i % NS, cell: c >>> 0 }); }
function setPages(n) { S.pages = n; post({ t: 'pages', n }); }
function snapshot() { S.undo = { cell: S.cell.slice(), pages: S.pages }; }
function undo() {
  if (!S.undo) { say('NOTHING TO UNDO'); return; }
  const u = { cell: S.cell.slice(), pages: S.pages };
  S.cell.set(S.undo.cell); S.pages = S.undo.pages; S.undo = u; syncPattern(); say('UNDO');
}
const velFrom = e => e && e.pointerType === 'pen' && e.pressure > 0 ? clamp(Math.round(32 + Math.pow(e.pressure, .8) * 95), 1, 127) : velOf(S.pressure);

function withGenerator(fn) {
  if (!E.gen) { say('ENGINE LOADING...'); return false; }
  const io = new Uint32Array(E.gen.memory.buffer, E.gen.c12w_io(), 448);
  io.set(S.cell); fn(E.gen);
  S.cell.set(new Uint32Array(E.gen.memory.buffer, E.gen.c12w_io(), 448));
  return true;
}
function gen() { /* panel: seed = gen_seed++, density 50, complexity 50, the current style, the current length */
  snapshot();
  if (withGenerator(g => g.c12w_generate(S.genSeed, 50, 50, S.style, S.pages))) { S.genSeed = (S.genSeed + 1) & 0xffff; syncPattern(); say(`GENERATED ${STYLES[S.style]}`); } else S.undo = null;
}
function mut() { /* panel: seed = (gen_seed << 16) | mut_count++, medium strength, no locks */
  snapshot();
  if (withGenerator(g => g.c12w_mutate((((S.genSeed << 16) | (S.mutN & 0xffff)) >>> 0), 1, S.pages))) { S.mutN = (S.mutN + 1) & 0xffff; syncPattern(); say('MUTATED'); } else S.undo = null;
}
function copyPage(from, to) {
  snapshot();
  for (let t = 0; t < NT; t++) for (let s = 0; s < 16; s++) S.cell[t * NS + to * 16 + s] = S.cell[t * NS + from * 16 + s];
  syncPattern(); say(`PAGE ${from + 1} COPIED TO ${to + 1}`);
}
function nextKit() {
  S.kit = (S.kit + 1) % KITS.length; S.kitSet = true;
  post({ t: 'kit', character: S.kit, seed: S.kit * 37, wild: 50 }); say(`KIT ${KITS[S.kit]}`);
  if (!S.playing) [[0, 110, 0], [5, 80, 170], [3, 105, 340], [5, 80, 510], [0, 110, 680]].forEach(([t, v, ms]) => setTimeout(() => { if (!S.playing) audition(t, v, 0); }, 260 + ms));
}
function beat() {
  S.cell.fill(0);
  for (const s of [0, 4, 8, 12]) S.cell[0 * NS + s] = mk(110, 0);
  for (const s of [4, 12]) S.cell[3 * NS + s] = mk(105, 0);
  for (const s of [2, 6, 10, 14]) S.cell[5 * NS + s] = mk(80, 0);
  S.pages = 1; S.view = 0; S.undo = null;
}
beat();

function down(x, y, e, id) {
  const now = performance.now();
  if (y < 7) {
    const i = y * NS + S.view * 16 + x, was = V(i) > 0;
    if (!was) { const v = velFrom(e); setCell(i, mk(v, 0)); if (!S.playing) audition(y, v, 0); }
    act.set(id, { kind: 'step', i, was, t0: now });
  } else if (y > 7 && y < 15) {
    const t = y - 8, i = t * NS + S.view * 16 + x;
    if (V(i)) {
      const last = (t === 2 || t === 3) ? 5 : 2, k = VR(i) >= last ? 0 : VR(i) + 1;
      setCell(i, ((S.cell[i] & 0xffff00ff) | ((k ? 128 + k : 0) << 8)) >>> 0);
      if (!S.playing) audition(t, V(i), k);
      say(`${TRACKS[t][0]} STEP ${S.view * 16 + x + 1}: ${VARS[k].toUpperCase()}`);
    }
  } else if (y === 15) {
    if (x < 4) {
      const other = heldPages().find(p => p !== x);
      if (other !== undefined) copyPage(other, x);
      else if (S.sticky >= 0 && S.sticky !== x) { copyPage(S.sticky, x); S.sticky = -1; }
      else { S.view = x; if (e && e.shiftKey) S.sticky = S.sticky === x ? -1 : x; }
      act.set(id, { kind: 'page', x });
    } else if (x < 8) { setPages(x - 3); say(`LENGTH ${S.pages * 16} STEPS`); }
    else if (x === 8) gen();
    else if (x === 9) mut();
    else if (x === 10) nextKit();
    else if (x === 11) { S.style = (S.style + 1) % STYLES.length; say(`STYLE ${STYLES[S.style]}`); }
    else if (x === 12) undo();
    else if (x === 13) { S.clrT0 = now; act.set(id, { kind: 'clr' }); tickClr(); }
    else if (x === 14) stop();
    else if (x === 15) { S.playing ? stop() : play(); }
  }
  render();
}
function up(id, e) {
  const a = act.get(id); if (!a) return; act.delete(id);
  if (a.kind === 'step') {
    if (a.was) { if (performance.now() - a.t0 < 300) setCell(a.i, 0); else setCell(a.i, ((S.cell[a.i] & ~0x7f) | velFrom(e)) >>> 0); }
  } else if (a.kind === 'clr') S.clrT0 = 0;
  render();
}
function tickClr() {
  if (!S.clrT0) return;
  if (performance.now() - S.clrT0 >= 500) { snapshot(); S.cell.fill(0); syncPattern(); S.clrT0 = 0; for (const [k, a] of act) if (a.kind === 'clr') act.delete(k); say('CLEARED. UNDO BRINGS IT BACK'); return; }
  render(); requestAnimationFrame(tickClr);
}

/* ---- transport: the engine's sequencer when sound is on, a silent timer until then ---- */
let nextT = 0, raf = 0;
function play() {
  S.playing = true; S.pos = -1; post({ t: 'play', on: 1 });
  if (E.state !== 'on') { nextT = performance.now(); if (!raf) raf = requestAnimationFrame(loop); }
}
function stop() { S.playing = false; S.pos = -1; post({ t: 'play', on: 0 }); render(); }
function loop() {
  raf = 0; if (!S.playing || E.state === 'on') return;
  const now = performance.now(); let moved = false;
  while (now >= nextT) { S.pos = (S.pos + 1) % (S.pages * 16); nextT += 15000 / S.bpm; moved = true; }
  if (moved) render();
  raf = requestAnimationFrame(loop);
}
function nudge(d) { S.bpm = clamp(S.bpm + d, 30, 300); post({ t: 'tempo', bpm: S.bpm }); S.bpmUntil = performance.now() + 1300; setTimeout(render, 1350); render(); }

/* ---- input ---- */
$('#grid').addEventListener('pointerdown', startAudio, true);
$('#grid').addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') startAudio(); }, true);
pads.addEventListener('pointerdown', e => { const b = e.target.closest('.pad'); if (!b) return; select(b); down(+b.dataset.x, +b.dataset.y, e, e.pointerId); });
for (const ev of ['pointerup', 'pointercancel']) window.addEventListener(ev, e => up(e.pointerId, e));
pads.addEventListener('pointerover', e => { const b = e.target.closest('.pad'); if (b && !act.size) show(describe(+b.dataset.x, +b.dataset.y)); });
pads.addEventListener('focusin', e => { const b = e.target.closest('.pad'); if (b) show(describe(+b.dataset.x, +b.dataset.y)); });
pads.addEventListener('keydown', e => {
  const b = e.target.closest('.pad'); if (!b) return;
  const x = +b.dataset.x, y = +b.dataset.y;
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); if (!e.repeat) { select(b); down(x, y, e, 'kb'); } return; }
  const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key]; if (!d) return;
  e.preventDefault(); const nx = clamp(x + d[0], 0, 15), ny = clamp(y + d[1], 0, 15), n = els[ny * 16 + nx];
  b.tabIndex = -1; n.tabIndex = 0; n.focus();
});
pads.addEventListener('keyup', e => { if (e.key === 'Enter' || e.key === ' ') up('kb', e); });
pads.addEventListener('contextmenu', e => e.preventDefault());

const SIDE = {
  'tempo-up': ['TEMPO UP', 'The top left side button nudges the tempo up (hold to repeat). The grid shows the number for a moment, as on the device.', 1],
  'tempo-down': ['TEMPO DOWN', 'The bottom left side button nudges the tempo down (hold to repeat).', -1],
  'page-up': ['PAGE UP', 'The right side buttons move between pages. On the device, up from the grid opens CLATTER 12’s DSP load page. They do nothing on this page.', 0],
  'page-down': ['PAGE DOWN', 'The right side buttons move between pages. They do nothing on this page.', 0],
};
for (const id in SIDE) {
  const b = $('#' + id); let t1, t2;
  const stopRep = () => { clearTimeout(t1); clearInterval(t2); };
  b.addEventListener('pointerdown', () => { show({ name: SIDE[id][0], kind: 'SIDE BUTTON', does: SIDE[id][1], tryit: '' }); const d = SIDE[id][2]; if (!d) return; nudge(d); t1 = setTimeout(() => { t2 = setInterval(() => nudge(d), 90); }, 380); });
  for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) b.addEventListener(ev, stopRep);
  b.addEventListener('keydown', e => { if ((e.key === 'Enter' || e.key === ' ') && SIDE[id][2]) { e.preventDefault(); nudge(SIDE[id][2]); } });
  b.addEventListener('pointerover', () => show({ name: SIDE[id][0], kind: 'SIDE BUTTON', does: SIDE[id][1], tryit: '' }));
}

const pr = $('#pressure'), prOut = $('#pressure-out');
const setPr = () => { S.pressure = +pr.value; prOut.textContent = 'VEL ' + velOf(S.pressure); };
pr.addEventListener('input', setPr); setPr();
$('#face').addEventListener('click', e => { S.face = !S.face; e.currentTarget.setAttribute('aria-pressed', S.face); $('#board').classList.toggle('faceplate', S.face); for (const b of els) b._k = null; render(); });
$('#reset').addEventListener('click', () => { stop(); beat(); syncPattern(); say('DEFAULT BEAT'); });
$('#sound').addEventListener('click', () => {
  if (E.state === 'off') startAudio();
  else if (E.state === 'on') { E.muted = !E.muted; E.gain.gain.value = E.muted ? 0 : 1; sound(); }
});

window.C12PAGE = { S, E }; /* for the page's own tests */
loadEngine(); sound(); render();
})();
