// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Installs a firmware package (.fwsc) on an M-VAVE FM-1 or SMK-37 over WebMIDI SysEx, as
// third_party/felucca-bsp/tools/fm1_install.py does: the running firmware reads the package and starts the update loader,
// the loader reads it all and writes it, the device restarts and its identity is checked. The device asks (read requests
// on the package's logical image), the page answers.
// (The wire format and the Updater are ported from the owner's own Felucca web installer, felucca-ghp/webapp/installer,
// 2026-11-13, and extended to the SMK-37's 36-block packages.)

export const BLK = 0x30, KEEP = 0x2F, BLOCKS = 20, BLOCKS_LONG = 36;
export const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
export const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
export const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000;
const MAXDATA = 512;
const PORT_RE = /fm-1|felucca|ota|composite|sinco|usb-midi|smk|mke/i;   // never probe other gear
const IS_OTA = (id) => /^ota-/i.test(id.model);
export const modelOf = (text) => text.split("_")[0].replace(/^ota-/i, "");

// seconds in ms; the tests shorten them
export const DELAY = { start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000, poll: 1000, hs: 1000,
                       idleCheck: 8000, idleWrite: 180000, waitLoader: 30000, waitReboot: 40000 };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// errors carry a code the page explains: notfound, model, stopped, noloader, lost, noreturn, mismatch, badreq, badpkg
export function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

// ---- the package

// 36 blocks when the 21st to the 36th marker are all unused (0x7D: the SMK-37's packages), else 20
export function blocksOf(raw) {
  if (raw.length < BLOCKS_LONG * BLK) return BLOCKS;
  for (let i = BLOCKS; i < BLOCKS_LONG; i++) if (raw[i * BLK + KEEP] !== 0x7D) return BLOCKS;
  return BLOCKS_LONG;
}

// the package's identity ("FM-1_1300"): one marker byte after each block
export function productOf(raw) {
  if (raw.length < BLOCKS * BLK) throw fail("badpkg", "not a firmware package (too short)");
  let s = "";
  const n = blocksOf(raw);
  for (let i = 0; i < n; i++) {
    const m = raw[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device reads during an update: the package without its marker bytes
export function logicalImage(raw) {
  const n = blocksOf(raw), out = new Uint8Array(raw.length - n);
  for (let i = 0; i < n; i++) out.set(raw.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(raw.subarray(n * BLK), n * KEEP);
  return out;
}

export async function sha256hex(bytes) {
  const d = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

// ---- the wire format

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

export function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

export function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

export function parseIdentity(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// ---- MIDI

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  send(bytes) { try { this.output.send(bytes); return true; } catch (_) { return false; } }   // (false: unplugged)
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + DELAY.hs;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

export class Updater {
  constructor(access) { this.access = access; }

  // the first in/out pair of the same name that answers the handshake (and filter(identity))
  async find(filter) {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected" || !PORT_RE.test(input.name || "")) continue;
      const base = (input.name || "").replace(/\s+\d+$/, "");   // (Windows numbers each port)
      const output = outs.find((o) => o.name === input.name && o.state !== "disconnected") ||
                     outs.find((o) => (o.name || "").replace(/\s+\d+$/, "") === base && o.state !== "disconnected");
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = new Link(input, output);
      const id = await handshake(link, 2);
      if (id && (!filter || filter(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(DELAY.retry);
    }
    return null;
  }

  // answer read requests until the device asks for finishAddr
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, bytes = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(DELAY.poll);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `the device asked for ${r.addr.toString(16)}+${r.len}, outside the package`);
      await sleep(DELAY.reply);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) return { served, finished: false, lost: true };
      served++; bytes += r.len;
      if (onProgress) onProgress(served, bytes);   // (bytes sent: the loader reads out of order, so not the address)
    }
  }

  // running firmware -> loader -> new firmware; a device already in update mode only needs the write
  async install(dev, image, product, step = () => {}) {
    const model = modelOf(product);
    if (modelOf(dev.id.text) !== model) { dev.link.close(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`); }
    let ota = dev;
    if (!IS_OTA(dev.id)) {
      step("start", dev.id.text);
      dev.link.send(UPGRADE);
      await sleep(DELAY.start);
      const s1 = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idleCheck, (n) => step("check", n));
      dev.link.close();
      if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped answering"} after ${s1.served} requests; nothing was written`);
      step("loader");
      await sleep(DELAY.loader);
      ota = await this.waitFor((id) => IS_OTA(id) && modelOf(id.text) === model, DELAY.waitLoader);
      if (!ota) throw fail("noloader", "the update loader did not appear");
    }
    step("write", 0);
    ota.link.send(UPGRADE);
    await sleep(DELAY.start);
    const s2 = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idleWrite, (n, sent) => step("write", Math.min(99, Math.floor(sent * 100 / image.length))));
    ota.link.close();
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped answering"} after ${s2.served} requests`);
    step("write", 100);
    step("reboot");
    await sleep(DELAY.reboot);
    const back = await this.waitFor((id) => !IS_OTA(id), DELAY.waitReboot);
    if (!back) throw fail("noreturn", "the device did not come back");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `written, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }
}
