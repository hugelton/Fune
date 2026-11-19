#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""A demo for a camera, on the FM-1 or on the Cardputer Zero with the same timeline (to cut between them): the demo song
plays from its first scene while a timeline turns the pages, tracks and knobs over the console, on the song's own beat.
Nothing is saved: the song is loaded again at the end, so every knob turned is put back. Optionally the FM-1's own sound is
recorded at the same time (its USB audio, with sox).

  ~/fm1-venv/bin/python platforms/fm1hw/fm1_demo.py examples/demo/muriya_teaser.txt --dry-run
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_demo.py examples/demo/muriya_teaser.txt --countdown 5 --record take1.wav
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_demo.py examples/demo/muriya_teaser.txt --ssh pi@zero.local --countdown 5
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_demo.py examples/demo/muriya_teaser.txt --tcp 5799      (Fune --console 5799 here)

Where it plays (docs/DEMO_VIDEO.md):
  (default)          the FM-1 over USB (its console: the first /dev/cu.usbmodem* whose stat says stage=, or --port)
  --ssh USER@HOST    a Cardputer Zero: Fune is started there with its console (`fune --console PORT`, the screen and keys
                     as the launcher's) through ssh, the port forwarded; it quits when this ends. Quit the launcher's Fune first
  --tcp [HOST:]PORT  a Fune already running with --console PORT (a computer: a rehearsal of the timeline)

The timeline: one line a cue, "BAR[.BEAT] COMMAND", bars from 1 (the song's first scene), beats 1..4 (a fraction is fine:
3.2.5). COMMAND is any console line (page synth, track 2, btn seq, knob k1 3, note 60 100) or
  sweep KNOB DELTA BEATS     DELTA detents spread evenly over BEATS (a filter opening; turn it back with a second sweep)
  stop                       the end: the song stops there (else at the end of the timeline's last bar + 1)
"#" starts a comment. Options:
  --slot demo|N      the song: the built-in demo (default: the same song on every device), or a slot whose name holds --expect
  --countdown S      S seconds to start the camera, counted on the terminal, before PLAY
  --record FILE      the FM-1's sound to FILE (sox, --audio-device, default "Felucca": the FM-1's USB audio), from a second
                     before PLAY to two after the end
  --until BAR        stop after BAR (a rehearsal of the start)
The PLAY LED (the FM-1) and the first note mark the start in both the camera's picture and the recording."""
import argparse
import glob
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))


def parse_timeline(path):
    cues = []
    for n, raw in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"^(\d+)(?:\.(\d+(?:\.\d+)?))?\s+(.+)$", line)
        if not m:
            sys.exit(f"{path}:{n}: want 'BAR[.BEAT] COMMAND': {raw!r}")
        bar, beat, cmd = int(m.group(1)), float(m.group(2) or 1), m.group(3).split()
        if bar < 1 or not 1 <= beat < 5:
            sys.exit(f"{path}:{n}: bar from 1, beat 1..4: {raw!r}")
        at = (bar - 1) * 4 + (beat - 1)   # in beats from PLAY
        if cmd[0] == "sweep":
            if len(cmd) != 4:
                sys.exit(f"{path}:{n}: sweep KNOB DELTA BEATS")
            knob, delta, beats = cmd[1], int(cmd[2]), float(cmd[3])
            for i in range(abs(delta)):   # one detent at a time, evenly over the beats
                cues.append((at + beats * i / abs(delta), f"knob {knob} {1 if delta > 0 else -1}", n))
        else:
            cues.append((at, " ".join(cmd), n))
    cues.sort(key=lambda c: c[0])
    return cues


