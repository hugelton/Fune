# Plugin SDK and API

How to write a sound engine for Fune (the code: Kreisel) and how the host takes it in. The reasons behind the rules are in
this page is the reference.

**Status (2026-11-05).** The record, the descriptor, the DSP interface, the needs check and the registry are in place and
tested. **On the desktop (SDL) a plugin is a shared library loaded at start** (below: *Building one*); the first is
FM2 (`plugins/fm2`), tested by `tests/plugin_test.c`. On the FM-1 a plugin is still built into the firmware: loading one from
its flash (installed from the web IDE) is not done yet. Effects (`K_PLUG_EFFECT`) have a mono Q15 DSP ABI and play in the
track inserts. A plugin may lay out its own SOUND and EDIT with the host's widgets (*Screens*, 2026-11-11); without a layout
the host shows it with the vanilla view.

**2026-11-12.** One header holds the contract and its version: `src/kreisel_engine_api.h` (in `sdk/kreisel_sdk.h`). The
built-in engines and effects are records of the same kind, with their DSP in them, and pass the same check as a plugin
(`k_plugin_check`, run on each by `tests/plugin_test.c`). The smallest example, BEEP (`plugins/beep`), is built and played by
`tests/sdk_example_test.c` (*A minimal engine*, below).

**2026-11-12, the FM-1.** A plugin for the FM-1 is an image in its flash, run in place, installed over the USB console
(*On the FM-1*, below). Built and checked on the host; **not yet run on a device**.

## The parts of a plugin

A plugin is three things, and nothing else:

| Part | Type | Header | What it is |
| --- | --- | --- | --- |
| Record | `KPlugin` | `src/kreisel_plugin.h` | Its kind, version, what it needs; points at the other two |
| Descriptor | `KEngine` | `src/kreisel_engine.h` | Its name, id, parameters, macros, pages: what the host shows |
| DSP | `engine_t` | `src/kreisel_engine_types.h` | How it sounds: integer audio code |

`src/kreisel_engine_api.h` ties them together: the interface's version (`K_ENGINE_API_MAJOR`, `_MINOR`), the rules below
(*Versions and compatibility*), and `k_plugin_check`, the check every record passes, the built-in ones included.

**The host draws; the plugin lays out.** Without a layout the host builds the vanilla view from the parameter descriptor. With
one (`KPluginExt`, below: *Screens*) the plugin places the host's widgets on a grid and picks how they look; the host draws
them in the theme on every device, and may let the plugin draw inside a picture's frame. Values change only through the host.

## The record: `KPlugin`

```c
typedef struct {
    uint32_t size;          /* sizeof(KPlugin) as the plugin was built */
    uint16_t abi;           /* K_PLUGIN_ABI (1) */
    uint16_t version;       /* the plugin's own version (a song keeps it beside the id) */
    uint8_t kind;           /* K_PLUG_SYNTH (notes) or K_PLUG_DRUM (lanes) */
    const KEngine *desc;    /* the descriptor; desc->uid is its identity */
    const void *dsp;        /* a synth's `const engine_t *` */
    /* what it needs (0: not said) */
    uint32_t fs;            /* the sample rate its DSP was built for; 0 any */
    uint32_t ram;           /* bytes of state it keeps, all parts and voices together */
    uint16_t cost;          /* microseconds one voice takes a 32-sample block on the FM-1 */
    uint16_t needs;         /* K_NEED_FLOAT if it uses floating point */
    const uint8_t *lane_notes;   /* a synth that is a drum: the note each lane plays (8); 0: General MIDI's */
} KPlugin;
```

- **The record only grows at its end.** A field added later reads 0 in an older record, and 0 always means "what the
  interface did before". `size` says how much of the record the plugin knows; a record of `K_PLUGIN_SIZE_1` bytes (before
  the needs) is still taken.
- **Say what you need, never which devices you are for.** Each device has its room (`KDeviceCaps`). The installer compares
  your needs with a device's room before it sends the plugin, and the host compares them again when it registers the
  plugin. A new device then needs no change to any plugin.

| Device | `fs` | RAM for plugins | `cost` budget a track | Floats |
| --- | --- | --- | --- | --- |
| FM-1 | 44100 | 16 KB | 120 us (6 voices: 20 us a voice) | no |
| SDL, web | 44100 | 1 MB | 2000 us | yes |

## The descriptor: `KEngine`

```c
typedef struct {
    const char *name;             /* "FUZZ": shown on the engine bar and the cards */
    uint32_t uid;                 /* K_UID('f','u','z','z'): its identity, forever */
    const char *page_title[3];    /* names of its rows of four ("" goes with the engine's own) */
    KParamDesc edit[K_ENGINE_PARAMS];   /* up to 20 parameters */
    uint8_t drum;                 /* 1: plays lanes (a drum), not notes; a SYNTH may say so of itself (below) */
    const char *lanes[10];        /* a drum: its lanes, two letters each ("BD"); the host plays eight */
    uint8_t macro[4];             /* four of its parameters (indexes): HOME's first row of cards */
    uint8_t nparams;              /* how many of edit[] it has */
    uint8_t page2;                /* edit[0 .. page2-1] on SOUND (after ENGINE), the rest on EDIT */
    uint8_t nomod;                /* K_NOMOD_FILTER, K_NOMOD_SHAPE: host modulation it does not read */
    uint8_t gate;                 /* 1: plays a gate of its own; the track's ENV pages are not read */
} KEngine;
```

