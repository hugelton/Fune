// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The installer page's behaviour, for any page that loads it: <script type="module" src=".../page.js"></script>.
// The page owns the look and the static text; this script owns the installing and every string it writes.
//
// Language: <html lang="ja"> gives Japanese, any other lang English.
// Files: packages.json and the .fwsc packages are fetched from the folder this script is in (import.meta.url), so a page
// in another folder (e.g. /en/fune/install/) can load /fune/install/page.js and use the same packages.
//
// The element ids this script uses (a fixed contract with the page):
//   find     <button>  connect: asks for MIDI access and looks for the device. Label it "Connect" / 「接続」.
//   found    <p>       start hidden. Shows "Looking…", the device found (+ an ALPHA badge: <span class="badge">) or why not.
//   s2       <div>     start hidden. Holds agree, go, bar and status; shown once a device is found.
//   agree    <input type="checkbox">  "At my own risk": go stays disabled until it is ticked.
//   go       <button>  start disabled. Installs. Label it "Install" / 「インストール」 (the messages name these two buttons).
//   bar      <progress max="100">     start hidden. The progress.
//   status   <div role="status" aria-live="polite">  the progress text and the result; class "ok" or "bad" is set on it.
//   steps    <ol>      three <li> (Connect, Install, Play): this script sets each li's class to "done", "on" or "".
//   ver      <span>    inside the page's alpha sentence: replaced by "an alpha (0.0.1)" / 「アルファ版（0.0.1）」 from packages.json.
//   nomidi   <section> start hidden. Shown, and s1 hidden, where WebMIDI is missing (not Chrome or Edge, or not HTTPS).
//   s1       <section> holds find, found and s2.
// Classes used: span.dev (the model's name inside found), span.badge (ALPHA).
import { Updater, productOf, logicalImage, modelOf, sha256hex } from "./installer.js";

const BASE = new URL(".", import.meta.url);
const LANG = (document.documentElement.lang || "").toLowerCase().startsWith("ja") ? "ja" : "en";

const TEXT = {
  en: {
    notfound: "No device. Check the cable, close other MIDI apps, Connect again.",
    model: "Wrong package for this device.",
    stopped: "The device stopped. Nothing written. Replug and try again.",
    lost: "Disconnected. Replug and press Install again.",
    noloader: "Replug and press Install again.",
    noreturn: "Turn the device off and on.",
    mismatch: "Written, but not confirmed. Try once more.",
    badreq: "Stopped: an unexpected request.",
    badpkg: "Not a package for this device.",
    download: "Download failed. Reload and try again.",
    nopackage: "No package for this device.",
    looking: "Looking…",
    allow: "Allow MIDI access, then Connect again.",
    build: (n) => ` → build ${n}`,
    downloading: "Downloading…",
    checking: "Checking…",
    starting: "Starting…",
    writing: (v) => `Writing… ${v}%`,
    restarting: "Restarting…",
    done: "Done. Enjoy.",
    version: (v) => `an alpha (${v})`,
  },
  ja: {
    notfound: "機器が見つかりません。ケーブルを確かめ、ほかのMIDIアプリを閉じてから、もう一度接続してください。",
    model: "この機器用のパッケージではありません。",
    stopped: "機器が応答しなくなりました。何も書き込まれていません。挿し直して、もう一度お試しください。",
    lost: "接続が切れました。挿し直して、もう一度インストールを押してください。",
    noloader: "挿し直して、もう一度インストールを押してください。",
    noreturn: "機器の電源を入れ直してください。",
    mismatch: "書き込みましたが、確認できませんでした。もう一度お試しください。",
    badreq: "想定外の要求があったため止めました。",
    badpkg: "この機器用のパッケージではありません。",
    download: "ダウンロードに失敗しました。ページを再読み込みして、もう一度お試しください。",
    nopackage: "この機器用のパッケージがありません。",
    looking: "探しています…",
    allow: "MIDIへのアクセスを許可してから、もう一度接続してください。",
    build: (n) => ` → ビルド${n}`,
    downloading: "ダウンロードしています…",
    checking: "確認しています…",
    starting: "準備しています…",
    writing: (v) => `書き込んでいます… ${v}%`,
    restarting: "再起動しています…",
    done: "完了しました。お楽しみください。",
    version: (v) => `アルファ版（${v}）`,
  },
};
const T = TEXT[LANG];

