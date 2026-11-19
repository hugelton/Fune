// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Songs and presets between a Fune device (the FM-1) and a computer, over its USB serial console (Web Serial in Chrome or
// Edge; a port-like object in tests/transfer_test.mjs). The console commands are fm1_console.c's, as platforms/fm1hw/fm1_slots.py
// uses them: slots, slotget / slotdata (a slot out), songput / songdata / slotput (a file in). A .fune file is a slot's bytes
// as the device keeps them: a song (KRSG) or a preset (KRSP), checked here as the device's loader checks its header.

export const SLOTS = { song: 6, preset: 24 };
const KIND_OF = { KRSG: "song", KRSP: "preset" };

export function crc16(b) {   // CRC-16/CCITT-FALSE, the files' own (binascii.crc_hqx(.., 0xFFFF))
  let c = 0xFFFF;
  for (const x of b) { c ^= x << 8; for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF; }
  return c;
}
const CRC32T = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function crc32(b) {   // zlib's, the console's transfer check (and a zip's)
  let c = 0xFFFFFFFF;
  for (const x of b) c = CRC32T[(c ^ x) & 255] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
const r16 = (b, i) => b[i] | b[i + 1] << 8;
const hex = (b) => Array.from(b, (x) => x.toString(16).padStart(2, "0")).join("");
const err = (code, extra = {}) => Object.assign(new Error(code), { code, ...extra });

// a song or preset file as the loader checks it: { kind, name, data } (its own bytes, anything after them dropped).
// Errors (code): notfune, version, short, damaged, noengine
export function fileInfo(d) {
  const tag = String.fromCharCode(...d.slice(0, 4));
  if (d.length < 10 || !KIND_OF[tag]) throw err("notfune");
  if (r16(d, 4) !== 1) throw err("version");
  const end = 8 + r16(d, 6);
  if (end + 2 > d.length) throw err("short");
  if (crc16(d.subarray(0, end)) !== r16(d, end)) throw err("damaged");
  let pos = 8, name = "", trak = false;
  while (pos < end) {
    if (end - pos < 6) throw err("damaged");
    const t = String.fromCharCode(...d.slice(pos, pos + 4)), n = r16(d, pos + 4);
    if (pos + 6 + n > end) throw err("damaged");
    if (t === "NAME") name = String.fromCharCode(...d.slice(pos + 6, pos + 6 + n)).replace(/\0.*$/s, "").trim();
    if (t === "TRAK" && n >= 9) trak = true;
    pos += 6 + n;
  }
  const kind = KIND_OF[tag];
  if (kind === "preset" && !trak) throw err("noengine");
  return { kind, name, data: d.slice(0, end + 2) };
}

// a file name for a slot's file: its name, else the kind and number
export function fileName(kind, n, name) {
  const s = (name || "").replace(/[^A-Za-z0-9.-]+/g, "_").replace(/^_+|_+$/g, "");
  return (s || `${kind}${n}`) + ".fune";
}

// The device's console on a port: { open(opts), close(), readable, writable, setSignals? } (a Web Serial SerialPort).
// One command at a time; each has a one-line reply.
export class Console {
  constructor(port) { this.port = port; this.buf = ""; this.queue = Promise.resolve(); this.closed = false; }
  async open() {
    await this.port.open({ baudRate: 115200 });
    if (this.port.setSignals) await this.port.setSignals({ dataTerminalReady: true });   // (the FM-1 speaks only while DTR is up)
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.pump = (async () => {
      const dec = new TextDecoder();
      try {
        for (;;) { const { value, done } = await this.reader.read(); if (done) break; this.buf += dec.decode(value, { stream: true }); }
      } catch (_) { /* closed or unplugged */ }
      this.closed = true;
    })();
    await new Promise((r) => setTimeout(r, 150));
    this.buf = "";   // (whatever the device said before: not a reply)
  }
  async close() {
    this.closed = true;
    try { await this.reader.cancel(); } catch (_) {}
    try { this.reader.releaseLock(); } catch (_) {}
    try { this.writer.releaseLock(); } catch (_) {}
    try { await this.port.close(); } catch (_) {}
  }
  ask(cmd, timeout = 1500) {   // the reply line; throws code "noreply" when none comes
    const run = async () => {
      this.buf = "";
      await this.writer.write(new TextEncoder().encode(cmd + "\r"));
      const until = Date.now() + timeout;
      while (Date.now() < until && !this.closed) {
        const nl = this.buf.indexOf("\n");
        if (nl >= 0) {
          const line = this.buf.slice(0, nl).trim();
          this.buf = this.buf.slice(nl + 1);
          if (line) return line;
          continue;
        }
        await new Promise((r) => setTimeout(r, 5));
      }
      throw err(this.closed ? "lost" : "noreply", { cmd });
    };
    const p = this.queue.then(run, run);
    this.queue = p.catch(() => {});
    return p;
  }
}

// every slot: { song: [name|null x6], preset: [name|null x24] } (null empty, "" no name)
export async function listSlots(con) {
  const r = await con.ask("slots");
  if (!r.startsWith("ok")) throw err("notfune-device", { reply: r });
  const out = {};
  for (const [kind, s] of [["song", "s"], ["preset", "p"]]) {
    out[kind] = [];
    for (let n = 1; n <= SLOTS[kind]; n++) {
      const m = r.match(new RegExp(`\\b${s}${n}=(\\S+)`));
      const v = m ? m[1] : "-";
      out[kind].push(v === "-" ? null : v === "*" ? "" : v.replace(/_/g, " "));
    }
  }
  return out;
}

// a slot's file out of the device: { kind, name, data }. progress(0..1)
export async function pull(con, kind, n, progress = () => {}) {
  let r = await con.ask(`slotget ${kind} ${n}`, 3000);
  if (r.startsWith("err slotget empty")) throw err("empty");
  let m = r.match(/^ok bytes=(\d+) crc=0x([0-9A-Fa-f]{8})/);
  if (!m) throw err("device", { reply: r });
  const total = +m[1], crc = parseInt(m[2], 16), data = new Uint8Array(total);
  let got = 0;
  while (got < total) {
    r = await con.ask(`slotdata ${got}`);
    m = r.match(/^ok (\d+) ([0-9A-Fa-f]+)$/);
    if (!m || +m[1] !== got || m[2].length % 2) throw err("device", { reply: r });
    for (let i = 0; i < m[2].length && got < total; i += 2) data[got++] = parseInt(m[2].slice(i, i + 2), 16);
    progress(got / total);
  }
  if (crc32(data) !== crc) throw err("garbled");
  const f = fileInfo(data);
  if (f.kind !== kind) throw err("wrongkind", { kind: f.kind });
  return f;
}

// a file into slot n (its kind): the device checks it again and writes it. Errors: playing, garbled, refused, wrongkind
export async function push(con, kind, n, file, progress = () => {}) {
  const f = fileInfo(file);
  if (f.kind !== kind) throw err("wrongkind", { kind: f.kind });
  const data = f.data;
  let r = await con.ask(`songput ${data.length}`);
  if (r !== "ok") throw err(r.startsWith("err songput size") ? "toobig" : "device", { reply: r });
  for (let i = 0; i < data.length; i += 24) {   // (a console line holds 64 characters)
    r = await con.ask("songdata " + hex(data.subarray(i, i + 24)));
    if (r !== `ok n=${Math.min(i + 24, data.length)}`) throw err("device", { reply: r });
    progress(Math.min(i + 24, data.length) / data.length * 0.9);
  }
  r = await con.ask(`slotput ${kind} ${n} 0x${crc32(data).toString(16).toUpperCase().padStart(8, "0")}`, 6000);   // (an erase and a write)
  if (r.startsWith("err slotput playing")) throw err("playing");
  if (r.startsWith("err slotput crc")) throw err("garbled");
  if (!r.startsWith("ok")) throw err("refused", { reply: r });
  progress(1);
  return f;
}

// a zip of files [{ name, data }], stored (no compression: the files are small and already dense)
export function zip(files, date = new Date()) {
  const enc = new TextEncoder(), parts = [], dir = [];
  const t = (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1);
  const d = ((date.getFullYear() - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate();
  let off = 0;
  const u16 = (v) => [v & 255, (v >> 8) & 255], u32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
  for (const f of files) {
    const name = enc.encode(f.name), c = crc32(f.data), n = f.data.length;
    const common = [...u16(20), ...u16(0), ...u16(0), ...u16(t), ...u16(d), ...u32(c), ...u32(n), ...u32(n), ...u16(name.length), ...u16(0)];
    const local = Uint8Array.from([0x50, 0x4B, 3, 4, ...common]);
    parts.push(local, name, f.data);
    dir.push(Uint8Array.from([0x50, 0x4B, 1, 2, ...u16(20), ...common, ...u16(0), ...u16(0), ...u16(0), ...u32(0), ...u32(off)]), name);
    off += local.length + name.length + n;
  }
  const size = dir.reduce((s, p) => s + p.length, 0);
  const end = Uint8Array.from([0x50, 0x4B, 5, 6, 0, 0, 0, 0, ...u16(files.length), ...u16(files.length), ...u32(size), ...u32(off), 0, 0]);
  const all = [...parts, ...dir, end], out = new Uint8Array(all.reduce((s, p) => s + p.length, 0));
  let p = 0;
  for (const a of all) { out.set(a, p); p += a.length; }
  return out;
}
