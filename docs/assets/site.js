/* CLATTER 12 project page: the panel itself, running in the browser.
   The grid is drawn from the LEDs of CLATTER 12's own panel source (panel/clatter12.cpp), engine
   included, built for WebAssembly with CLATTER 12's simulator of the Plinky API (assets/c12panel.wasm,
   sim/wasm_panel.cpp). So what a pad does here is what the firmware does: nothing about the controls
   is re-implemented in this file. Until sound is started the panel runs on this thread, silent; then
   it moves, with its project, into an AudioWorklet (assets/c12-worklet.js). It is a demonstration of
   the interface, not a Plinky: no pressure sensor, no Plinky delay or reverb, no CPU limit.
   Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only */
(() => {
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const PAGES = ['SEQ', 'SOUND', 'GEN', 'MIX', 'FX', 'SAVE'];
const STYLES = ['MACHINE', 'ELECTRO', 'BREAKS', 'JUNGLE', 'HALFTIME', 'BROKEN', 'GARAGE', 'INDUSTRIAL', 'GLITCH', 'POLYMETER', 'DUB'];
const KITS = ['CLASSIC', 'ANALOG', 'DIGITAL', 'INDUSTRIAL', 'MINIMAL', 'GLITCH', 'TECHNO', 'ELECTRO', 'DUB', 'NOISE', 'METALLIC', 'SOFT', 'BROKEN', 'ACID', 'LO-FI', 'CHAOS'];
/* row 15 is the same on every page: its labels */
const ROW15 = { 0: 'SEQ', 1: 'SND', 2: 'GEN', 3: 'MIX', 4: 'FX', 5: 'SAV', 12: 'UND', 13: 'RED', 14: 'STP', 15: 'PLY' };
const ROW15_SEQ = { 6: 'GEN', 7: 'MUT', 8: 'STY', 9: 'CLR' };

/* ---- the panel: here (silent) or in the audio worklet ---- */
const P = { mod: null, x: null, node: null, ctx: null, gain: null, state: 'loading', why: '', remote: false,
  leds: new Uint32Array(256), page: 0, playing: 0, tempo: 120, status: [0, 0, 1, 0, 50, 0, 0, 0, 0], peak: 0, muted: false,
  waits: new Map(), id: 0, last: 0, timer: 0 };

function str(p) { const m = new Uint8Array(P.x.memory.buffer, p); let n = 0; while (m[n]) n++; return new TextDecoder().decode(m.subarray(0, n)); }
function putText(s) { const b = new TextEncoder().encode(s); new Uint8Array(P.x.memory.buffer, P.x.c12p_text(), b.length + 1).set([...b, 0]); return b.length; }
function ask(m) { return new Promise((res) => { const id = ++P.id; P.waits.set(id, res); P.node.port.postMessage({ ...m, id }); }); }

const panel = {
  touch(x, y, p) { if (P.remote) P.node.port.postMessage({ t: 'touch', x, y, p }); else if (P.x) P.x.c12p_touch(x, y, p); },
  releaseAll() { if (P.remote) P.node.port.postMessage({ t: 'release_all' }); else if (P.x) P.x.c12p_release_all(); },
  setPage(page) { if (P.remote) P.node.port.postMessage({ t: 'page', page }); else if (P.x) P.x.c12p_set_page(page); },
  setTempo(bpm) { if (P.remote) P.node.port.postMessage({ t: 'tempo', bpm }); else if (P.x) P.x.c12p_set_tempo(bpm); },
  async help(x, y) { if (P.remote) return (await ask({ t: 'help', x, y })).text; return P.x ? str(P.x.c12p_help(x, y)) : ''; },
  async save() { if (P.remote) return (await ask({ t: 'save' })).text; const n = P.x.c12p_save(); return str(P.x.c12p_text()).slice(0, n); },
  async load(text) { if (P.remote) return (await ask({ t: 'load', text })).ok; return P.x.c12p_load(putText(text)) === 0; },
};

/* here, silent: one panel frame per 4 ms of real time */
function localTick(now) {
  P.timer = 0;
  if (P.remote || !P.x) return;
  let n = Math.floor((now - P.last) / 4);
  if (n > 0) {
    P.last += n * 4;
    if (n > 30) { n = 30; P.last = now; }
    for (let i = 0; i < n; i++) P.x.c12p_frame();
    P.leds.set(new Uint32Array(P.x.memory.buffer, P.x.c12p_leds(), 256));
    P.page = P.x.c12p_page(); P.playing = P.x.c12p_playing(); P.tempo = P.x.c12p_tempo();
    for (let i = 0; i < 9; i++) P.status[i] = P.x.c12p_status(i);
    render();
  }
  P.timer = requestAnimationFrame(localTick);
}

async function loadPanel() {
  try {
    const r = await fetch('assets/c12panel.wasm');
    if (!r.ok) throw new Error('c12panel.wasm: HTTP ' + r.status);
    P.bytes = await r.arrayBuffer();
    P.mod = await WebAssembly.compile(P.bytes);
    P.x = (await WebAssembly.instantiate(P.mod, {})).exports;
    P.x.__wasm_call_ctors();
    if (P.x.c12p_init() !== 0) throw new Error('the engine did not start');
    P.state = 'off';
    P.last = performance.now();
    P.timer = requestAnimationFrame(localTick);
    hint();
  } catch (err) { P.state = 'failed'; P.why = String(err.message || err); }
  sound();
}

function startAudio() {
  if (P.state !== 'off') return;
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) { P.state = 'failed'; P.why = 'this browser has no Web Audio'; sound(); return; }
  P.state = 'starting'; sound();
  let ctx; /* created inside the user's tap: browsers refuse to start audio otherwise */
  try { ctx = new AC({ sampleRate: 32000, latencyHint: 'interactive' }); } catch (_) { ctx = new AC({ latencyHint: 'interactive' }); }
  P.ctx = ctx; ctx.resume();
  (async () => {
    if (!ctx.audioWorklet) throw new Error('AudioWorklet needs https or localhost');
    await ctx.audioWorklet.addModule('assets/c12-worklet.js');
    const node = new AudioWorkletNode(ctx, 'c12panel', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { module: P.mod } });
    const gain = ctx.createGain();
    node.connect(gain).connect(ctx.destination);
    P.node = node; P.gain = gain;
    node.port.onmessage = onWorklet;
    node.onprocessorerror = () => { P.state = 'failed'; P.why = 'the audio worklet stopped'; sound(); };
  })().catch((err) => { P.state = 'failed'; P.why = String(err.message || err); P.ctx = null; sound(); });
}
async function onWorklet(e) {
  const m = e.data;
  if (m.t === 'ready') {
    if (!m.ok) { P.state = 'failed'; P.why = 'the engine did not start in the worklet'; sound(); return; }
    /* the project moves across: what was built here keeps playing there */
    const text = await panel.save(), page = P.page, tempo = P.tempo, playing = P.playing;
    if (P.timer) cancelAnimationFrame(P.timer);
    P.timer = 0;
    P.x.c12p_release_all();
    const node = P.node;
    const loaded = await new Promise((res) => { const id = ++P.id; P.waits.set(id, res); node.port.postMessage({ t: 'load', text, id }); });
    if (!loaded.ok) console.warn('CLATTER 12: the project did not move to the audio worklet');
    P.remote = true;
    fingers.clear(); /* a touch that began before the move has been played here: it ends with it */
    panel.setPage(page);
    panel.setTempo(tempo);
    if (playing) { panel.touch(15, 15, 150); setTimeout(() => panel.touch(15, 15, 0), 60); }
    P.state = 'on'; P.rate = m.rate; sound();
  } else if (m.t === 'state') {
    P.leds = m.leds; P.page = m.page; P.playing = m.playing; P.tempo = m.tempo; P.status = m.status; P.peak = Math.max(P.peak * .8, m.peak);
    render(); meter();
  } else if (m.id && P.waits.has(m.id)) {
    P.waits.get(m.id)(m); P.waits.delete(m.id);
  }
}
function sound() {
  const b = $('#sound');
  b.textContent = { loading: 'LOADING...', off: 'START SOUND', starting: 'STARTING...', on: P.muted ? 'UNMUTE' : 'MUTE', failed: 'NO SOUND' }[P.state];
  b.setAttribute('aria-pressed', P.state === 'on' && !P.muted ? 'true' : 'false');
  b.disabled = P.state === 'loading' || P.state === 'starting' || P.state === 'failed';
  if (P.state === 'failed') say('NO SOUND: ' + P.why.toUpperCase());
  render();
}
let lastMeter = -1;
function meter() { const w = Math.round(clamp(Math.pow(P.peak, .5), 0, 1) * 20) * 5; if (w !== lastMeter) { lastMeter = w; $('#meter').style.width = w + '%'; } }