class Tcp:
    """the SDL builds' console (platforms/sdl/console.c): a line a command, a line a reply"""

    def __init__(self, host, port, wait=0.0):
        t_end = time.monotonic() + wait
        while True:
            try:
                self.s = socket.create_connection((host, port), timeout=2)
                break
            except OSError:
                if time.monotonic() > t_end:
                    sys.exit(f"no Fune console at {host}:{port} (started with --console {port}?)")
                time.sleep(0.3)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        if not self.ask("ver", timeout=15).startswith("ver"):   # (the app answers once its loop runs: a start can take seconds)
            sys.exit(f"Fune at {host}:{port} does not answer")

    def ask(self, cmd, timeout=1.5):
        self.s.sendall((cmd + "\n").encode())
        t_end = time.monotonic() + timeout
        while b"\n" not in self.buf and time.monotonic() < t_end:
            self.s.settimeout(max(0.01, t_end - time.monotonic()))
            try:
                chunk = self.s.recv(4096)
            except OSError:
                break
            if not chunk:
                break
            self.buf += chunk
        line, _, self.buf = self.buf.partition(b"\n")
        return line.decode(errors="replace").strip()

    def close(self):
        self.s.close()


class Serial:
    """the FM-1's console (fm1_console.c) over USB"""

    def __init__(self, port):
        from fm1_con import open_console, ask
        self._ask = ask
        self.s = open_console(port)

    def ask(self, cmd, timeout=1.5):
        return self._ask(self.s, cmd, timeout)

    def close(self):
        self.s.close()


def probe_port():
    from fm1_con import open_console, ask
    for p in sorted(glob.glob("/dev/cu.usbmodem*")):
        try:
            with open_console(p) as s:
                if "stage=" in ask(s, "stat"):
                    return p
        except Exception:
            pass
    sys.exit("no FM-1 console found (a /dev/cu.usbmodem* whose stat says stage=): is it connected and running Fune?")


