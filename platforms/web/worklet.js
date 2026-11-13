// SPDX-License-Identifier: MPL-2.0
// The AudioWorklet: Kreisel's audio (kreisel_audio_web.c, a standalone WebAssembly module) renders here, on the audio thread.
// The page posts { wasm } once (bytes, no SharedArrayBuffer needed), then { app, notes } (the core's KApp as bytes, and live
// notes [track, note, vel] in order) whenever something changed. The worklet answers { step } so the page's playhead is the
// sample clock's.
class KreiselProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.x = null; this.n = 0; this.lastStep = -2;
    this.port.onmessage = ({ data }) => {
      if (data.wasm) {
        WebAssembly.instantiate(data.wasm, {}).then(({ instance }) => {
          const x = instance.exports;
          if (x._initialize) x._initialize();
          x.ka_init(sampleRate);
          this.x = x; this.port.postMessage({ ready: true });
        }).catch(e => this.port.postMessage({ error: String(e) }));
        return;
      }
      const x = this.x; if (!x) return;
      if (data.app) {
        const src = new Uint8Array(data.app);
        if (src.length === x.ka_app_size()) { new Uint8Array(x.memory.buffer, x.ka_app_ptr(), src.length).set(src); x.ka_sync(); }
      }
      if (data.notes) for (const [t, n, v] of data.notes) x.ka_note(t, n, v, v > 0 ? 1 : 0);
    };
  }
  process(inputs, outputs) {
    const x = this.x, o = outputs[0];
    if (!x) return true;
    const L = o[0], R = o[1] || o[0], n = L.length;
    const p = x.ka_render(n) >>> 2, f = new Float32Array(x.memory.buffer, p * 4, n * 2);
    for (let i = 0; i < n; i++) { L[i] = f[2 * i]; if (R !== L) R[i] = f[2 * i + 1]; }
    if ((this.n++ & 3) === 0) {   // ~ every 11 ms
      const s = x.ka_playing() ? x.ka_step() : -1;
      if (s !== this.lastStep) { this.lastStep = s; this.port.postMessage({ step: s }); }
    }
    return true;
  }
}
registerProcessor('kreisel', KreiselProcessor);
