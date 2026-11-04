/* CLATTER 12 on the web: the engine inside an AudioWorklet.
   Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

   Hosts docs/assets/c12.wasm: the LXR-02 engine, converter and sequencer of the Plinky panel, built
   for WebAssembly (core/c12_wasm.c). The page sends it what the panel's UI would call (cells, pages,
   tempo, kit, play, trigger) and it renders 128-frame quanta. It reports the sequencer's position, and
   its peak level so the page can show that sound is flowing. Nothing here is Plinky code. */
class C12Processor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions;
    this.x = new WebAssembly.Instance(o.module, {}).exports;
    this.ok = this.x.c12w_init(sampleRate, 0x12c1a77e) === 0;
    this.pos = -2;
    this.peak = 0;
    this.tick = 0;
    this.port.onmessage = e => this.onMessage(e.data);
    this.port.postMessage({ t: 'ready', ok: this.ok, rate: sampleRate });
  }

  onMessage(m) {
    if (!this.ok) return;
    const x = this.x;
    switch (m.t) {
      case 'pattern': // the whole pattern as the sequencer's 32-bit cells
        new Uint32Array(x.memory.buffer, x.c12w_io(), 448).set(new Uint32Array(m.cells));
        x.c12w_commit(m.pages);
        break;
      case 'cell': x.c12w_cell(m.track, m.step, m.cell >>> 0); break;
      case 'pages': x.c12w_pages(m.n); break;
      case 'tempo': x.c12w_tempo(m.bpm); break;
      case 'play': x.c12w_play(m.on ? 1 : 0); break;
      case 'kit': x.c12w_kit(m.character, m.seed >>> 0, m.wild); break;
      case 'trigger': x.c12w_trigger(m.track, m.velocity, m.variation); break;
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.ok || !out || out.length < 1) return true;
    const n = out[0].length;
    if (n > 128) return true;
    const x = this.x;
    x.c12w_render(n);
    const pcm = new Int16Array(x.memory.buffer, x.c12w_pcm(), n * 2);
    const l = out[0], r = out[1] || out[0];
    let pk = 0;
    for (let i = 0; i < n; i++) {
      const a = pcm[2 * i], b = pcm[2 * i + 1];
      l[i] = a / 32768;
      r[i] = b / 32768;
      const m = a < 0 ? -a : a;
      if (m > pk) pk = m;
    }
    if (pk > this.peak) this.peak = pk;
    const pos = x.c12w_position();
    if (pos !== this.pos) { this.pos = pos; this.port.postMessage({ t: 'pos', pos }); }
    if (++this.tick % 12 === 0) { this.port.postMessage({ t: 'level', peak: this.peak / 32768 }); this.peak = 0; }
    return true;
  }
}
registerProcessor('c12', C12Processor);
