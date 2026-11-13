# Controllers

A MIDI keyboard, a control surface or a pad box plugged into Fune (a computer, the Cardputer Zero or an H700 over USB, or
Network MIDI) plays notes on the track in hand and follows the clock with no setup. What its knobs, faders and buttons do comes
from a **controller profile**: a short text file. Some are built in; you can write your own.

## Where

- Built in: `src/kreisel_ctl.c` (now: KORG nanoKONTROL2 in its factory CC mode).
- Yours: `controllers/` in Fune's data folder (macOS `~/Library/Application Support/Kreisel/controllers`, Linux
  `~/.local/share/Kreisel/controllers`). Fune makes the folder with `example.conf` the first time it starts, and reads every
  `*.conf` there when it starts. A profile of yours with the same `name` as a built-in one takes its place.

A source is matched by its MIDI port name: a profile whose `match` is a part of it (any case) is used for that source. The rows
it does not name fall through to the usual ones (notes, the clock, CC 7 level, CC 10 pan, CC 16 to 19 the macros). Fune prints
`controllers: <port>: <profile>` when it picks one.

## The file

```
# a comment
name = My Box
match = mybox            # a part of the port's name; empty: never chosen
cc 74 = knob 1           # the page in hand's first knob, the CC's 0 .. 127 across its range
cc 20 ch 2 = rel 2       # an endless encoder on channel 2 (1 .. 63 up, 65 .. 127 down)
cc 0 = level 1           # track 1's fader; "level" alone: the track in hand's
cc 10 = pan
cc 1 = macro 1           # HOME's macros 1 .. 4
cc 41 = play             # a button: done when pressed (a value of 64 or more)
note 36 ch 10 = rec      # a pad's note as a button
note any = note          # notes on the track in hand
key f = up               # a computer keyboard's key (see Keys)
```

What a row can do:

| Kind | Words |
|---|---|
| a value | `knob N` (1..8), `rel N`, `level` / `level N`, `pan`, `macro N` (1..4) |
| a button | `play` `rec` `undo` `tools` `ok` `back` `home` `menu` `save` `settings` |
| a step | `track+` `track-` `select+` `select-` `pattern+` `pattern-` `value+` `value-` `octave+` `octave-` |
| the cursor | `up` `down` `left` `right` |
| a page | `page seq` `glo` `synth` `edit` `env` `lfo` `fx` `arp` |
| notes | `note` (on a `note` row only) |

A line Fune cannot read is reported with its number, and that file is skipped.

## MIDI learn

A profile can be made on the screen, a control at a time (a computer, the Zero, an H700; not the FM-1):

1. Settings, **Connect**, **Learn**: OK (or click it again). The settings close; the balloon says `Learn: a control`.
2. Point at what the controller should do, the way you would do it yourself: turn a knob or a card (or click it, or turn the
   wheel over it), a track's fader on the mixer or its tile on HOME, press PLAY, REC, UNDO, TOOLS, SAVE, the menu, a page key, or
   step the track, the cursor (SELECT), the pattern or the octave. It is pointed at, not changed. The balloon: `MIDI -> Knob 1`.
   (The arrows and OK still move the cursor, to reach a control.)
3. Move the controller's knob, fader or button (pad). The first CC (or, for a button, a note too) is bound:
   `CC 74 -> Knob 1`. Pointing at another control before that takes that one instead.

Esc (or HOME) stops it at any step. Each binding is written at once as a row of the port's profile, and works at once:

- the port already has a profile of yours: the row goes into its file (a row of the same CC or note and channel is replaced);
- it has a built-in one (nanoKONTROL2): its text is copied into `controllers/<name>.conf`, with the row, and takes its place;
- it has none: a new file named after the port, `name` and `match` its name (up to a character a profile cannot hold).

What it writes: a knob as `knob N` (N: its place on the page), on the mixer `level N` (the track's) and `pan`, on HOME `macro N`,
the buttons by their words above. A channel other than 1 is written (`ch N`). An endless encoder is learned as `knob`: change it
to `rel` in the file. Only the first 8 profiles can be in use at once.

## Keys

A profile's `key` rows make a keyboard layout of their own (a USB keyboard on the Zero, or any computer): put the profile's name
in `io.conf`, `[keys] map = My Box`. Keys: a letter or a digit, `up` `down` `left` `right` `pgup` `pgdn` `home` `enter` `space`
`comma` `period`, `f1` .. `f12`. A key does buttons, steps, the cursor and pages (not values).
