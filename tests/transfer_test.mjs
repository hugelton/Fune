// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The transfer page's console code (platforms/web/transfer/transfer.js) against a simulated FM-1 console (fm1_console.c's
// slots, slotget, slotdata, songput, songdata, slotput): the list, a pull, a push and its round trip, a file of the wrong kind,
// a refusal while playing, a garbled line, a zip of everything.
//   node tests/transfer_test.mjs SONG.krs
//   node tests/transfer_test.mjs SONG.krs --port /dev/cu.usbmodem...   the real FM-1 instead (through ~/fm1-venv's pyserial):
//        reads every slot, and only with --write N also writes SONG.krs into song slot N and reads it back
import { readFileSync } from "node:fs";
import { spawn } from "node:child_process";
import { homedir } from "node:os";
import * as X from "../platforms/web/transfer/transfer.js";

const args = process.argv.slice(2);
const song = new Uint8Array(readFileSync(args[0]));
const portArg = args.includes("--port") ? args[args.indexOf("--port") + 1] : null;
const writeArg = args.includes("--write") ? +args[args.indexOf("--write") + 1] : 0;
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };
const throwsCode = async (f, code, what) => {
  try { await f(); check(false, `${what}: no error`); } catch (e) { check(e.code === code, `${what} (${e.code})`); }
};
const hex = (b) => Array.from(b, (x) => x.toString(16).padStart(2, "0").toUpperCase()).join("");

// a port as Web Serial gives it, the far end a function that answers each line
function simPort(answer) {
  let ctl;
  const readable = new ReadableStream({ start(c) { ctl = c; } });
  const enc = new TextEncoder();
  let line = "";
  const writable = new WritableStream({
    write(chunk) {
      line += new TextDecoder().decode(chunk);
      let i;
      while ((i = line.indexOf("\r")) >= 0) {
        const cmd = line.slice(0, i); line = line.slice(i + 1);
        const r = answer(cmd);
        if (r !== null) setTimeout(() => ctl.enqueue(enc.encode(r + "\r\n")), 1);
      }
    },
  });
  return { readable, writable, open: async () => {}, close: async () => { try { ctl.close(); } catch (_) {} }, setSignals: async () => {} };
}

// the console's slot commands, over its own store
function simDevice() {
  const st = { song: Array(6).fill(null), preset: Array(24).fill(null), playing: false, garble: false, up: null, out: null };
  st.song[0] = song;
  const nameOf = (d) => { try { return X.fileInfo(d).name; } catch (_) { return ""; } };
  const answer = (cmd) => {
    const w = cmd.split(" ");
    if (w[0] === "slots") {
      const one = (k, s) => st[k].map((d, i) => `${s}${i + 1}=${d === null ? "-" : nameOf(d).replace(/ /g, "_") || "*"}`).join(" ");
      return `ok ${one("song", "s")} ${one("preset", "p")}`;
    }
    if (w[0] === "slotget") {
      const d = st[w[1]][+w[2] - 1];
      if (!d) return "err slotget empty";
      st.out = d;
      return `ok bytes=${d.length} crc=0x${X.crc32(d).toString(16).toUpperCase().padStart(8, "0")}`;
    }
    if (w[0] === "slotdata") {
      const off = +w[1], piece = st.out.slice(off, off + 64);
      if (st.garble && off === 0) { st.garble = false; piece[3] ^= 1; }
      return `ok ${off} ${hex(piece)}`;
    }
    if (w[0] === "songput") { st.up = { want: +w[1], b: [] }; return +w[1] > 3072 ? "err songput size" : "ok"; }
    if (w[0] === "songdata") {
      for (let i = 0; i < w[1].length; i += 2) st.up.b.push(parseInt(w[1].slice(i, i + 2), 16));
      return `ok n=${st.up.b.length}`;
    }
    if (w[0] === "slotput") {
      const d = Uint8Array.from(st.up.b);
      if (X.crc32(d) !== parseInt(w[3], 16)) return "err slotput crc";
      if (st.playing) return "err slotput playing (stop first)";
      st[w[1]][+w[2] - 1] = d;
      return `ok slot=${w[2]} bytes=${d.length} name=${nameOf(d)}`;
    }
    return "err ?";
  };
  return { st, port: simPort(answer) };
}

