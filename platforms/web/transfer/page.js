// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The transfer page's behaviour (index.html beside it): connect, the slots, a slot to a file, a file to a slot, all to a zip.
// <html lang="ja"> gives Japanese, any other lang English. The page owns the look; this script every string it writes.
import { Console, SLOTS, listSlots, pull, push, zip, fileName, fileInfo } from "./transfer.js";

const LANG = (document.documentElement.lang || "").toLowerCase().startsWith("ja") ? "ja" : "en";
const TEXT = {
  en: {
    none: "Not connected", looking: "Connecting…", connected: "connected", lost: "Disconnected. Connect again.",
    noport: "No port chosen.", notdevice: "That port is not a Fune FM-1. Connect again and pick the FM-1.",
    empty: "(empty)", noname: "(no name)", download: "Download", replace: "Replace", load: "Load",
    used: (n, all) => `${n} of ${all}`, showempty: "Show empty slots", hideempty: "Hide empty slots",
    confirm: (kind, n, name) => `Replace ${kind} ${n}${name ? ` (${name})` : ""}? It can't be undone.`,
    pulled: (f) => `Saved ${f}`, pushed: (kind, n, name) => `Loaded ${name || "the file"} into ${kind} ${n}`,
    backedup: (n, f) => `Backed up ${n} file${n === 1 ? "" : "s"} to ${f}`, nothing: "Nothing to back up.",
    reading: (kind, n) => `Reading ${kind} ${n}…`, writing: (kind, n) => `Writing ${kind} ${n}…`,
    kind: { song: "song", preset: "preset" },
    err: {
      notfune: "Not a Fune song or preset.", version: "Made by a newer Fune. Update the FM-1 first.", short: "The file is cut short.",
      damaged: "The file is damaged.", noengine: "A preset with no engine in it.", wrongkind: (k) => `That file is a ${k}. Drop it on a ${k} slot.`,
      playing: "The FM-1 is playing. Stop it, then try again.", garbled: "The data came over damaged. Try again.",
      refused: "The FM-1 refused the file.", toobig: "The file is too big for the FM-1.", noreply: "The FM-1 did not answer. Replug it and connect again.",
      device: "The FM-1 gave an unexpected answer. Try again.",
    },
  },
  ja: {
    none: "未接続", looking: "接続しています…", connected: "接続中", lost: "接続が切れました。もう一度接続してください。",
    noport: "ポートが選ばれませんでした。", notdevice: "そのポートは Fune の FM-1 ではありません。もう一度接続して FM-1 を選んでください。",
    empty: "（空き）", noname: "（名前なし）", download: "保存", replace: "置き換え", load: "読み込み",
    used: (n, all) => `${n} / ${all}`, showempty: "空きスロットも表示", hideempty: "空きスロットを隠す",
    confirm: (kind, n, name) => `${kind} ${n}${name ? `（${name}）` : ""}を置き換えますか？元に戻せません。`,
    pulled: (f) => `${f} を保存しました`, pushed: (kind, n, name) => `${name || "ファイル"}を ${kind} ${n} に読み込みました`,
    backedup: (n, f) => `${n} 個を ${f} にバックアップしました`, nothing: "バックアップするものがありません。",
    reading: (kind, n) => `${kind} ${n} を読んでいます…`, writing: (kind, n) => `${kind} ${n} に書いています…`,
    kind: { song: "ソング", preset: "プリセット" },
    err: {
      notfune: "Fune のソングでもプリセットでもありません。", version: "新しい Fune で作られています。先に FM-1 を更新してください。",
      short: "ファイルが途中で切れています。", damaged: "ファイルが壊れています。", noengine: "エンジンの入っていないプリセットです。",
      wrongkind: (k) => `このファイルは${TEXT.ja.kind[k]}です。${TEXT.ja.kind[k]}のスロットにドロップしてください。`,
      playing: "FM-1 が再生中です。止めてから、もう一度お試しください。", garbled: "データが壊れて届きました。もう一度お試しください。",
      refused: "FM-1 がファイルを受け付けませんでした。", toobig: "FM-1 には大きすぎるファイルです。",
      noreply: "FM-1 が応答しません。挿し直して、もう一度接続してください。", device: "FM-1 から想定外の応答がありました。もう一度お試しください。",
    },
  },
};
const T = TEXT[LANG];
const $ = (id) => document.getElementById(id);

let con = null, slots = null, busy = false, showEmpty = false, pushTo = null;

