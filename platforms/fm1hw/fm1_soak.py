#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""A soak test on the FM-1 over its console: song 1 loaded and playing, random page, track, knob, SELECT, key and page-button
commands for SECS seconds (default 180), then the frame and audio timings, the count of commands with no reply, the crash record;
the song loaded again and stopped. A reboot shows as a small up_ms, a stall as audio_over > 0.
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_soak.py [SECS]"""
import sys,time,glob,random
SECS=int(sys.argv[1]) if len(sys.argv)>1 else 180
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_con import open_console, ask
P=glob.glob('/dev/cu.usbmodem*')[0]
random.seed(7)
pages=["home","step","drum","roll","song","synth","env1","env2","fx","mixer","bus1","bus2","lfo","arp","scl","edit","tracks"]
cmds=0; errs=0; t0=time.time()
with open_console(P) as s:
    ask(s,"load 1"); ask(s,"prof"); ask(s,"play")
    while time.time()-t0<SECS:
        r=random.random()
        if r<0.25: c="page "+random.choice(pages)
        elif r<0.35: c="track %d"%random.randint(1,4)
        elif r<0.65: c="knob k%d %d"%(random.randint(1,4),random.choice([-3,-1,1,3]))
        elif r<0.75: c="knob sel %d"%random.choice([-1,1])
        elif r<0.85: n=random.randint(48,72); ask(s,"note %d 100"%n); time.sleep(0.05); c="note %d 0"%n
        else: c="btn "+random.choice(["seq","fx","env","lfo","edit","glo","arp"])
        rep=ask(s,c); cmds+=1
        if not rep: errs+=1
        time.sleep(0.03)
    st=ask(s,"stat"); print([x for x in st.split() if x.split('=')[0] in ('stage','up_ms','frame_mean_ms','frame_max_ms','audio_max_us','audio_over','audio_gap_max_us','vcap')])
    print("cmds",cmds,"no reply",errs)
    print(ask(s,"crash")[:120])
    ask(s,"play"); ask(s,"load 1"); ask(s,"page mixer")
