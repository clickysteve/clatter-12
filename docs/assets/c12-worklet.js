/* CLATTER 12 on the web: the panel inside an AudioWorklet.
   Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

   Hosts docs/assets/c12panel.wasm: CLATTER 12's panel source as it is (engine and Plinky glue) with
   CLATTER 12's simulator of the Plinky API (sim/), built for WebAssembly. Each 4 ms of audio is one
   UI frame, as on a Plinky: two 64-frame audio blocks, then on_ui. The page sends touches, page and
   tempo changes, a project to load; this sends back the LEDs, the transport, and the level. At a
   context rate other than the panel's 32 kHz the panel's output is converted linearly here. */
class C12Panel extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.x = new WebAssembly.Instance(o.module, {}).exports;
    this.x.__wasm_call_ctors();
    this.ok = this.x.c12p_init() === 0;
    this.step = 32000 / sampleRate; // panel frames per output frame
    this.fifo = new Float32Array(1024 * 2);
    this.n = 0;   // frames held
    this.pos = 0; // fractional read position
    this.frames = 0;
    this.peak = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
    this.port.postMessage({ t: 'ready', ok: this.ok, rate: sampleRate });
  }

  // ASCII both ways (a project is JSON and base64): AudioWorkletGlobalScope has no TextEncoder
  text(s) {
    const m = new Uint8Array(this.x.memory.buffer, this.x.c12p_text(), s.length + 1);
    for (let i = 0; i < s.length; i++) m[i] = s.charCodeAt(i) & 127;
    m[s.length] = 0;
    return s.length;
  }
  str(p) {
    const m = new Uint8Array(this.x.memory.buffer, p);
    let s = '';
    for (let i = 0; m[i]; i++) s += String.fromCharCode(m[i]);
    return s;
  }

  onMessage(m) {
    if (!this.ok) return;
    const x = this.x;
    switch (m.t) {
      case 'touch': x.c12p_touch(m.x, m.y, m.p); break;
      case 'release_all': x.c12p_release_all(); break;
      case 'page': x.c12p_set_page(m.page); break;
      case 'tempo': x.c12p_set_tempo(m.bpm); break;
      case 'load': { const ok = x.c12p_load(this.text(m.text)) === 0; this.port.postMessage({ t: 'loaded', id: m.id, ok }); break; }
      case 'save': { const n = x.c12p_save(); this.port.postMessage({ t: 'saved', id: m.id, text: this.str(x.c12p_text()).slice(0, n) }); break; }
      case 'help': this.port.postMessage({ t: 'help', id: m.id, text: this.str(x.c12p_help(m.x, m.y)) }); break;
    }
  }

  // one panel frame: 128 frames of 32 kHz audio into the fifo
  frame() {
    const x = this.x;
    x.c12p_frame();
    const pcm = new Int16Array(x.memory.buffer, x.c12p_pcm(), 256);
    if (this.n + 128 > 1024) { this.n = 0; this.pos = 0; }
    this.fifo.set(Float32Array.from(pcm, (v) => v / 32768), this.n * 2);
    this.n += 128;
    if (++this.frames % 4 === 0) this.report();
  }

  report() {
    const x = this.x;
    const leds = new Uint32Array(x.memory.buffer, x.c12p_leds(), 256).slice();
    const status = [];
    for (let i = 0; i < 9; i++) status.push(x.c12p_status(i));
    this.port.postMessage({ t: 'state', leds, page: x.c12p_page(), playing: x.c12p_playing(), tempo: x.c12p_tempo(), status, peak: this.peak / 32768 }, [leds.buffer]);
    this.peak = 0;
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.ok || !out || !out.length) return true;
    const l = out[0], r = out[1] || out[0], f = this.fifo;
    for (let i = 0; i < l.length; i++) {
      while (this.pos + 1 >= this.n) this.frame();
      const k = Math.floor(this.pos), t = this.pos - k;
      const a = f[2 * k] + (f[2 * k + 2] - f[2 * k]) * t;
      const b = f[2 * k + 1] + (f[2 * k + 3] - f[2 * k + 1]) * t;
      l[i] = a;
      r[i] = b;
      const m = Math.max(Math.abs(a), Math.abs(b)) * 32768;
      if (m > this.peak) this.peak = m;
      this.pos += this.step;
    }
    // drop what has been read
    const used = Math.floor(this.pos);
    if (used > 0) {
      f.copyWithin(0, used * 2, this.n * 2);
      this.n -= used;
      this.pos -= used;
    }
    return true;
  }
}
registerProcessor('c12panel', C12Panel);
