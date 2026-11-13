# Fune — user's guide

Fune is a small groovebox. It has four tracks on the M-VAVE FM-1 and eight on a computer. Each track gets:
- a 16-step sequencer with four patterns (a piano roll, steps, or eight drum lanes)
- a synth engine or a drum kit
- two insert effects

A mixer with two effect buses sits over all the tracks. Songs and presets are saved on the device.

The screens show no instructions. A key that does something is drawn as a keycap, so this guide is where the handling is written down. It is
written for the FM-1 first. The computer, the Cardputer Zero and handhelds with a pad are covered at the end.

## The FM-1's controls

| Control | What it is for |
| --- | --- |
| K1–K4 | the four values of the row in hand |
| SELECT (encoder) | moves through rows; held HOME + SELECT changes the track |
| ALGORITHM (encoder) | the engine on a synth page, the effect on FX, the pattern A–D on a sequencer page |
| PRESETS (encoder) | the next or previous preset of the track in hand |
| Page buttons | SEQ, GLO, HOME, SEL (sound), EDIT, ENV, LFO, FX, ARP: a press opens the family's first page, the next press its next page |
| PLAY, REC | play / stop; arm recording |
| OCT+ / OCT- | the octave of the keys; when something asks, yes / no |
| SAVE | save, load, name and erase songs; on a synth page, overwrite the preset in hand |
| 27 keys | play the track in hand; on a drum track the white keys are steps, the black keys lanes |

Three rules hold everywhere:
- **OCT- cancels** anything.
- **A page button held + SELECT** opens that page's tools (see *Tools*).
- **HOME held** opens the settings.

## First start

On the first start Fune plays the demo song COLDWIRE. After that it opens the song you saved last, unless *Restore last* is
off in the settings. Press PLAY to hear it.

The top line of the screen shows:
- the track in hand (its number in its colour)
- the transport
- the preset's name
- the tempo
- the meters
- the page

## Tracks and the mixer

- **HOME** opens the mixer: one row per track, with its level, pan and the sends to BUS 1 and BUS 2.
- **HOME again** shows the track's four macros, its most-used values. A third press goes back to the mixer.
- **Change track**: HOME held + SELECT. On HOME and the mixer, SELECT alone does it.
- **Mute and solo**: GLO held + a black key.
- **TRACKS** (in the page menu on a computer) lists the tracks one above another.

## Playing and recording

- The keys play the track in hand. OCT+ / OCT- move the octave; HOME held + PRESETS does the same.
- To record live, press REC, then PLAY. Notes go into the pattern as you play. A note's length is how long you held it.
- **The metronome** sounds only while recording. Turn it on in the settings' *Metro* tab. There you also choose its sound
  (Beep, Click, Wood) and whether a bar is counted in before recording starts.

## The sequencer (SEQ)

SEQ opens ROLL. Each further press moves on: ROLL, then STEP, then the tracker view, then ROLL again. On a drum track the
turn is DRUM, then the tracker view.

- **ROLL** is a piano roll. SELECT moves through the steps and the notes of a chord. K1–K4 set the step, note, length and
  velocity. A note's length stops at the next note.
- **STEP** shows the steps of the track.
- **DRUM** (on a drum track) has eight lanes. The black keys pick a lane, the white keys set its steps.
- **Patterns**: ALGORITHM picks pattern A, B, C or D of the track.
- **Probability**: the fourth card on STEP and DRUM. A step can play 100, 75, 50 or 25 % of the time; a step that may skip
  is drawn faint.
- **The tracker view** shows the same notes as a table: a row per step, a column per track.
  - SELECT moves between columns (tracks). K1 moves between rows.
  - The keys write a note into the row.
  - On a drum column the white keys are the lanes' pads.

## Songs (GLO)

GLO opens SONG, then BUS 1, then BUS 2.

SONG holds eight scenes. Each scene says which pattern each track plays (none, A, B, C or D).
- SELECT picks the scene.
- K1–K4 set the patterns of tracks 1–4.
- PLAY plays the song from that scene.

The tempo, swing and clock source are on the cards of the SEQ and SONG pages.

## Sounds

The synth pages are SEL (the sound), EDIT, ENV 1 / ENV 2 and LFO.

- **ALGORITHM** on a synth page picks the engine. Each engine keeps its own values per track, so turning back brings the sound
  back.
- **PRESETS** steps through the factory sounds and your own. When a sound has changed since its preset, the name shows a `*`.
- **To keep a sound**: on a synth page press SAVE, then SAVE again (or OCT+). This overwrites the user preset in hand. A new
  user preset is made from the SAVE page.

| Engine | Sound |
| --- | --- |
| Retro | two oscillators, a sub and noise into a filter |
| Phase | phase distortion: two lines, each with its own DCO, DCW and DCA envelope |
| Trio | three chip-style oscillators and a filter |
| Lofi | a pulse with motion (sweep, vibrato, arpeggio) and noise |
| Organ | nine drawbars, rotary, percussion and click; it plays as a gate (no envelope pages) |
| Digital | two FM operators |
| Drum | eight lanes of synthesised pieces (and three samples) |

There are 69 factory presets: 12 each for Retro, Trio, Lofi and Digital, 11 for Phase, 4 for Organ, and 6 drum kits. There
are 24 user preset slots.