const $ = (id) => document.getElementById(id);
function stepTo(n) { [...$("steps").children].forEach((li, i) => { li.className = i < n ? "done" : i === n ? "on" : ""; }); }
function status(text, cls = "") { $("status").textContent = text; $("status").className = cls; }

let access = null, updater = null, dev = null, packages = [], chosen = null, busy = false;

async function loadList() {
  try {
    const r = await fetch(new URL("packages.json", BASE), { cache: "no-cache" });
    if (r.ok && (r.headers.get("content-type") || "").includes("json")) packages = await r.json();
  } catch (_) { packages = []; }
  const v = packages.find((p) => p.version);
  if (v && $("ver")) $("ver").textContent = T.version(v.version.replace(/\s*alpha$/i, ""));
}

// a package from the server, its size, digest and identity checked (a missing file can come back as the site's home page)
async function fetchPackage(p) {
  const r = await fetch(new URL(p.file, BASE), { cache: "no-cache" });
  const raw = new Uint8Array(await r.arrayBuffer());
  if (!r.ok || raw.length !== p.size || await sha256hex(raw) !== p.sha256 || productOf(raw) !== p.product)
    throw Object.assign(new Error(T.download), { code: "download" });
  return raw;
}

function showPackage() {
  if (!chosen) { status(T.nopackage, "bad"); $("go").disabled = true; return; }
  $("go").disabled = busy || !$("agree").checked;
}

$("find").onclick = async () => {
  if (busy) return;
  $("found").hidden = false;
  $("found").textContent = T.looking;
  try {
    if (!access) access = await navigator.requestMIDIAccess({ sysex: true });
  } catch (_) {
    $("found").textContent = T.allow;
    return;
  }
  updater = updater || new Updater(access);
  if (dev) { dev.link.close(); dev = null; }
  dev = await updater.find();
  if (!dev) { $("found").textContent = T.notfound; $("s2").hidden = true; return; }
  const model = modelOf(dev.id.text);
  $("found").innerHTML = "";
  const d = document.createElement("span"); d.className = "dev"; d.textContent = model;
  if (!chosen || modelOf(chosen.product) !== model) chosen = packages.find((p) => modelOf(p.product) === model) || null;
  $("found").append(d, chosen ? T.build(chosen.product.split("_").pop()) : "");
  if (chosen && chosen.alpha) { const a = document.createElement("span"); a.className = "badge"; a.textContent = "ALPHA"; $("found").append(" ", a); }
  $("s2").hidden = false;
  status("");
  stepTo(1);
  showPackage();
};

$("agree").onchange = showPackage;

$("go").onclick = async () => {
  if (busy || !dev || !chosen) return;
  busy = true; $("go").disabled = true; $("find").disabled = true; $("bar").hidden = false; $("bar").value = 0;
  try {
    status(T.downloading);
    const raw = chosen.raw || await fetchPackage(chosen);
    if (modelOf(dev.id.text) !== modelOf(chosen.product)) throw Object.assign(new Error(T.model), { code: "model" });
    const image = logicalImage(raw);
    await updater.install(dev, image, chosen.product, (s, v) => {
      if (s === "start" || s === "check") { status(T.checking); $("bar").value = 3; }
      else if (s === "loader") { status(T.starting); $("bar").value = 6; }
      else if (s === "write") { status(T.writing(v)); $("bar").value = 8 + Math.round(v * 0.88); }
      else if (s === "reboot") { status(T.restarting); $("bar").value = 97; }
    });
    $("bar").value = 100;
    status(T.done, "ok");
    stepTo(2);
  } catch (e) {
    status(T[e.code] || e.message, "bad");
  } finally {
    dev = null; busy = false; $("find").disabled = false;
    $("go").disabled = true;
  }
};

if (!navigator.requestMIDIAccess || !window.isSecureContext) { $("nomidi").hidden = false; $("s1").hidden = true; }
loadList();
