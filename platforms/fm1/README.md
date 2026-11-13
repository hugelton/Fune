# FM-1 adapter

Two things carry the FM-1's name:

- **The FM-1 simulator**: the `fm1` profile (`src/ui/draw.c`, 240 x 240, four knobs, a key bed) and the drawn panel
  in `platforms/sdl/panel.c` (`./build/kreisel --profile fm1 --panel fm1`). They run on a desktop and in the browser;
  they are the reference for what the controls do.
- **The firmware for the real FM-1**: `platforms/fm1hw/`. It maps the real buttons, encoders, pot and keys to the
  same `KAction`s the simulator panel sends.

The mapping (`platforms/fm1hw/fm1_main.c`, after `platforms/sdl/panel.c`):

- page buttons (FX, SEL, ENV, LFO, EDIT, GLO, ARP, SEQ) -> `K_ACT_PAGE_GROUP_DOWN` / `UP` with the page family;
  a short press opens its first page, a hold its list
- SELECT -> the selected track; ALGORITHM -> focus next / previous (rows, lanes, steps, menus); PRESETS -> the track's sound;
  KNOB 1-4 -> `K_ACT_PARAM_DELTA`; the MASTER pot -> `K_ACT_MASTER_DELTA` (the pot is absolute, the core takes a delta)
- HOME -> HOME (the four tracks); again, the SEQ view of the track; SAVE -> Save; PLAY; REC; OCT- / OCT+ -> octave
- the 27 keys, F3 at the left: the 16 white ones are `K_ACT_KEY` 0..15, the 11 black ones 16..26

Any action needed for normal use stays available on platforms without these dedicated buttons.