### The id

`K_UID(a, b, c, d)`: four printable characters. **Upper-case letters and digits only are Hügelton Instruments'** (the
built-in `KANL`, `KPHS`, `KTRI`, `KLOF`, `KOGN`, `DRUM`). Yours must have at least one lower-case letter. The id never
changes and is never given to another engine: a song names its engines by id, so a missing or changed engine can never be
mistaken for another.

### Parameters: `KParamDesc`

```c
typedef struct {
    const char *label;            /* about four letters: it sits beside an icon on the smallest card */
    uint8_t fmt;                  /* KF_* */
    int16_t min, max, def;
    const char *const *names;     /* KF_ENUM: a 0-terminated list */
    const char *unit;             /* KF_INT / KF_ENUM: an optional unit ("ct") */
} KParamDesc;
```

| `fmt` | Shown as | Notes |
| --- | --- | --- |
| `KF_INT` | the number (and `unit`) | |
| `KF_PCT` | 0 .. 100 % of its range | most knobs: `0, 127` |
| `KF_BIPCT` | -100 .. +100 % | from the middle |
| `KF_SEMI` | +7st | a knob from the middle |
| `KF_ENUM` | one of `names` | a rotary switch; a few names get marked places |
| `KF_ONOFF` | On / Off | drawn as a switch |
| `KF_CUTOFF`, `KF_TIME`, `KF_DB`, `KF_NOTE`, `KF_OCT` ... | in their unit | see `kreisel_engine.h` |
| `KF_LINK_FENV` | the host's ENV 1 -> filter amount | the slot is not yours: it stands for the host's value, so a filter engine can show it on its EDIT row |

- Labels: keep them to about four letters ("CUT", "DCAY"); `tests/ui_test.c` checks the built-in ones.
- Order: put the sound sources first (SOUND, before `page2`) and the shaping (filter, amp: EDIT) after.
- An enum's values are clamped to its names, whatever `max` says. The host never trusts a stored value.

### Pages and rows

SOUND shows `edit[0 .. page2-1]` from K1 (the engine is picked with ALGORITHM, not a knob of the rows), four a row (a row a knob set, SELECT moves between rows; ALGORITHM picks the engine). EDIT shows
`edit[page2 ..]`. The host's own pages (ENV 1, ENV 2, LFO, FX) follow every engine. Keep each page to three rows: on the
FM-1 a page fits one screen.

### Host modulation: `nomod`, `gate`

The host modulates every voice: pitch (ENV 1, LFO, bend), a cutoff offset and a shape offset (ENV 1 and LFO destinations FLT
and SHP), and the amplitude. If your DSP does not read `vmod_t.cutoff` or `vmod_t.shape`, set `K_NOMOD_FILTER` /
`K_NOMOD_SHAPE`: the host still lets those destinations turn but shows them dim. An organ-like engine sets `gate = 1`: when
it is picked at its defaults the track's ENV 1 becomes a gate (attack 0, sustain full, a short release).

## The DSP: `engine_t`

Your engine is an `engine_t` (`src/kreisel_engine_types.h`); the host calls these members:

| Member | When | Required |
| --- | --- | --- |
| `render(t, v, out, n, m)` | each block of `n` (32) samples, for each sounding voice | yes |
| `note_on(t, v)` | a voice starts (set its phases and state) | no |
| `block(t)` | once a block and part, before its voices (shared state: an LFO, a rotor) | no |
| `amp(t, v, adsr)` | once a control tick: your own amplitude curve from the ADSR value (Q15) | no |
| `keep` | bits of `v->s[]` kept when a sounding voice is retriggered | no |

Other `engine_t` members (`ownenv`, `done`, `post`, `keys`, `mem` ...) are not called by Kreisel yet; leave them 0.

### Reading your parameters

Your parameters arrive in the part's array, `t->p[P_E0 + k]` for `edit[k]`, as stored (the host clamps them to your
range). Read them once per block, outside the sample loop.

### `render`

```c
void render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
```

- `m->inc`: the phase increment of the note (32-bit phase, 2^32 a cycle); `m->pitch16` the pitch in 1/16 semitone;
  `m->fine` a residual detune (1/4096).
- `m->cutoff`, `m->shape`: the host's modulation, `0 .. 127 << 8`, to add to your own (see `nomod`).
- Add your output into `out[i]` (do not overwrite: voices sum), through `voice_amp(s, m, i)`, which applies the envelope,
  the velocity and the LFO's amplitude. Keep `s` about +-16000 (Q15 with headroom).
- Per-voice state: `v->ph[0..2]` (phases) and `v->s[0..7]` (filters, envelopes, noise). Anything larger: your own arrays,
  indexed by part and voice, and counted in `KPlugin.ram`.

### Rules for the DSP

- **Integers.** No `float` on the FM-1 (a float operation is a library call there; `K_NEED_FLOAT` if you must). Multiply is
  not free either: hoist what you can out of the sample loop.