function status(text, cls = "") { $("status").textContent = text; $("status").className = cls; }
function errText(e) {
  const m = T.err[e.code];
  return typeof m === "function" ? m(e.kind) : m || e.message;
}
async function run(f) {
  if (busy || !con) return;
  busy = true; enable();
  try { await f(); } catch (e) {
    if (e.code === "lost") { disconnected(); status(T.lost, "bad"); } else status(errText(e), "bad");
  }
  busy = false; enable();
}
function enable() {
  document.querySelectorAll("ol.slots button").forEach((b) => { b.disabled = busy; });
  $("backup").disabled = busy || !con || !slots || ![...slots.song, ...slots.preset].some((s) => s !== null);
  $("connect").disabled = busy;
}
function save(name, data, type = "application/octet-stream") {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([data], { type }));
  a.download = name;
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function draw() {
  for (const kind of ["song", "preset"]) {
    const ol = $(kind), list = slots[kind];
    ol.textContent = "";
    list.forEach((name, i) => {
      const n = i + 1, empty = name === null;
      if (kind === "preset" && empty && !showEmpty) return;
      const li = document.createElement("li");
      li.className = empty ? "empty" : "";
      li.dataset.kind = kind; li.dataset.n = n;
      const num = document.createElement("span"); num.className = "n"; num.textContent = n;
      const nm = document.createElement("span"); nm.className = "name"; nm.textContent = empty ? T.empty : name || T.noname;
      const acts = document.createElement("span"); acts.className = "acts";
      if (!empty) {
        const d = document.createElement("button"); d.className = "ghost"; d.textContent = T.download;
        d.onclick = () => run(() => doPull(kind, n, li));
        acts.append(d);
      }
      const u = document.createElement("button"); u.className = "ghost"; u.textContent = empty ? T.load : T.replace;
      u.onclick = () => { if (!busy) { pushTo = { kind, n, li }; $("file").value = ""; $("file").click(); } };
      acts.append(u);
      li.append(num, nm, acts);
      li.ondragover = (ev) => { ev.preventDefault(); li.classList.add("drop"); };
      li.ondragleave = () => li.classList.remove("drop");
      li.ondrop = (ev) => {
        ev.preventDefault(); li.classList.remove("drop");
        const f = ev.dataTransfer.files[0];
        if (f) loadFile(f, { kind, n, li });
      };
      ol.append(li);
    });
    $(kind + "count").textContent = T.used(list.filter((s) => s !== null).length, SLOTS[kind]);
  }
  const anyEmpty = slots.preset.some((s) => s === null);
  $("allpresets").hidden = !anyEmpty;
  $("allpresets").textContent = showEmpty ? T.hideempty : T.showempty;
  $("songs").hidden = $("presets").hidden = false;
  enable();
}
function progress(li) { return (v) => { li.classList.add("busy"); li.querySelector(".name").dataset.pct = Math.round(v * 100) + "%"; }; }
function done(li) { li.classList.remove("busy"); }

async function refresh() { slots = await listSlots(con); draw(); }

async function doPull(kind, n, li) {
  status(T.reading(T.kind[kind], n));
  try {
    const f = await pull(con, kind, n, progress(li));
    const name = fileName(kind, n, f.name);
    save(name, f.data);
    status(T.pulled(name), "ok");
  } finally { done(li); }
}

async function loadFile(file, to) {
  if (busy || !con) return;
  let f;
  try { f = fileInfo(new Uint8Array(await file.arrayBuffer())); } catch (e) { status(errText(e), "bad"); return; }
  if (f.kind !== to.kind) { status(errText({ code: "wrongkind", kind: T.kind[f.kind] }), "bad"); return; }
  const old = slots[to.kind][to.n - 1];
  if (old !== null && !confirm(T.confirm(T.kind[to.kind], to.n, old))) return;
  await run(async () => {
    status(T.writing(T.kind[to.kind], to.n));
    try {
      await push(con, to.kind, to.n, f.data, progress(to.li));
      await refresh();
      status(T.pushed(T.kind[to.kind], to.n, f.name), "ok");
    } finally { done(to.li); }
  });
}
$("file").onchange = () => { const f = $("file").files[0]; if (f && pushTo) loadFile(f, pushTo); };

$("backup").onclick = () => run(async () => {
  const files = [];
  for (const kind of ["song", "preset"])
    for (let n = 1; n <= SLOTS[kind]; n++) {
      if (slots[kind][n - 1] === null) continue;
      status(T.reading(T.kind[kind], n));
      const f = await pull(con, kind, n);
      files.push({ name: `${kind}s/${String(n).padStart(2, "0")}-${fileName(kind, n, f.name)}`, data: f.data });
    }
  if (!files.length) { status(T.nothing); return; }
  const d = new Date(), stamp = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
  const name = `fune-fm1-${stamp}.zip`;
  save(name, zip(files, d), "application/zip");
  status(T.backedup(files.length, name), "ok");
});

$("allpresets").onclick = () => { showEmpty = !showEmpty; draw(); };

function disconnected() {
  con = null; slots = null;
  $("found").textContent = T.none;
  $("songs").hidden = $("presets").hidden = true;
  enable();
}

$("connect").onclick = async () => {
  if (busy) return;
  if (con) { await con.close(); disconnected(); }
  let port;
  try { port = await navigator.serial.requestPort(); } catch (_) { status(T.noport, "bad"); return; }
  $("found").textContent = T.looking;
  status("");
  busy = true; enable();
  try {
    con = new Console(port);
    await con.open();
    slots = await listSlots(con);
    $("found").innerHTML = "";
    const d = document.createElement("span"); d.className = "dev"; d.textContent = "FM-1";
    $("found").append(d, " " + T.connected);
    busy = false;
    draw();
  } catch (e) {
    if (con) await con.close();
    disconnected();
    status(e.code === "noreply" || e.code === "notfune-device" ? T.notdevice : errText(e), "bad");
  }
  busy = false; enable();
};

if (navigator.serial) navigator.serial.addEventListener("disconnect", (ev) => { if (con && ev.target === con.port) { disconnected(); status(T.lost, "bad"); } });
if (!navigator.serial || !window.isSecureContext) { $("noserial").hidden = false; $("s1").hidden = true; }
$("found").textContent = T.none;