/* ---- the pads: the panel's LEDs ---- */
const pads = $('#pads'), els = [];
for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
  const b = document.createElement('button');
  b.type = 'button'; b.className = 'pad'; b.dataset.x = x; b.dataset.y = y; b.tabIndex = (x === 0 && y === 0) ? 0 : -1;
  const t = document.createElement('span'); t.className = 't'; b.appendChild(t);
  pads.appendChild(b); els.push(b);
}
const rowsEl = $('#rows'), rowEls = [];
for (let y = 0; y < 16; y++) { const s = document.createElement('span'); rowsEl.appendChild(s); rowEls.push(s); }

/* LED_RGB words: g << 24 | r << 16 | b << 8, 5 bits nominal (brighter saturates). A display gamma
   (0.6), because a lit LED looks brighter to the eye than its share of full scale on a screen. */
function rgb(c) { return [(c >>> 16) & 255, (c >>> 24) & 255, (c >>> 8) & 255].map((v) => Math.min(255, Math.round(255 * Math.pow(Math.min(v, 31) / 31, 0.6)))); }
function paint(b, c) {
  const k = c >>> 0;
  if (b._k === k) return; b._k = k;
  if (!k) { b.style.background = ''; b.style.boxShadow = ''; b.style.setProperty('--lab', 'rgba(255,255,255,.5)'); return; }
  const v = rgb(k), key = v.join(), m = Math.max(...v) / 255;
  b.style.background = `rgb(${key})`;
  b.style.boxShadow = m > .45 ? `0 0 ${Math.round(5 + m * 12)}px rgba(${key},${(m * .5).toFixed(2)})` : '';
  b.style.setProperty('--lab', (v[0] * .3 + v[1] * .59 + v[2] * .11) > 120 ? 'rgba(0,0,0,.78)' : 'rgba(255,255,255,.62)');
}
let labelsFor = -1;
async function labels() {
  /* row names and pad labels for the page shown, from the panel's own descriptions */
  const page = P.page;
  if (labelsFor === page) return;
  labelsFor = page;
  for (let x = 0; x < 16; x++) {
    const t = ROW15[x] || (page === 0 ? ROW15_SEQ[x] : '') || '';
    els[15 * 16 + x].firstChild.textContent = t;
  }
  for (let y = 0; y < 15; y++) {
    let name = '';
    if (page === 0 && y < 7) name = ['DRUM 1', 'DRUM 2', 'DRUM 3', 'SNARE', 'CYMBAL', 'CL HAT', 'OP HAT'][y];
    else if (page !== 5) {
      for (let x = 0; x < 16 && !name; x++) { const h = await panel.help(x, y); name = h ? h.split('\n')[0] : ''; }
      name = name.replace(/^(DRUM \d|SNARE|CYMBAL|HI-HAT|CL HAT|OP HAT) (?=[A-Z])/, '').replace(/:.*$/, '').replace(/ \d+$/, '');
      if (page === 0) name = { 8: 'VAR · RATCHET', 9: 'CONDITION', 10: 'ROLL · RAMP', 11: 'VELOCITY', 12: 'MUTE · SOLO', 13: 'LOCK · PLAY', 14: 'PAGE LEN SWING' }[y] || name;
      if (page === 3) name = { 0: 'MUTE', 1: 'SOLO', 2: 'LEVEL', 10: 'FINE - +', 11: 'PAN', 12: 'DELAY SEND', 13: 'REVERB SEND', 14: 'METER' }[y] ?? '';
      if (page === 4) name = name.replace('COMPRESSOR', 'COMP').replace(' (WET)', '').replace('RING MOD', 'RING').replace(/ RATIO.*/, ' RATIO').replace(/SHAPE.*/, 'SHAPE');
      if (page === 2) name = name.replace('MUTATION AMOUNT', 'MUT AMOUNT');
    }
    if (labelsFor !== page) return;
    rowEls[y].textContent = name.slice(0, 14);
  }
  rowEls[15].textContent = 'PAGES · PLAY';
  if (P.page === 5) rowEls[7].textContent = "PLINKY'S SAVE PAGE";
}
function render() {
  for (let i = 0; i < 256; i++) paint(els[i], P.leds[i]);
  labels();
  lcd();
  $('#savepage').hidden = P.page !== 5;
  for (const b of document.querySelectorAll('#pagebar button')) b.setAttribute('aria-pressed', String(+b.dataset.page === P.page));
}
function lcd() {
  const el = $('#lcd'), s = P.status;
  const snd = { loading: 'LOADING', off: 'SOUND OFF', starting: 'STARTING...', on: P.muted ? 'MUTED' : 'SOUND ON', failed: 'NO SOUND' }[P.state];
  const html = P.sayUntil > performance.now() ? `<b>${P.say}</b>`
    : `<span>PAGE <b>${PAGES[P.page] || '-'}</b></span><span><b>${P.tempo}</b> BPM</span><span>${P.playing ? '<b>PLAYING</b>' : 'STOPPED'}</span><span>LENGTH <b>${s[2] * 16}</b></span><span>SWING <b>${s[4]}</b></span><span>STYLE <b>${STYLES[s[0]] || ''}</b></span><span>KIT <b>${KITS[s[1]] || ''}</b></span><span>UNDO <b>${s[7]}</b></span><span id="snd">${snd}</span>`;
  if (el._h !== html) { el._h = html; el.innerHTML = html; }
}
function say(msg) { P.say = msg; P.sayUntil = performance.now() + 2600; setTimeout(render, 2650); render(); }