- **No divide in the sample loop.** Take reciprocals once a block.
- **No allocation, no I/O, no blocking.** The audio runs on an interrupt with a hard deadline (a block is 726 us at 44.1 kHz).
- **The sample rate is fixed at build time** (`FS`, 44100). Declare it in `KPlugin.fs`.
- Helpers from `dsp.c`: `sine_i(ph)`, `osc_saw(ph, inc)`, `osc_pulse(ph, inc, pw)` (band-limited), `osc_tri(ph)`,
  `softclip(x)`, `noise32(&state)`, `mulq15(a, b)`, `cents_inc(pitch16, cents, fine)`, `pitch_inc(pitch16)`, the SVF
  `tsvf_coef()`.

## Registering

```c
int k_plugin_register(const KPlugin *plugin);   /* its index, or K_PLUG_E_* */
void k_plugin_set_caps(const KDeviceCaps *caps); /* the platform's room, at start */
int k_plugin_fits(const KPlugin *p, const KDeviceCaps *c, uint32_t used); /* the installer's check */
```

The registry copies the record; your descriptor and DSP must outlive it. Up to `K_PLUGIN_SLOTS` (8) plugins sit after the
built-in engines; ALGORITHM (or the engine box's < >) steps through them all.

| Refusal | Why |
| --- | --- |
| `K_PLUG_E_NULL` | no record or no descriptor |
| `K_PLUG_E_ABI` | another `abi`, or a record shorter than `K_PLUGIN_SIZE_1` |
| `K_PLUG_E_UID` | not four printable characters, or no lower-case letter (Hügelton's space) |
| `K_PLUG_E_TAKEN` | the id is there already |
| `K_PLUG_E_KIND` | missing DSP for a synth/effect; a drum with no lanes |
| `K_PLUG_E_DESC` | a bad range or default, a macro past `nparams`, `page2` past `nparams`, two labels with one id |
| `K_PLUG_E_FULL` | all slots taken |
| `K_PLUG_E_VERSION` | its own `version` is not 1 .. 255 (`K_ENGINE_VERSION_MAX`: a song keeps it in a byte) |
| `K_PLUG_E_RATE` / `RAM` / `CPU` / `NEED` | it does not fit this device |

## What the host does with it

- **Songs** keep, per track, your id, your `version` and the values by parameter. Rearranging the built-in engines, or
  installing others, never changes what a song plays.
- **A missing plugin** (a song names an id that is not installed): the track says NO PLUGIN, keeps your values untouched and
  writes them back on save, and plays its notes with a plain sine meanwhile. Installing the plugin brings the sound back.
- **An older plugin than the song's** (the song was saved with your version 3, version 2 is installed): the same as a missing
  one, untouched and written back (`k_track_newer` tells the two apart); updating the plugin brings the sound back.
- **Switching engines** keeps each engine's values for the track; switching back restores them.
- **Screens**: your layout where you gave one for that kind of screen (*Screens*), else the vanilla view. A desktop folds
  EDIT, ENV and LFO into SOUND: there your SOUND and EDIT tables are drawn one over the other.

## Versions and compatibility

`src/kreisel_engine_api.h` says it in full; in short, three numbers:

| Number | Where | What it means |
| --- | --- | --- |
| `K_ENGINE_API_MAJOR` | `KPlugin.abi` (= `K_PLUGIN_ABI`, 1) | the layouts of `KEngine`, `KParamDesc`, `engine_t`, `voice_t`, `vmod_t`, a part's prefix, the record's first `K_PLUGIN_SIZE_1` bytes. Another one is refused. Changes only when a layout must. |
| `K_ENGINE_API_MINOR` | `KPlugin.size`, `KPluginExt.size`, `KHostApi.size` | what has been appended (3: the needs, `lane_notes`, `ext`; 4: a drum's values for each lane, no layout changed: *A synth that is a drum*). An older plugin's missing fields read 0 ("not said"); a newer plugin's unknown fields, and widgets of an unknown kind, are ignored. |
| your `version` | `KPlugin.version`, kept by a song beside your id | 1 .. 255. Raise it when your parameters change. |

How a song finds your values again: by your id, then **each value by its parameter's id**, the hash of its label
(`k_param_id`), never by its place. So a new version of your engine may add parameters (an old song gives them their
default), drop some (their values are dropped), reorder them, move them between SOUND and EDIT, or widen a range. It must not:

- give a label another meaning (rename the label instead: an old song's value is then dropped, not misread);
- change what a stored number sounds like (a range made finer, an enum's names reordered): add a parameter with a new label;
- reuse your id for another engine, or take a built-in one's.

| A song names | Here | The track |
| --- | --- | --- |
| your id, version v | version >= v | plays; values by id, each brought into its range |
| your id, version v | version < v | kept untouched, as a missing one (`k_track_newer`); written back on save |
| an id not here | | NO PLUGIN: kept untouched, plays a sine; written back on save |

## Building one (a desktop)

Include `sdk/kreisel_sdk.h` (one header: the descriptor and the record, the DSP interface, the parameter slots `P_E0 ..`, a
part as an engine sees it, and the host's integer helpers and tables, compiled into the plugin) and export one function:

```c
#include "kreisel_sdk.h"
/* ... the descriptor, the engine_t, the KPlugin record (as below) ... */
KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &MY_PLUGIN; }
```

Build it as a shared library with `sdk` and `src` on the include path (the repository's
CMake builds `plugins/fm2` so: `build/plugins/fm2.so`). Put it in `~/.kreisel/plugins/` for the SDL build: the host opens every `.dylib`, `.so` or `.dll` there
before it loads the song (`k_plugin_load_dir`, `src/kreisel_plugin_load.c`), registers what `kreisel_plugin()` returns
(checked against the device's room) and keeps it open. A song that names a plugin that is not there keeps its track as
NO PLUGIN and plays a sine; put the library back and the song finds it again.

## On the FM-1: an image in flash (2026-11-12)

The same C file builds for the FM-1. There it is not a shared library but an **image**: its code and constants stay in the
flash and run in place (XIP), nothing is copied to RAM but its `.data`. The format is `src/kreisel_plugin_image.h`.

```bash
python3 tools/kpl_build.py plugins/beep/beep.c                 # -> build/fm1/plugins/beep.kpl (Docker, as platforms/fm1hw/build.sh)
python3 tools/kpl_build.py --info build/fm1/plugins/beep.kpl   # BEEP 'beep' v1 synth, interface 1.3, 4004 bytes, 16 relocations
~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py install build/fm1/plugins/beep.kpl
~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py list       # off N, on N, erase N
```

**Building.** `kpl_build.py` compiles with the FM-1 toolchain and the firmware's own settings (`platforms/targets/fm1.toml`:
the 64-bit products out of line for the CPU erratum, among others) and links the plugin three times: at address 0, with its
code moved, with its RAM moved. The 32-bit fields that move are its relocations (in data and inside instructions alike);
anything else that differs is refused, and a fourth link where the FM-1 puts its first image must equal the file relocated
there. It refuses: a section other than code, constants, `.data` and `.bss` (no constructors); floating point unless the record
says `K_NEED_FLOAT` (the FM-1 then refuses it: it has no FPU); a 64-bit multiply beside a 16-bit load
(`tools/fm1_mul_scan.py`, as for the firmware); a `kreisel_plugin()` that does more than return its record's address.
Not position-independent code: the toolchain's `-fPIC` is FDPIC (a GOT base in `r15` that the caller sets, function
descriptors), which the host's plain function pointers cannot call. An image is relocated once, when it is written, for the
place the FM-1 gives it.

**The image** (`KImageHeader`, 64 bytes, little-endian, then the code and constants, then `.data`'s first values):

| Field | |
| --- | --- |
| `magic` | `KPIM`; written last on the device, so an install cut short leaves no image |
| `hdr_size`, `format` | 64, 1 |
| `flags` | on / off without an erase: off while an odd number of its bits are 0 (outside the header's CRC) |
| `api` | `K_ENGINE_API_VERSION` it was built against; another major is refused, any minor is taken |
| `version`, `kind`, `uid` | the record's (its version is 1 .. 255: one byte, as in a song) |
| `base`, `ram` | where it is linked: the XIP address of its first byte, the RAM address of its `.data` (0 in a file) |
| `size`, `record` | the image's bytes (header included), the record's offset |
| `data_off`, `data_size`, `bss_size` | `.data`'s values in the image; `.bss` follows `.data` in RAM, zeroed |
| `nreloc` | a file: that many 32-bit words after the image (offset, bit 31: RAM); 0 once linked |
| `crc`, `hcrc` | zlib CRC-32 of the bytes after the header; of the header's bytes 4 .. 59 |

**Where it goes.** Flash `0x097000..0x0DFFFF` (292 KiB, 73 sectors; Felucca's store before, never written by a firmware update),
one image after another on 4 KiB sectors; BEEP takes one (most of it the sine table and the descriptor, which every plugin
carries). The `.data` and `.bss` of all images share a pool of **2 KiB** at RAM `0x01C08000` (`.plug_ram`, first in RAM: the
images are linked for that address, and `app.ld` keeps it there). Keep a voice's state in `voice_t` (`ph[]`, `s[]`) as the
built-in engines do; the pool is for a little state of the whole plugin.

**At power-on**, before the saved song loads (a song finds its plugin): each image is checked (the header and its CRC, the
interface, linked where it lies, the CRC of the rest, the record inside the image and its descriptor, DSP and functions inside
it too), of one id the newest version is taken, and it is registered through `k_plugin_register` as any plugin is: the same
`k_plugin_check`, the FM-1's room (44.1 kHz, no FPU, 120 us a track a block). An FM-1 without images is not touched at all.

**SAFE start, without plugins:** HOME held at power-on (no saved song, no sound, as before), or by itself the start after a run
with plugins that ended in a fault, a watchdog reset or a boot that never got healthy (once; `plugs` says `boot=failed`).
`plugoff N` keeps one image off from then on; `plugerase N` removes one that is not live.

**Installing** (the console, `fm1_console.c`; the FM-1 refuses while it plays: each erase stops the sound):

| Command | Reply |
| --- | --- |
| `plugs` | `plugs n= live= free= ram= pool= boot= xip=`, then a line an image: `plug N at= bytes= uid= v= kind= ram= state= on= [err=] [engine= name=]` |
| `plugput SIZE RAM` | the FM-1 picks the place and the RAM, erases the sectors: `ok base=0x.. ram=0x..` |
| `plugdata HEX` | 27 bytes a line, written a page at a time: `ok n=` |
| `plugend CRC` | the CRC-32 of all of it; the header checked, read back through XIP, the magic written: `ok n= uid= live=` |
| `plugoff N`, `plugon N`, `plugerase N` | from the next start (on also takes it now when it can) |

A plugin whose id is live already (a new version) is written but plays from the next start, the newest of them; erase the old
one then. A Web IDE does what `fm1_plugin.py` does: ask `plugput`, add `base` (or `ram`, bit 31) to each relocated word, set
the header's `base`, `ram`, `nreloc` 0 and both CRCs, send it.

**What each costs the firmware:** 2704 B of RAM (the pool 2048, the list of eight images 608), 6.6 KB of code; RAM headroom
8456 -> 5752 B.

## A minimal engine: BEEP (`plugins/beep`)

A sine with white noise mixed in: three parameters, one voice state, integers only. The whole plugin:

```c
#include "kreisel_sdk.h"

static void beep_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = 0;                                   /* the sine from its zero crossing */
    v->s[0] = (int32_t)(0x9E3779B9u ^ v->age);      /* the noise's seed: another for each note */
}

static void beep_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    /* the parameters, once a block: t->p[P_E0 + k] is edit[k], already in its range */
    const int32_t noise = t->p[P_E0] * 258;         /* 0 .. 127 -> Q15 */
    const int32_t sine = 32767 - noise;
    const int32_t level = t->p[P_E1] * 258;
    const int32_t dtn = t->p[P_E2];
    const uint32_t inc = dtn ? cents_inc(m->pitch16, dtn, m->fine) : m->inc;
    uint32_t ph = v->ph[0], i;
    int32_t st = v->s[0];
    for (i = 0; i < n; i++) {
        const int32_t x = mulq15(sine_i(ph), sine) + mulq15((int32_t)(noise32(&st) >> 17) - 16384, noise);
        out[i] += voice_amp(mulq15(x, level), m, i);   /* add, never overwrite: the voices sum */
        ph += inc;
    }
    v->ph[0] = ph;
    v->s[0] = st;
}

/* how it sounds (the DSP) */
static const engine_t BEEP_DSP = {.name = "BEEP", .note_on = beep_note_on, .render = beep_render};

/* what the host shows (the descriptor): name, id, parameters, UI hints */
static const KEngine BEEP_DESC = {
    .name = "BEEP",
    .uid = K_UID('b', 'e', 'e', 'p'),               /* yours forever; a lower-case letter in it (upper case is Hügelton's) */
    .page_title = {"TONE", "", ""},
    .edit = {
        {"NOIS", KF_PCT, 0, 127, 0, 0, 0},
        {"LVL", KF_PCT, 0, 127, 110, 0, 0},
        {"DTN", KF_INT, -50, 50, 0, 0, "ct"},
    },
    .macro = {0, 1, 2, 1},                          /* HOME's cards: NOIS LVL DTN LVL */
    .nparams = 3,
    .page2 = 2,                                     /* NOIS LVL on SOUND, DTN on EDIT */
    .nomod = K_NOMOD_FILTER | K_NOMOD_SHAPE,        /* it reads neither the cutoff nor the shape modulation */
};

/* the record: kind, its own version, the descriptor and the DSP, what it needs */
static const KPlugin BEEP = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &BEEP_DESC, &BEEP_DSP,
                             FS, 0, 4, 0};          /* the host's rate; no state of its own; ~4 us a voice a block; integers */

KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &BEEP; }
```

Build and try it:

```bash
cmake --build build --target kreisel_plugin_beep   # build/sdk_example/beep.so
./build/kreisel_sdk_example_test                    # loads it with the FM-1's room, plays A4 (440 Hz), noise, silence; a song
cp build/sdk_example/beep.so ~/.kreisel/plugins/    # the desktop app then offers BEEP after the built-in engines
```

A host that builds a plugin in (the FM-1 today) registers the same record at start, after `k_plugin_set_caps()`:
`if (k_plugin_register(&BEEP) < 0) { /* refused: see the table above */ }`.


## Screens: `KPluginExt` (2026-11-11)

`src/kreisel_plugin_ui.h` (in `sdk/kreisel_sdk.h`). A record's last field, `ext`, points at a `KPluginExt`: what a plugin adds
beyond its sound. Like the record it only grows at its end (`size`).

```c
typedef struct KPluginExt {
    uint16_t size, version;                  /* sizeof(KPluginExt), K_PLUGIN_EXT_VERSION */
    void (*attach)(const KHostApi *host);    /* at registration: what the device is (0: not wanted) */
    const KLayout *layouts; uint8_t nlayouts;
    void (*draw)(const KDraw *g, int id, const int16_t *values, int n);   /* a picture's drawing */
} KPluginExt;
```

### What the device is: `KHostApi`

The plugin is told what the device **has**, never its name: the sample rate, the room (RAM, time, voices), `has` (`K_NEED_FLOAT`:
a fast FPU), the screen's kind (`K_SCR_SQUARE` the FM-1's 240 x 240, `K_SCR_SMALL` about 320 x 170 .. 200, `K_SCR_LARGE` a
desktop) and size, its knobs and its inputs (`K_IN_KNOBS`, `K_IN_KEYS`, `K_IN_PAD`, `K_IN_TOUCH`, `K_IN_KEYBOARD`, `K_IN_MOUSE`).
`attach` gets a pointer the host keeps current (the screen is known once the UI starts): keep it, read it when you need it.

### Layouts: `KLayout`, `KWidget`

A layout is for a page (`K_LPAGE_SOUND`, `K_LPAGE_EDIT`; an effect's `K_LPAGE_FX`, a drum's `K_LPAGE_DRUM`: below) on the
kinds of screen in its mask; the first that matches is taken; a page with none gets the vanilla view. It is a grid of `cols` x `rows` cells (1 .. 16) over the page under the preset's line (the
preset and the engine's `< name >` stay the host's). Each widget takes a rectangle of cells:

| Widget | Bound to | Drawn as |
| --- | --- | --- |
| `K_W_KNOB` | a parameter | label, a ring with the value's arc, value |
| `K_W_SLIDER` | a parameter | upright when its cell is taller than wide, else lying; from the middle when bipolar |
| `K_W_SWITCH` | a parameter | on / off (KF_ONOFF; any other: off at its minimum) |
| `K_W_VALUE` | a parameter | the value as text (`K_ST_LARGE`: the big font) |
| `K_W_LABEL` | none | `text` |
| `K_W_ENV` | four parameters, `param` .. `+3` | the A D S R curve |
| `K_W_SCOPE` | none | the track's sound after your engine and its inserts, before the fader |
| `K_W_PICTURE` | none (`param`: its number) | your drawing (`draw`) |

Styles (bits): `K_ST_FLAT` (no rings, handles or rounding: a bar's fill is the value), `K_ST_FRAME`, `K_ST_NOLABEL`,
`K_ST_NOVALUE`, `K_ST_ACCENT`, `K_ST_LARGE`. Colours are the theme's, so a layout follows the theme and the track's colour.

- **The knobs still take the page's parameters four at a time, in `edit[]`'s order** (the FM-1's K1 .. K4: the row in hand is
  lit). Put a row's four where you like; keep them together if the device has knobs.
- A touch, a mouse, the cursor take a widget's parameter (the widget's cell is its node). A widget bound to the other page's
  parameter is drawn, not taken.
- Registration checks every table: a cell past its grid, a parameter it has not, a picture with no `draw`, a page its kind has
  not (an effect: `K_LPAGE_FX` alone; `K_LPAGE_DRUM`: a drum's alone; `K_WP_MIX`: an effect's alone): `K_PLUG_E_DESC`. A
  widget of a kind the host does not know is skipped (its cells stay empty), so a newer plugin runs on an older host.
- One table can serve every screen (a grid stretches); give a screen kind its own table where it should look different.

### Pictures: `KDraw`

Inside a picture's frame the plugin draws with the calls in `KDraw`: `fill`, `line`, `text` (the small font), `text_w`,
`scope` (the track's sound, as `K_W_SCOPE` shows it). Coordinates from (0, 0) to (`w` - 1, `h` - 1), integers; colours
`K_COL_BG`, `K_COL_FAINT`, `K_COL_LINE`, `K_COL_MID`, `K_COL_TEXT`, `K_COL_ACCENT`, `K_COL_ALERT`. Nothing leaves the frame (a
line that would is not drawn, a text that would not fit is not drawn). `lit`: the knobs hold the parameters it shows. A frame
has a budget of 400 calls: past it a call does nothing, so a picture can slow a frame, never stop it.

`draw(g, id, values, n)` gets the engine's values as stored (`edit[]`'s order) and only reads them. It runs on the UI's side
(the main loop), never in the audio. On the FM-1: no float, few calls (draw every other point on `K_SCR_SQUARE`).

### An example: FLAT (`plugins/flat`)

Flat sliders only; a scope over SOUND; over EDIT its own picture (the filter's curve from CUT and RESO). One table for every
screen but a large one, where the scope stands beside the sliders:

```c
#define SL(k, x, y, w, h) {K_W_SLIDER, K_ST_FLAT, k, x, y, w, h, 0}
static const KWidget SOUND_W[] = {
    {K_W_SCOPE, K_ST_FLAT, -1, 0, 0, 4, 2, 0},
    SL(0, 0, 2, 1, 3), SL(1, 1, 2, 1, 3), SL(2, 2, 2, 1, 3), SL(3, 3, 2, 1, 3),
};
static const KLayout LAYOUTS[] = {
    {K_SCR_LARGE, K_LPAGE_SOUND, 9, 4, 5, SOUND_L}, {K_SCR_LARGE, K_LPAGE_EDIT, 9, 4, 5, EDIT_L},
    {K_SCR_ANY, K_LPAGE_SOUND, 4, 5, 5, SOUND_W}, {K_SCR_ANY, K_LPAGE_EDIT, 4, 5, 5, EDIT_W},
};
static const KPluginExt FLAT_EXT = {sizeof(KPluginExt), K_PLUGIN_EXT_VERSION, flat_attach, LAYOUTS, 4, flat_draw};
static const KPlugin FLAT = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &FLAT_DESC, &FLAT_DSP, FS, 0, 14, 0, 0, &FLAT_EXT};
```

`tests/plugin_ui_test.c` draws it on the FM-1, the Zero, the handhelds and a desktop.

### An effect's insert: `K_LPAGE_FX` (2026-11-12)

An effect (`K_PLUG_EFFECT`) has the same `ext`; its only page is `K_LPAGE_FX`, and an effect with a SOUND, EDIT or DRUM layout
is refused. The host draws the table in the FX page in the place of the insert that holds the effect: the insert's name `FX1`,
the effect's name and its `< >` stay the host's, the table fills the rest of the insert's box (the FM-1's: beside the name, about
150 x 90; a handheld's: a short row; a desktop's rack: the unit's controls under its window).

- `param` is an index into the effect's `edit[]`, or `K_WP_MIX`: the insert's MIX (the host's wet / dry).
- Each widget is the FX page's item that holds its value, so the knobs, the cursor, a touch, the wheel turn it through the host's
  commands. The FM-1's K1 .. K4 still take the page's items four at a time: the insert's first row is its first two parameters
  (three where the ALGORITHM encoder picks the effect) and MIX, then rows of the rest; the widgets the knobs hold are lit. Put the
  first row together; give every parameter a widget (one with none cannot be reached on a device with no knobs).
- While the insert in hand has a table, no cards are drawn under the FX page (as on SOUND with a layout); the other insert keeps its
  own view (its table, unlit, or the host's).
- An insert's values are kept 0 .. 100 (`set_param` gets 0 .. 32767): a widget's bar, ring or picture reads them so, whatever
  `min` and `max` the descriptor shows them as. A picture's `values` are the effect's, in `edit[]`'s order; `draw` is the same call.
- The built-in effects have no tables: their FX page is as it was.

The example is `plugins/tilt` (TILT, id `tilt`: a tilt EQ, a drive, a sample hold, a level; integers, 28 bytes of state, no
delay memory). On the FM-1 the knobs' row is tall and the picture (the tilt's line) sits beside LVL:

```c
#define KN(k, x, y, w, h) {K_W_KNOB, 0, k, x, y, w, h, 0}
static const KWidget SQUARE_W[] = {
    KN(0, 0, 0, 1, 2), KN(1, 1, 0, 1, 2), KN(2, 2, 0, 1, 2), KN(K_WP_MIX, 3, 0, 1, 2),   /* K1 .. K4 */
    {K_W_PICTURE, K_ST_FLAT, 0, 0, 2, 3, 1, 0}, {K_W_VALUE, 0, 3, 3, 2, 1, 1, 0},
};
static const KLayout LAYOUTS[] = {
    {K_SCR_SQUARE, K_LPAGE_FX, 4, 3, 6, SQUARE_W}, {K_SCR_SMALL, K_LPAGE_FX, 7, 1, 6, SMALL_W},
    {K_SCR_LARGE, K_LPAGE_FX, 5, 4, 6, LARGE_W},
};
static const KPlugin TILT = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_EFFECT, &TILT_DESC, &TILT_DSP,
                             0, sizeof(Tilt), 6, 0, 0, &TILT_EXT};
```

### A drum's lane page: `K_LPAGE_DRUM` (2026-11-12)

A drum (`K_PLUG_DRUM`, or a synth whose descriptor says `drum`) may have a `K_LPAGE_DRUM` table: it is taken on the drum's
**SOUND** page, before a `K_LPAGE_SOUND` one. SOUND is a drum's lane page: Kreisel's DRUM shows the lane in hand there (its piece,
level, pan, tune, tone, decay, click, attack and pitch sweep; its ENV 1 shows four of them again, its EDIT is the kit's). The host
writes the lane in hand over the table (`Lane 1  BD`, the names of `desc->lanes`); the knobs, the cursor, a touch take SOUND's
parameters as on any SOUND table. EDIT stays the kit's (`K_LPAGE_EDIT`).

An outside drum's first ten parameters are each lane's own (2026-11-12, *A synth that is a drum*): the table shows and turns the
lane in hand's, the lane named over it (the one the keys play); its parameters from the eleventh on are the track's, as DRUM's kit.

## Still to come

- A layout for the built-in engines through the same tables (their hand-drawn FM-1 views stay until then).
- The FM-1's images on a device (*On the FM-1*: built, not yet run); installing them
  from the web IDE (the same steps as `fm1_plugin.py`; the installer checks `k_plugin_fits` against the device's room first);
  a screen that lists them (the console only for now).
- Effect buses: track inserts are supported; shared effect buses are not yet plugin-routed.
- What the screen says for a track kept because the song's engine is newer than the one here (`k_track_newer`): it says NO
  PLUGIN for now.
- Measuring `cost` on the device for you (a console command), instead of declaring it by hand.


## Effects

The effect DSP signature is in `src/kreisel_effect.h`, separate from the voice-based synth `engine_t`.
It provides `init(state, memory, sample_rate)`, `process(state, in, out, frames)`,
`reset(state)`, `set_param(state, id, value)`, and optional `mem_size(rate)`.
The processing stream is Q15 in `int32_t`, mono, in-place permitted, up to 32 frames per call.
No heap allocation, blocking, libc calls or floating point inside `process`.
The host owns wet/dry, the two per-track insert slots, and routing.

Built-in effect types 1..4 retain their original song meaning:
DIST=Drive, CHO=Flanger, DLY=Delay, REV=Room. Types 5..15 add Gain, Filter, Fuzz,
Auto Wah, Crush, Phaser, Tremolo, Wobble, Noise, Pitch and Amp. These DSPs
are imported from Pendel (the author's own, relicensed MPL-2.0 in Kreisel's copy).
The FX page exposes all effect parameters through additional rows while preserving the
original two parameter positions and mix controls in older song files.

Their values (2026-11-12: every effect but FILTER and AMP had two; each has a third, which at its middle, 50, sounds bit for bit
as before it was there: `tests/fx_golden_test.c`). On the FM-1 an insert's knobs are K1..K3 its values, K4 MIX.

| Effect | Values | The third |
| --- | --- | --- |
| DIST | DRIV TONE LVL | (as it was) |
| CHO | RATE FDBK DPTH | the sweep's depth, 0 .. 1.5x (1x at 50) |
| DLY | TIME FDBK SYNC | a switch: on, TIME steps through the song's tempo, 1/32 .. 1/1 with triplets and dotted (a division longer than the line is halved until it fits: 1 s, the FM-1's 0.27 s) |
| REV | SIZE DCAY TONE | the tail's damping: darker below 50, brighter above |
| GAIN | GAIN PAN SAT | PAN: a balance of the track after its inserts (the chain is mono), as far as MIX lets it; SAT a switch, a soft clip after the gain |
| FILTER | CUT RES MODE | (as it was) |
| FUZZ | FUZZ BIAS TONE | a tilt after it: a low-pass below 50 (to ~430 Hz), the highs raised above (to 2x over ~860 Hz) |
| WAH | SENS RES MODE | a switch, Env / LFO: an LFO sweeps it, SENS then its rate (0.05 .. 12.8 Hz) |
| CRUSH | BITS DWN TONE | the same tilt, after the crush |
| PHASER | RATE DPTH FDBK | the feedback, 0 .. 0.8 (0.34 at 50) |
| TREMOLO | RATE DPTH WAVE | a switch, Sine / Sqr (a square, its edges eased) |
| WOBBLE | RATE DPTH SYNC | a switch: on, RATE is a division of the tempo (a wow a division) |
| NOISE | HISS CRKL TONE | the tilt, on the hiss and crackle only |
| PITCH | SEMI FDBK FINE | -50 .. +50 cents |
| AMP | MODE GAIN BASS MID TREB LVL CAB | (as it was) |

A switch is a `KF_ONOFF` value kept 0 .. 100 like the rest: on above 50, off at its default 50 and below (a detent turns it on,
to 100, or off, to 0; `names` may call its two states, "Env" "LFO"). A switch labelled `SYNC` makes the effect's first value a
note division on the screen (`KF_FXDIV`) while it is on. The song's tempo reaches the built-in effects only:
`set_param(state, PD_HOST_TEMPO, bpm)` (`third_party/pendel-dsp/pd_dsp.h`), sent when the tempo changes; an outside effect is
not sent an id past its parameters.

Effect ids live in a separate registry from sound-engine ids. `k_effect_at(type)`
returns an effect by type (0 means NONE); `k_plugin_register` accepts
`K_PLUG_EFFECT` and returns its effect type, while synth registration still
returns its engine index. An effect's `KPlugin.dsp` points to a `KEffectDSP`.
For synths it still points to Felucca's `engine_t`.

An effect's screens are **vanilla host UI**, generated from `KEngine.edit[]`, unless it lays out its insert (`K_LPAGE_FX`, above).
The FX page starts with the two legacy slot rows (TYPE, P0, P1, MIX), then
offers the remaining parameters in four-control rows. Unused cells are hidden.
Up to ten parameters per insert are supported; all values persist with the song.
The host owns focus, navigation, routing, bypass and wet/dry.

The built-in Pendel effects publish `fs`, `ram`, and provisional `cost`
estimates in `KPlugin`. The FM-1 host's `KDeviceCaps` limits allocation
and execution; an effect that does not fit is bypassed safely. Cost figures
are budget estimates, **not yet measured on FM-1 hardware**. The large
delay effect may not fit the FM-1's current 16 KiB plugin RAM allowance.

## A synth that is a drum

A `K_PLUG_SYNTH` plugin may declare itself a drum: `desc->drum = 1` and its lanes named in `desc->lanes` (two letters each; the
host plays eight). The host then treats its track as a drum track: the DRUM sequencer page with lanes, not the piano roll; on a
device with a key bed (the FM-1) a black key picks a lane and plays it and the white keys are that lane's sixteen steps, on every
page. Each lane is played as a note of the synth's own voices, a step long: `lane_notes[lane]`, or when it is 0 (or the record is
older and has no such field) General MIDI's: BD 36, SD 38, CH 42, OH 46, CP 39, LT 45, HT 50, RS 37. A synth that says it is a drum
but names no lanes is refused (`K_PLUG_E_KIND`). The arpeggiator does not run on such a track.

**Values for each lane** (2026-11-12, `K_ENGINE_API_MINOR` 4). Each lane has its own values of the drum's first ten parameters
(`edit[0]` .. `edit[9]`, fewer when it has fewer: `k_plugin_lane_params`); the rest are the track's, one set for the kit (as
Kreisel's DRUM: ten values a lane, four the kit's). SOUND, EDIT, HOME's macros, MIDI CC and the tools turn the lane in hand's
(the lane the keys pick, `Lane n` over the table). Nothing in the interface changed: while the host calls the drum for a voice
(`note_on`, `amp`, `render`) the part's `t->p[P_E0 + k]` holds that voice's lane's values; in `block` and `post` (once a part,
no voice) lane 1's. So a drum that reads `t->p` in those calls plays each lane with its own values, built for any minor; one
that keeps something worked out from `t->p` in `block` for all its voices hears lane 1's there (work it out in `note_on` or
`render` instead). A track keeps one drum's lanes: turning to another engine and back finds them; a second outside drum on the
same track takes them over (from its track values) when its values are first reached. A song keeps them after `TRAK`'s values
(by those parameters' ids); a song from before gives every lane the track's values.

