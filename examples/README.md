# COLDWIRE — original electro demo for Kreisel

A small original four-track sketch drawing on early-1980s electro and sequenced synthesizer music. **No melody or recording is copied from Cybotron or Kraftwerk.**

- 124 BPM, straight 16ths, E minor.
- Track 1: **DRUM** — T8 BD on steps **1, 7, 11**, T8 SD on **5, 13** with a T8 CP on 13; quiet TP ZP on **every 16th except step 3**; T8 closed hats on 3, 7, 11, an open one on 15, a soft T8 CB on 10.
- Track 2: **RETRO** — syncopated E-minor bass.
- Track 3: **DIGITAL** — high FM motifs, a little right, into the delay (BUS 2).
- Track 4: **PHASE** — restrained minor-chord stabs, a little left, into the reverb (BUS 1).

The four tracks peak within 2 dB of each other (about -13 dB each, -7 dB together); the kick and the bass stay in the middle.

Patterns A–D contain variations for the synth tracks. The eight SONG rows suggest an arrangement, but **the current audio transport only loops each track's currently selected pattern**; it does not automatically advance SONG scenes. Change patterns manually to hear the variations. DRUM lanes currently have a single step mask shared across patterns.

## Load on the desktop SDL build

The file is already in Kreisel's KRSG format. With Kreisel closed, **back up an existing slot before replacing it**:

```sh
mkdir -p ~/.kreisel/songs
cp examples/coldwire.krs ~/.kreisel/songs/song6.krs
```

Launch Kreisel normally, then select SONG slot 6 in SAVE > LOAD (or let RESTORE LAST open the newest slot). On the FM-1, over its USB serial console (the song in it is replaced: `save` it first if it should stay):

```sh
~/fm1-venv/bin/python platforms/fm1hw/fm1_put_song.py examples/coldwire.krs 1
```

That loads it and keeps it in song slot 1 (any of 1..6).

To edit the score, change `examples/coldwire.json` (a track's `level`, `pan`, `send1` / `send2` to the buses, `env1` / `env2`, its `params` and `patterns`, or a drum track's `lanes`; the song's `bus1` / `bus2` settings, `null` for a default), then from the repository root run:

```sh
python3 tools/make_demo_song.py
python3 tools/make_demo_song.py --check
```

The generator has no dependencies beyond Python's standard library. The checked-in binary is playable directly and the JSON is the editable source.