def stat_field(rep, key):
    m = re.search(r"\b%s=(-?\d+)" % key, rep)
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("timeline")
    ap.add_argument("--slot", default="demo")
    ap.add_argument("--expect", default="MURIYA")
    ap.add_argument("--bpm", type=int, default=0, help="for --dry-run (else read from the device)")
    ap.add_argument("--countdown", type=int, default=0)
    ap.add_argument("--record")
    ap.add_argument("--audio-device", default="Felucca")
    ap.add_argument("--until", type=int, default=0)
    ap.add_argument("--port")
    ap.add_argument("--ssh", help="USER@HOST: a Cardputer Zero")
    ap.add_argument("--tcp", help="[HOST:]PORT: a Fune running with --console PORT")
    ap.add_argument("--console-port", type=int, default=5799)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    cues = parse_timeline(a.timeline)
    end_beat = next((c[0] for c in cues if c[1] == "stop"), None)
    if end_beat is None:
        end_beat = (int(max(c[0] for c in cues) // 4) + 1) * 4 if cues else 4
    if a.until:
        end_beat = min(end_beat, a.until * 4)
    cues = [c for c in cues if c[0] < end_beat and c[1] != "stop"]

    if a.dry_run:
        bpm = a.bpm or 98
        spb = 60.0 / bpm
        for at, cmd, n in cues:
            print(f"{int(at // 4) + 1:3d}.{at % 4 + 1:<5.3g} {at * spb:7.2f}s  {cmd}")
        print(f"end: bar {int(end_beat // 4) + 1}, {end_beat * spb:.1f} s at {bpm} BPM; {len(cues)} commands")
        return

    cues = sorted(cues + [(b * 4 + 2, "?scene", 0) for b in range(0, int(end_beat // 4), 8)], key=lambda c: c[0])
    remote = None
    if a.ssh:   # Fune started on the Zero with its console, the port forwarded here
        p = a.console_port
        remote = subprocess.Popen(["ssh", "-tt", "-o", "ExitOnForwardFailure=yes", "-L", f"{p}:127.0.0.1:{p}", a.ssh,
                                   f"/usr/share/APPLaunch/bin/fune --console {p}"],
                                  stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        con, where = Tcp("127.0.0.1", p, wait=20), f"{a.ssh} (Fune started there)"
    elif a.tcp:
        host, _, port = a.tcp.rpartition(":")
        con, where = Tcp(host or "127.0.0.1", int(port), wait=2), f"{host or '127.0.0.1'}:{port}"
    else:
        port = a.port or probe_port()
        con, where = Serial(port), port
    rec = None
    try:
        if stat_field(con.ask("stat"), "playing"):
            con.ask("play")   # (stopped first: PLAY below starts from the top)
        if a.slot == "demo":
            rep = con.ask("demo")
            if not rep.startswith("ok"):
                sys.exit(f"demo refused ({rep or 'no reply'}): a firmware from 0.0.2 on has it; or --slot N")
            name, again = rep.split("demo=", 1)[-1], "demo"
        else:
            slot = int(a.slot)
            m = re.search(r"\bs%d=(\S+)" % slot, con.ask("slots"))
            name = m.group(1) if m else ""
            if a.expect and a.expect.lower() not in name.replace("_", " ").lower():
                sys.exit(f"song slot {slot} is {name or '(empty)'!r}, not {a.expect!r}: --slot N, or --expect '' to play it anyway")
            if not con.ask(f"load {slot}").startswith("ok"):
                sys.exit(f"load {slot} refused")
            again = f"load {slot}"
        bpm = stat_field(con.ask("stat"), "bpm") or 120
        spb = 60.0 / bpm
        print(f"{where}: {name}, {bpm} BPM; {len(cues)} commands over {end_beat * spb:.1f} s")
        con.ask("page song")   # PLAY on SONG plays the song from its first scene (elsewhere the patterns loop)
        for i in range(a.countdown, 0, -1):
            print(f"  {i}", flush=True)
            time.sleep(1)
        if a.record:
            if not shutil.which("sox"):
                sys.exit("sox missing (brew install sox)")
            rec = subprocess.Popen(["sox", "-q", "-t", "coreaudio", a.audio_device, a.record],
                                   stdin=subprocess.DEVNULL, stderr=subprocess.PIPE)
            time.sleep(1.0)
            if rec.poll() is not None:
                sys.exit(f"sox could not record from {a.audio_device!r}: {rec.stderr.read().decode(errors='replace')[:200]}")
        t0 = time.monotonic()
        if not con.ask("play").startswith("ok"):
            sys.exit("play refused")
        late = 0.0
        try:
            for at, cmd, n in cues:
                due = t0 + at * spb
                while True:   # (sleep short: time.sleep oversleeps)
                    left = due - time.monotonic()
                    if left <= 0:
                        break
                    time.sleep(min(left, 0.005))
                late = max(late, time.monotonic() - due)
                if cmd == "?scene":   # (every 8 bars, mid-bar: where the device says it is, against the timeline)
                    sc = stat_field(con.ask("stat"), "scene")
                    if sc:
                        print(f"  bar {int(at // 4) + 1}: plays scene {sc}" + ("" if sc == int(at // 4) + 1 else "  (off!)"))
                    continue
                rep = con.ask(cmd)
                if not rep or rep.startswith("err"):
                    print(f"  line {n}: {cmd!r} -> {rep or '(no reply)'}")
            while time.monotonic() < t0 + end_beat * spb:
                time.sleep(0.01)
        except KeyboardInterrupt:
            print("stopped")
        finally:
            if stat_field(con.ask("stat"), "playing"):
                con.ask("play")
            con.ask(again)   # every knob back as the song has it (nothing was saved)
            con.ask("page home")
            if rec:
                time.sleep(2.0)
                rec.terminate()
                rec.wait()
                print(f"recorded: {a.record}")
        print(f"done; the latest command {late * 1000:.0f} ms after its beat")
    finally:
        con.close()
        if remote:
            remote.terminate()
            remote.wait()


if __name__ == "__main__":
    main()
