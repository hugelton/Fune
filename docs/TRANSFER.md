# Songs and presets between the FM-1 and a computer

A song or a user preset moves as a `.fune` file: the bytes exactly as a slot keeps them, a song (`KRSG`) or a preset (`KRSP`),
the format in `src/kreisel_song.h`. The same file is what the desktop build keeps in its own slots
(`~/Library/Application Support/Kreisel/songs/song1.krs .. song6.krs`, `presets/preset01.krp .. preset24.krp`; on Linux
`~/.local/share/kreisel/`): a `.krs` or `.krp` copied out and renamed `.fune` is the same thing, and the other way round.

## FM-1 to computer

The FM-1 connected over USB, its console answering (`platforms/fm1hw/fm1_con.py stat`). Requires pyserial (`~/fm1-venv`).

```bash
~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py list                    # the six songs and 24 presets, by name
~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py pull song 1             # -> NAME.fune (the song's name), or song1.fune
~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py pull preset 5 bass.fune
```

`pull` reads the slot as it is in flash (not the song being played: `save` it first if it changed). The bytes come with a CRC-32;
a damaged transfer is refused, try again.

## Computer to FM-1

```bash
~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py check song.fune       # what the file is (no device)
~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py push song.fune 3      # a song into song slot 3; a preset file into preset slot 3
```

`push` overwrites the slot. Stop the transport first: the FM-1 refuses while it plays (writing flash silences the audio). It also
refuses a file whose CRC-32 does not match what was sent, and anything its loader would refuse (another kind, a damaged file, a
newer format). The song being played is not changed: open the slot with LOAD on the SAVE page (or `fm1_con.py "load 3"`).

## On the desktop (macOS, Linux)

- `Fune song.fune` (or `build/Fune.app/Contents/MacOS/Fune song.fune`), drop the file on the window, or open it from
  the Finder (the app takes `.fune` files).
- A song replaces the song in hand; a preset goes onto the track in hand, with its engine. A file that would not load is refused
  and nothing changes (the reason on stderr).
- SAVE then keeps it in a slot, as any song or preset.

To send a desktop song to the FM-1: SAVE it into a slot, copy `songs/songN.krs` to `NAME.fune`, `push` it.

## The console commands (fm1_console.c)

| Command | Reply |
| --- | --- |
| `slots` | `ok s1=NAME .. s6=.. p1=.. p24=..` (`_` for a space, `*` no name, `-` empty) |
| `slotget song\|preset N` | `ok bytes=N crc=0x........` (zlib CRC-32): the slot read into the transfer buffer |
| `slotdata OFF` | `ok OFF HEX`: 64 bytes of it from OFF |
| `songput N`, `songdata HEX` | the bytes of a file on their way in (24 a line: a console line holds 64 characters) |
| `slotput song\|preset N CRC` | `ok slot=N bytes=.. name=..`, or `err slotput crc`, `err slotput playing`, `err slotput RC` |

`slotput` checks the CRC-32, then the file as the loader would (`k_song_check` / `k_preset_check`: the very refusals of
`k_song_load` / `k_preset_load`, run without an app, since a second KApp does not fit the FM-1's RAM), then writes the slot
(`k_save_put`). A preset is at most 384 bytes on the FM-1 (a cell of its bank).

## In a browser

`platforms/fm1hw/transfer.html` does the same in Chrome or Edge (Web Serial), with nothing to install: open the file, press
Connect, pick the FM-1's port. It lists the slots; Pull downloads a slot as a .fune file, Push sends one into a slot (checked as
`fm1_slots.py` checks it: its header and CRC-16, then CRC-32 both ways; the FM-1 checks again before it writes, and refuses while
it plays). Not yet tried against the device.
