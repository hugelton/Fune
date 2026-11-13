#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Send commands to the FM-1's USB serial console (platforms/fm1hw/fm1_console.c) and print each reply.

  ~/fm1-venv/bin/python platforms/fm1hw/fm1_con.py "page mixer"
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_con.py "prof" "page roll" --wait 2 "stat"   # --wait SECONDS between commands
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_con.py --port /dev/cu.usbmodem1234 stat

Without --port the first /dev/cu.usbmodem* is used. Each command is one line; the reply is one line ('ok', 'err ...',
or key=value pairs). Exit status 1 if a reply starts with 'err' or none comes. Requires pyserial (~/fm1-venv).
Other tools use open_console() and ask() (fm1_slots.py)."""
import argparse
import glob
import sys
import time


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* (is the FM-1 connected, running the console build?)")
    return ports[0]


def open_console(port=None):
    """The console, open (a serial.Serial: use it in a with). DTR up: the device speaks only while it is."""
    try:
        import serial
    except ImportError:
        sys.exit("pyserial missing: ~/fm1-venv/bin/pip install pyserial")
    s = serial.Serial(port or find_port(), 115200, timeout=0.1, write_timeout=1)
    s.dtr = True
    time.sleep(0.15)
    s.reset_input_buffer()
    return s


def ask(s, cmd, timeout=1.5):
    """One command, its one-line reply ('' when none came)."""
    s.write(cmd.encode("ascii") + b"\r")
    end = time.time() + timeout
    line = b""
    while time.time() < end:
        line += s.read(256)
        if line.endswith(b"\n"):
            break
    return line.decode("ascii", "replace").strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device (default: the first /dev/cu.usbmodem*)")
    ap.add_argument("--timeout", type=float, default=1.5, help="seconds to wait for a reply (default 1.5)")
    ap.add_argument("--wait", type=float, default=0.0, help="seconds to sleep after each command but the last")
    ap.add_argument("commands", nargs="+", help='commands, e.g. "stat" "page mixer" "key 3 down"')
    a = ap.parse_args()
    rc = 0
    with open_console(a.port) as s:
        for n, cmd in enumerate(a.commands):
            text = ask(s, cmd, a.timeout)
            print(text if len(a.commands) == 1 else f"{cmd}: {text}")
            if not text or text.startswith("err"):
                rc = 1
            if a.wait and n + 1 < len(a.commands):
                time.sleep(a.wait)
    return rc


if __name__ == "__main__":
    sys.exit(main())