/* ---- the readout: the panel's own description of the pad ---- */
function show(name, kind, does) { $('#ro-name').textContent = name; $('#ro-kind').textContent = kind; $('#ro-does').textContent = does; }
async function describe(x, y) {
  const h = await panel.help(x, y);
  if (!h) { show(`PAD ${x + 1}, ${y + 1}`, PAGES[P.page] + ' PAGE', P.page === 5 ? "Plinky's own save and load page is drawn here on the device. On this page use SAVE PROJECT and LOAD PROJECT below." : 'Not used on this page.'); return; }
  const [name, kind, does] = h.split('\n');
  show(name, kind, does);
}
function hint() { show('READOUT', '', 'Hover or tap a pad: this says what it does, from the panel itself. The first tap on the grid also starts the sound.'); }

/* ---- input: pointers are fingers ---- */
const fingers = new Map(); /* pointerId -> [x, y] */
const latched = new Set();  /* "x,y" held by Shift-click */
const pressure = () => Math.round(+$('#pressure').value * 2.55);
function press(x, y, e) {
  let p = pressure();
  if (e && e.pointerType === 'pen' && e.pressure > 0) p = Math.round(e.pressure * 255);
  panel.touch(x, y, Math.max(20, p));
}
pads.addEventListener('pointerdown', (e) => {
  const b = e.target.closest('.pad'); if (!b) return;
  e.preventDefault();
  startAudio();
  const x = +b.dataset.x, y = +b.dataset.y, k = x + ',' + y;
  describe(x, y);
  if (e.shiftKey) {
    if (latched.has(k)) { latched.delete(k); panel.touch(x, y, 0); b.classList.remove('sel'); }
    else { latched.add(k); press(x, y, e); b.classList.add('sel'); }
    return;
  }
  if (latched.has(k)) return;
  try { b.releasePointerCapture(e.pointerId); } catch (_) {}
  fingers.set(e.pointerId, [x, y]);
  press(x, y, e);
});
pads.addEventListener('pointermove', (e) => {
  const f = fingers.get(e.pointerId);
  if (f && e.pointerType === 'pen' && e.pressure > 0) panel.touch(f[0], f[1], Math.max(20, Math.round(e.pressure * 255)));
});
function lift(e) { const f = fingers.get(e.pointerId); if (!f) return; fingers.delete(e.pointerId); panel.touch(f[0], f[1], 0); }
for (const ev of ['pointerup', 'pointercancel']) window.addEventListener(ev, lift);
pads.addEventListener('pointerover', (e) => { const b = e.target.closest('.pad'); if (b && !fingers.size) describe(+b.dataset.x, +b.dataset.y); });
pads.addEventListener('focusin', (e) => { const b = e.target.closest('.pad'); if (b) describe(+b.dataset.x, +b.dataset.y); });
pads.addEventListener('keydown', (e) => {
  const b = e.target.closest('.pad'); if (!b) return;
  const x = +b.dataset.x, y = +b.dataset.y;
  if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); if (!e.repeat) { startAudio(); press(x, y); } return; }
  const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key]; if (!d) return;
  e.preventDefault(); const n = els[clamp(y + d[1], 0, 15) * 16 + clamp(x + d[0], 0, 15)];
  b.tabIndex = -1; n.tabIndex = 0; n.focus();
});
pads.addEventListener('keyup', (e) => { const b = e.target.closest('.pad'); if (b && (e.key === 'Enter' || e.key === ' ')) panel.touch(+b.dataset.x, +b.dataset.y, 0); });
pads.addEventListener('contextmenu', (e) => e.preventDefault());