// the real FM-1's console, through a pyserial bridge (stdin to the port, the port to stdout)
function realPort(path) {
  const py = `${homedir()}/fm1-venv/bin/python`;
  const code = `
import os, sys, threading, serial
s = serial.Serial(sys.argv[1], 115200, timeout=0.05); s.dtr = True
def up():
    while True:
        b = os.read(0, 4096)
        if not b: os._exit(0)
        s.write(b)
threading.Thread(target=up, daemon=True).start()
while True:
    b = s.read(4096)
    if b: os.write(1, b)
`;
  let child, ctl;
  return {
    open: async () => {
      child = spawn(py, ["-c", code, path], { stdio: ["pipe", "pipe", "inherit"] });
      child.stdout.on("data", (b) => ctl && ctl.enqueue(new Uint8Array(b)));
      await new Promise((r) => setTimeout(r, 400));
    },
    readable: new ReadableStream({ start(c) { ctl = c; } }),
    writable: new WritableStream({ write(chunk) { child.stdin.write(Buffer.from(chunk)); } }),
    close: async () => { try { ctl.close(); } catch (_) {} child.kill(); },
  };
}

if (!portArg) {
  const { st, port } = simDevice();
  const con = new X.Console(port);
  await con.open();
  const name = X.fileInfo(song).name;
  let s = await X.listSlots(con);
  check(s.song[0] === name && s.song[1] === null && s.preset.length === 24, `list: song 1 ${s.song[0]}`);
  const f = await X.pull(con, "song", 1);
  check(hex(f.data) === hex(song), `pull song 1: ${f.data.length} bytes, the same`);
  await throwsCode(() => X.pull(con, "song", 2), "empty", "pull an empty slot");
  st.garble = true;
  await throwsCode(() => X.pull(con, "song", 1), "garbled", "a garbled line is caught");
  await X.push(con, "song", 4, song);
  s = await X.listSlots(con);
  check(s.song[3] === name && hex(st.song[3]) === hex(song), "push song 4: kept as sent");
  await throwsCode(() => X.push(con, "preset", 1, song), "wrongkind", "a song into a preset slot");
  st.playing = true;
  await throwsCode(() => X.push(con, "song", 5, song), "playing", "refused while playing");
  st.playing = false;
  const bad = song.slice(); bad[12] ^= 0x55;
  await throwsCode(() => X.push(con, "song", 5, bad), "damaged", "a damaged file is not sent");
  await throwsCode(() => X.push(con, "song", 5, Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])), "notfune", "not a Fune file");
  const extra = new Uint8Array(song.length + 5); extra.set(song);
  await X.push(con, "song", 6, extra);
  check(st.song[5].length === song.length, "trailing bytes dropped before sending");
  check(X.fileName("song", 3, "MY SONG!") === "MY_SONG.fune" && X.fileName("preset", 7, "") === "preset7.fune", "file names");
  const z = X.zip([{ name: "songs/01-A.fune", data: song }, { name: "songs/04-B.fune", data: song }], new Date(2026, 10, 15, 10, 0, 0));
  const eocd = z.length - 22;
  check(z[0] === 0x50 && z[1] === 0x4B && z[eocd] === 0x50 && z[eocd + 10] === 2, `zip: ${z.length} bytes, 2 entries`);
  if (process.env.ZIP_OUT) (await import("node:fs")).writeFileSync(process.env.ZIP_OUT, z);
  await con.close();
} else {
  const con = new X.Console(realPort(portArg));
  await con.open();
  const s = await X.listSlots(con);
  console.log("songs:", s.song.map((v) => v ?? "-").join(" | "));
  console.log("presets:", s.preset.map((v) => v ?? "-").join(" | "));
  for (const kind of ["song", "preset"])
    for (let n = 1; n <= X.SLOTS[kind]; n++)
      if (s[kind][n - 1] !== null) {
        const t = Date.now(), f = await X.pull(con, kind, n);
        check(f.kind === kind, `pull ${kind} ${n}: ${f.name || "(no name)"}, ${f.data.length} bytes, ${Date.now() - t} ms`);
      }
  if (writeArg) {
    const t = Date.now();
    await X.push(con, "song", writeArg, song);
    const back = await X.pull(con, "song", writeArg);
    check(hex(back.data) === hex(X.fileInfo(song).data), `push song ${writeArg} and read it back (${Date.now() - t} ms)`);
  }
  await con.close();
}
console.log(fails ? `${fails} failed` : "all passed");
process.exit(fails ? 1 : 0);