**ARP** opens the arpeggiator; ARP again opens **SCL**, the scale: notes are kept to it.

## Effects

**FX** shows the track's two insert slots.
- In an empty slot, turn ALGORITHM to insert an effect: Dist, Cho, Dly, Rev, Gain, Filter, Fuzz, Wah, Crush, Phaser,
  Tremolo, Wobble, Noise, Pitch or Amp.
- K1–K4 set its values. K4 is the mix.
- A value with two positions is drawn as a switch.
- A value that picks a wave is drawn as that wave.

**BUS 1 and BUS 2** (GLO) are shared effects. Each track sends to them from the mixer.

## Tools

A page button held + SELECT opens that page's tools and picks a row; let go, and the list stays open.
- K1–K4 set the tool's values. You hear the result at once.
- OCT+ does it; OCT- lets it go.

The tools act on the pattern: Fill, Euclid, Random, Shift, Reverse, Duplicate, Clear and others, depending on the page.
**Undo** the last tool with HOME held + REC; press it again to redo.

## Saving

SAVE opens the song slots (six on the FM-1). Choose a slot with SELECT, then press OCT+ for its menu:

| Action | Keys |
| --- | --- |
| Save where the song came from | SAVE, OCT+, OCT+ |
| Load | SAVE, the slot, OCT+, SELECT to *Load*, OCT+ |
| Name | SAVE, OCT+, SELECT to *Name*, OCT+; then K1 moves the cursor, K2 picks the letter, OCT+ finishes |
| Erase | SAVE, OCT+, SELECT to *Erase*, OCT+, then OCT+ once more (OCT- keeps it) |

Songs and presets can be copied to and from a computer as `.fune` files ([TRANSFER.md](TRANSFER.md)).

## Settings (HOME held)

| Tab | Rows |
| --- | --- |
| Display | the LEDs (the FM-1), screen off, help |
| System | restore last (open the last song at start), clear all (asks twice), about; on the FM-1, USB level (Fixed: full level whatever the volume knob; Volume: as the knob) |
| Metro | the metronome, its sound, count-in |
| Connect | MIDI in, channel, clock (internal or external); on a computer, *Learn* (below) |

## MIDI

- **In**: notes, clock and CC, over USB and TRS on the FM-1.
- **Out**: the keys and the sequencer's notes, one channel per track. Drum lanes go out as General MIDI notes: BD 36, SD 38,
  CH 42, OH 46, CP 39, LT 45, HT 50, RS 37. When Fune runs on its own tempo, the clock goes out too, with Start and Stop.
- **The FM-1 over USB** is also an audio interface: a computer can record it (the device "Felucca"). By default it
  records at full level whatever the MASTER knob says (the knob sets the headphones and the line out only); the settings'
  *USB level* set to Volume makes it follow the knob.
- **On a computer**, Fune shows up as the MIDI source "Fune". To map a controller's knobs, use *Learn* in the Connect tab:
  1. Press OK on *Learn*.
  2. Touch the knob, fader or button on the screen.
  3. Move the controller's knob.

  The mapping is saved in a profile per controller ([CONTROLLERS.md](CONTROLLERS.md)).

## On a computer

`Fune.app` (macOS) and `kreisel` (Linux) have eight tracks. Every value can be dragged with the mouse or turned with the
wheel. The keyboard works as well.

| Action | Mouse | Keyboard |
| --- | --- | --- |
| A value | drag it, or the wheel | + / - |
| Focus, accept, back | click | arrows, Return, Escape |
| Page menu | the page name in the header | Tab |
| Track | click a track | Page Up / Page Down |
| Tools | TOOLS in the header | T |
| Copy, paste, clear | right click | Cmd+C, Cmd+V, Delete |
| Undo / redo | | Cmd+Z |
| Save | | Cmd+S |
| Play / stop | the header's play | Space |

A `.fune` file can be opened by dropping it on the window.

## On the Cardputer Zero and on handhelds

- **Cardputer Zero** (keyboard only):
  - F Z X C move the cursor. K and L change the value.
  - 1–0 open the pages. OK is yes, Del is no, Space plays.
  - `,` and `.` do ALGORITHM's job. r is REC, s SAVE, g the settings, u undo.
  - `[` and `]` change the octave, o and p the presets, t the tools.
- **A pad** (H700 handhelds):
  - The D-pad moves the cursor. A held + the D-pad changes a value.
  - A presses, B goes back, X opens the tools, Y is REC.
  - L and R change the track, START plays, SELECT opens the page list. L2 and R2 change the octave.
  - SELECT held + START saves, + Y undoes, + L / R changes the preset.

## When something goes wrong

- **Keep M-VAVE's stock bootloader** on the FM-1 (and the SMK-37): Fune is tested on it, and replacement bootloaders
  are not recommended ([FM1_INSTALL.md](FM1_INSTALL.md)).
- **The FM-1 restarts or hangs at start**: hold HOME while it starts. It then opens without the saved song.
  [FM1_INSTALL.md](FM1_INSTALL.md) explains how to get out of a crash loop and how to install again.
- **A song from a newer version** can still be opened. A sound whose engine this version lacks stays silent and is marked
  *Update*; it is kept as it was.