/* Plinky's side buttons: tempo on the left, pages on the right */
const SIDE = {
  'tempo-up': ['TEMPO UP', 'Plinky’s top left side button: tempo up (hold to repeat). CLATTER 12 follows Plinky’s tempo and transport.', () => panel.setTempo(clamp(P.tempo + 1, 30, 300))],
  'tempo-down': ['TEMPO DOWN', 'Plinky’s bottom left side button: tempo down (hold to repeat).', () => panel.setTempo(clamp(P.tempo - 1, 30, 300))],
  'page-up': ['PAGE UP', 'Plinky’s right side buttons scroll through the panel’s pages (SEQ, SOUND, GEN, MIX, FX, SAVE). On the device, up from SEQ opens the DSP load page.', () => panel.setPage(clamp(P.page - 1, 0, 5))],
  'page-down': ['PAGE DOWN', 'Plinky’s right side buttons scroll through the panel’s pages. Row 15’s first six pads jump straight to one.', () => panel.setPage(clamp(P.page + 1, 0, 5))],
};
for (const id in SIDE) {
  const b = $('#' + id); let t1, t2;
  const stopRep = () => { clearTimeout(t1); clearInterval(t2); };
  b.addEventListener('pointerdown', () => { show(SIDE[id][0], 'SIDE BUTTON', SIDE[id][1]); SIDE[id][2](); if (id.startsWith('tempo')) t1 = setTimeout(() => { t2 = setInterval(SIDE[id][2], 90); }, 380); });
  for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) b.addEventListener(ev, stopRep);
  b.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); SIDE[id][2](); } });
  b.addEventListener('pointerover', () => show(SIDE[id][0], 'SIDE BUTTON', SIDE[id][1]));
}
for (const b of document.querySelectorAll('#pagebar button')) b.addEventListener('click', () => panel.setPage(+b.dataset.page));

