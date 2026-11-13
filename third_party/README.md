# third_party

Code Kreisel builds on. Each directory keeps its own licence and notice files; do not edit them in place (change
them upstream, or write the change down in the commit that updates the copy).

| Directory | What | Licence | From |
| --- | --- | --- | --- |
| `pendel-dsp/` | The insert effects (oscillator, SVF, delay, the fifteen DSPs) from Pendel, the author's own. | MPL-2.0 (relicensed for Kreisel, 2026-11-07; upstream Pendel is GPL-3.0-only) | Pendel |
| `felucca-bsp/` | The hardware layer of the M-VAVE FM-1: `chip/` (clock, interrupts, flash, USB, boot, the update entry) and `boards/fm1/` (key scan and LEDs, audio, display wiring, ADC). Used by `platforms/fm1hw/`. | MPL-2.0 (SDK: Apache-2.0, not included) | [felucca-bsp](https://github.com/hugelton/felucca-bsp), pinned by Felucca 2.0 at `5942e68b`; copied from `firmware/bsp` of the 2026-11-04 snapshot |

## Why a copy and not a submodule

`felucca-bsp/` is a copy:

- A build from a fresh clone needs no network and no `git submodule update`; GitHub's ZIP and source archives leave
  submodule directories empty (Felucca's own BUILDING.md warns about it).
- The copy is exactly what was built and read here. The BSP is small (about 620 KB) and changes rarely.
- A fix to the hardware layer can be made here and sent upstream as a pull request, without a second repository to
  keep in step.

The cost is that the copy can drift. It was taken from a snapshot that has no `.git` of its own for the BSP, so the
commit could not be checked against `5942e68b`; `felucca-bsp/` is the content of `firmware/bsp` as of that snapshot.
To update it, copy the new tree over the old one (keeping `NOTICE`, `LICENSE`, `LICENSING.md` and `LICENSES/`), run
`platforms/fm1hw/build.sh`, and say the upstream commit in the commit message.

The engine interface and shared voice DSP that came from Felucca-Engines (the author's own) are no longer a copy here: they
are Kreisel's `src/kreisel_engine_types.h`, `src/kreisel_voice_dsp.c` and `tools/gen_engine_tables.py`, relicensed MPL-2.0
(2026-11-07). Felucca-Engines' own engines, with another contributor's change, are not used.

## Licence notes for `felucca-bsp`

- Every file under `chip/`, `boards/`, `loader/` and `examples/` is MPL-2.0, applied per file: a change to one of
  these files, when distributed, is published under MPL-2.0. Kreisel is MPL-2.0 too.
- `NOTICE` lists who wrote what: Leo Kuroshita (the chip layer, board code, loader), Kerem Kilic (keremimo: the USB
  MIDI clock and transport sync in `chip/usb.c`, the second core, USB host integration, tests), Adrien Tibi
  (Recursive Field Instruments: the USB MIDI host), Charles Vestal (charlesvestal: the analog key rows, conversions in
  steps), Steve Pinkham (spinkham: the divide-by-zero trap fix, reported by Felucca pull request 111). Keep it with the
  code, and keep it in every distributed package.
- The JieLi AC79 SDK (Apache-2.0) is not part of the copy. Three of its files go into a package.
