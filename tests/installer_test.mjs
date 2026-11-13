// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The web installer's updater (platforms/web/installer/installer.js) against a simulated FM-1 / SMK-37: the handshake, step 1
// (a few reads, then the check's finish), the loader appearing under another port, step 2 (the whole logical image read in
// 512-byte pieces and compared with the package), the restart and the identity. Also a wrong model, and a device that stops.
//   node tests/installer_test.mjs PACKAGE.fwsc [...]
import { readFileSync } from "node:fs";
import * as I from "../platforms/web/installer/installer.js";

for (const k of Object.keys(I.DELAY)) I.DELAY[k] = Math.min(I.DELAY[k], 30);
I.DELAY.idleCheck = I.DELAY.idleWrite = 300; I.DELAY.waitLoader = I.DELAY.waitReboot = 2000; I.DELAY.poll = 20; I.DELAY.hs = 50;

const identity = (text) => {
  const d = [0x00, 0x59, 0x11, 0, 0, 0, ...Array.from({ length: 27 }, (_, i) => i < text.length ? text.charCodeAt(i) : 0), 0];
  return [0xF0, ...I.pack7(d), 0xF7];
};
const request = (addr, len, fl = 0) => {
  const u = [0x00, 0x59, 0x30, 0, 0, 0, fl, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, (len >> 8) & 0xFF, 0];
  let s = 0; for (let i = 6; i < 14; i++) s += u[i];
  u.push(~s & 0xFF);
  return [0xF0, ...I.pack7(u), 0xF7];
};
const dataOf = (pkt) => { const b = I.unpack7(pkt.slice(1, -1)); return b.slice(14, 14 + (b[11] | (b[12] << 8))); };

// a device: one port that the page sees, answering as the firmware, then as its loader, then as the new firmware
function device(model, running, product, { stopAfter = -1 } = {}) {
  const ports = { inputs: new Map(), outputs: new Map() };
  const st = { mode: "run", text: running, wrote: null, reads: 0 };
  let input, output, gen = 0;
  const plug = (name) => {
    gen++;
    const my = gen;
    input = { name, state: "connected", onmidimessage: null, open: async () => {} };
    output = { name, state: "connected", open: async () => {}, send: (b) => { if (my !== gen) throw new Error("gone"); setTimeout(() => rx(Array.from(b)), 1); } };
    ports.inputs.clear(); ports.outputs.clear();
    ports.inputs.set(name, input); ports.outputs.set(name, output);
  };
  const tx = (pkt) => setTimeout(() => input.onmidimessage && input.onmidimessage({ data: Uint8Array.from(pkt) }), 1);
  const unplug = () => { input.state = output.state = "disconnected"; ports.inputs.clear(); ports.outputs.clear(); gen++; };
  let image = null, pending = [];
  const next = () => { if (pending.length) { const q = pending.shift(); const u = I.unpack7(q.slice(1, -1)); st.at = (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0; tx(q); } };
  function rx(b) {
    if (b.length === I.HS_QUERY.length && b.every((x, i) => x === I.HS_QUERY[i])) return tx(identity(st.text));
    if (b.length === I.UPGRADE.length && b.every((x, i) => x === I.UPGRADE[i])) {
      if (st.mode === "run") { pending = [request(0, 512), request(0x4000, 256), request(I.FINISH_CHECK, 8)]; }
      else { image = []; const n = st.size; pending = []; const order = []; for (let a = 0; a < n; a += 512) order.push(a); order.sort((x, y) => ((x * 7919) % 4093) - ((y * 7919) % 4093)); for (const a of order) pending.push(request(a, Math.min(512, n - a))); pending.push(request(I.FINISH_WRITE, 8)); }
      return next();
    }
    const d = dataOf(b);
    st.reads++;
    if (stopAfter >= 0 && st.reads > stopAfter) return;   // stops answering
    if (st.mode === "ota" && image && st.at !== undefined) image[st.at] = d;
    if (pending.length) return next();
    if (st.mode === "run") {   // the check finished: restart into the loader
      unplug(); setTimeout(() => { st.mode = "ota"; st.text = `ota-${model}_1`; plug(`${model} OTA`); }, 40);
    } else {
      st.wrote = new Uint8Array(st.size); for (const k of Object.keys(image)) if (Number(k) < st.size) st.wrote.set(image[k], Number(k)); st.wrote.n = Object.keys(image).filter((k) => Number(k) < st.size).length;
      unplug(); setTimeout(() => { st.mode = "run"; st.text = product; plug(model); }, 40);
    }
  }
  plug(model);
  return { access: ports, st };
}

let bad = 0;
const ok = (c, m) => { console.log((c ? "ok   " : "FAIL ") + m); if (!c) bad++; };

for (const path of process.argv.slice(2)) {
  const raw = new Uint8Array(readFileSync(path));
  const product = I.productOf(raw), model = I.modelOf(product), image = I.logicalImage(raw);
  ok(/^[^_]+_\d+$/.test(product), `${path}: identity ${product}, ${I.blocksOf(raw)} blocks, image ${image.length} B`);

  { // a full install
    const dv = device(model, `${model}_1200`, product); dv.st.size = image.length;
    const up = new I.Updater(dv.access);
    const dev = await up.find();
    ok(dev && dev.id.text === `${model}_1200`, "found the running device");
    const steps = [], pct = [];
    const got = await up.install(dev, image, product, (s, v) => { steps.push(s); if (s === "write") pct.push(v); });
    ok(got === product, `installed, the device reports ${got}`);
    ok(dv.st.wrote && dv.st.wrote.length === image.length && dv.st.wrote.every((x, i) => x === image[i]), "the loader received the whole image, byte for byte");
    ok(["start", "loader", "write", "reboot", "done"].every((s) => steps.includes(s)), "every step reported");
    ok(pct.every((v, i) => !i || v >= pct[i - 1]) && pct[pct.length - 1] === 100, "the write's percentage only goes up, to 100");
  }
  { // the other model
    const other = model === "FM-1" ? "SMK-37 Pro" : "FM-1";
    const dv = device(other, `${other}_1`, `${other}_1`);
    const up = new I.Updater(dv.access);
    const dev = await up.find();
    try { await up.install(dev, image, product); ok(false, "a wrong model refused"); } catch (e) { ok(e.code === "model", `a wrong model refused (${e.code})`); }
  }
  { // a device that stops answering in step 1 (a non-stock bootloader's lock): nothing written
    const dv = device(model, `${model}_1200`, product, { stopAfter: 1 }); dv.st.size = image.length;
    const up = new I.Updater(dv.access);
    const dev = await up.find();
    try { await up.install(dev, image, product); ok(false, "a stop reported"); } catch (e) { ok(e.code === "stopped" && !dv.st.wrote, `a stop reported, nothing written (${e.code})`); }
  }
}
console.log(bad ? `${bad} failed` : "all passed");
process.exit(bad ? 1 : 0);