const pr = $('#pressure'), prOut = $('#pressure-out');
const setPr = () => { prOut.textContent = String(pressure()); };
pr.addEventListener('input', setPr); setPr();
$('#face').addEventListener('click', (e) => { const on = e.currentTarget.getAttribute('aria-pressed') !== 'true'; e.currentTarget.setAttribute('aria-pressed', on); $('#board').classList.toggle('faceplate', on); });
$('#sound').addEventListener('click', () => {
  if (P.state === 'off') startAudio();
  else if (P.state === 'on') { P.muted = !P.muted; P.gain.gain.value = P.muted ? 0 : 1; sound(); }
});
$('#release').addEventListener('click', () => { latched.clear(); for (const b of els) b.classList.remove('sel'); panel.releaseAll(); });
$('#save').addEventListener('click', async () => {
  const text = await panel.save();
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
  a.download = 'clatter12-project.json';
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  say('PROJECT SAVED: THE SAME JSON PLINKY WRITES');
});
$('#load').addEventListener('change', async (e) => {
  const f = e.target.files[0]; if (!f) return;
  const ok = await panel.load(await f.text());
  say(ok ? 'PROJECT LOADED' : 'NOT A CLATTER 12 PROJECT: NOTHING CHANGED');
  e.target.value = '';
});

window.C12PAGE = { P, panel }; /* for the page's own tests */
hint(); sound(); loadPanel();
})();
