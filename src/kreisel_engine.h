/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_ENGINE_H
#define KREISEL_ENGINE_H
#include <stdint.h>

/* What an engine offers the UI: its two pages and eight parameters, as the Felucca-Engines (MIT) describe them
 * (tools/gen_engines.py reads them into engines_data.c). The formats keep Felucca-Engines' meanings and order. */
enum { KF_INT, KF_PCT, KF_BIPCT, KF_TIME, KF_LFOHZ, KF_CUTOFF, KF_DB, KF_SEMI, KF_ENUM, KF_BPM, KF_NOTE, KF_ONOFF, KF_OCT, KF_STEPS,
       KF_MNOTE,     /* KF_MNOTE (Kreisel's): a MIDI note as a name, 60 = C4 */
       KF_LINK_FENV,   /* (see below) */
       KF_CENTER,      /* a value either side of its default, shown as - 0 +: the default is 0 (DRUM's TUNE 64, PAN 50; the amp's bands) */
       KF_FXSEMI,      /* an effect's 0..100 standing for -12 .. +12 semitones (PITCH's SEMI: pd_pitch_semis' rounding) */
       KF_FADER,       /* (the host's: the values' table) a level in dB, K_LEVEL_0DB 0 dB (the mixer, HOME's LEVEL, a bus's RTN) */
       KF_COLOR,       /* (the host's) a track's colour, a name of the palette */
       KF_FXTYPE,      /* (the host's) an insert's effect, by its name (0 NONE) */
       KF_FXDIV };     /* (the host's) an effect's 0..100 standing for a note division, 1/32 .. 1/1 (a synced TIME or RATE) */
/* KF_LINK_FENV (Kreisel's) not the engine's own: the slot stands for the host's ENV 1 -> filter amount (ENV DEST FLT, 0..100,
 * 50 none), which the host turns there; an engine declares where it shows among its own */

typedef struct {
    const char *label;
    uint8_t fmt;                  /* KF_* */
    int16_t min, max, def;
    const char *const *names;     /* KF_ENUM: 0-terminated list */
    const char *unit;             /* KF_INT / KF_ENUM: an optional unit ("ct") */
} KParamDesc;
/* a switch (KF_ONOFF) on: an engine's 0/1, or an effect's kept 0..100 (on above the middle, off at its default 50); a switch may
 * name its two states (names[0] off, names[1] on: "Env" "LFO"), else On and Off */
static inline int k_switch_on(const KParamDesc *d, int v) { return d->max > 1 ? v > (d->min + d->max) / 2 : v != 0; }
static inline const char *k_switch_name(const KParamDesc *d, int v) { const int on = k_switch_on(d, v); return d->names ? d->names[on] : on ? "On" : "Off"; }
/* an effect's 0..100 as a division index, 0 .. 15 (as the effect takes it: pd_div_index of the value it is sent) */
static inline int k_fx_div_index(int v) { const int q = ((v < 0 ? 0 : v > 100 ? 100 : v) * 32767 + 50) / 100; return q * 16 >> 15; }

/* An engine's identity: a 32-bit id of four printable characters, one per engine whoever made it (K_UID('A','N','L','G')). Upper-case
 * letters and digits only are Hügelton Instruments'; others pick an id with a lower-case letter in it. The id never changes for an
 * engine and is never given to another: a saved song names its engines by id (never by index or slot), so an engine that is not
 * there, or has changed, cannot be mistaken for another. */
#define K_UID(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

#define K_ENGINE_PARAMS 20        /* the most parameters an engine has (SOUND and EDIT: Kreisel's ANALOG has nineteen) */
typedef struct {
    const char *name;             /* "ANALOG" */
    uint32_t uid;                 /* K_UID: its identity (not its place in the table) */
    const char *page_title[3];    /* what its rows of four are called ("OSC", "FLT"); "" a row that goes with the engine's own */
    KParamDesc edit[K_ENGINE_PARAMS];
    uint8_t drum;                 /* 1 = hits: the track plays lanes (a drum pattern), not notes (steps and a roll) */
    const char *lanes[10];        /* a drum engine: what its lanes are called (two letters); Kreisel plays K_LANES (8) of them */
    uint8_t macro[4];             /* its macros, declared by the engine: four of its parameters (0 .. nparams-1), what HOME's first row of
                                   * cards turns for the track in hand, in the order they sit on the cards */
    uint8_t nparams;              /* how many of edit[] it has: 8, or 12 (Kreisel's ANALOG) */
    uint8_t page2;                /* where its EDIT page starts: edit[0 .. page2-1] on SOUND (after ENGINE), the rest on EDIT */
    uint8_t nomod;                /* K_NOMOD_*: the host's modulations it does not read (ENV 1's and the LFO's FLT, SHP): shown dim; 0 all */
    uint8_t gate;                 /* 1: it plays a gate of its own (on at once, held, a short release: an organ); the track's ENV pages are not read */
} KEngine;
#define K_NOMOD_FILTER 1u         /* it reads no cutoff modulation (no filter, nothing in its place) */
#define K_NOMOD_SHAPE 2u          /* it reads no shape modulation */

#ifndef K_WITH_VECTOR
#define K_WITH_VECTOR 0           /* VECTOR (a 2D wavetable morph) is not in the MVP (2026-11-06): 1 builds it in (tools/gen_engines.py WITH_VECTOR alike) */
#endif
#ifndef K_WITH_ENSEMBLE
#define K_WITH_ENSEMBLE 0         /* ENSEMBLE (divide-down strings) is out of the MVP (2026-11-07, 0.0.1 Beta): 1 builds it in (tools/gen_engines.py WITH_ENSEMBLE alike) */
#endif
#define K_ENGINE_COUNT (7 + K_WITH_ENSEMBLE + K_WITH_VECTOR)   /* the built-in engines: ANALOG PHASE TRIO LOFI ORGAN (Kreisel's own, in the owner's order), ENSEMBLE (VECTOR) FM2, then DRUM; k_engine_count() with the plugins */
extern const KEngine K_ENGINE[K_ENGINE_COUNT];

/* The registry: how the rest of the code reaches an engine. Every lookup is checked, so a corrupt or foreign value (a song saved with an
 * engine that has been replaced) can never index out of the table or read another engine's parameters as its own. */
int k_engine_count(void);                     /* the engines a track can have: the built-in ones, then the plugins registered (kreisel_plugin.h) */
const KEngine *k_engine_at(int index);        /* the engine at an index; an index out of range gives the first engine (never 0) */
uint32_t k_engine_uid(int index);             /* its id; 0 for an index out of range */
int k_engine_find(uint32_t uid);              /* the index of the engine with that id, or -1: not here */
int k_param_clamp(const KParamDesc *d, int v);
int k_engine_macro(int index, int k);   /* the engine's k-th macro (0..3), as it declares it (KEngine.macro): which of its eight parameters (0..7) */
#endif
