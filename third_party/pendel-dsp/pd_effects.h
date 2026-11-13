/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * From Pendel (the author's own), relicensed MPL-2.0 for Kreisel, 2026-11-07. */
/* More effects for the rack, three values each (a third added 2026-11-12: at its middle each sounds as before). Mono in, mono out; each keeps some of the dry sound where it belongs to
 * the effect (a flanger's comb, a room's direct sound), and the rack's mix sets the rest. A rate is an index into
 * PD_LFO_INC (0.05 .. 12.8 Hz, exponential). Delay lines are int16, sized to what the effect reads (8 KiB at most). */
#ifndef PD_EFFECTS_H
#define PD_EFFECTS_H

#include "pd_dsp.h"

enum { PD_FUZZ_FUZZ, PD_FUZZ_BIAS, PD_FUZZ_TONE, PD_FUZZ_PARAMS };   /* +12 .. +60 dB into a hard clip; bias: asymmetry, gating;
                                                                 * tone: a tilt (pd_effects.c), darker below the middle, brighter above */
extern const PendelDSP PD_FX_FUZZ;

/* the input's level sweeps a band-pass, 200 Hz .. 3.5 kHz; MODE (a switch) on: an LFO sweeps it, SENS its rate (0.05 .. 12.8 Hz) */
enum { PD_WAH_SENS, PD_WAH_RESO, PD_WAH_MODE, PD_WAH_PARAMS };
extern const PendelDSP PD_FX_WAH;

enum { PD_CRUSH_BITS, PD_CRUSH_DOWN, PD_CRUSH_TONE, PD_CRUSH_PARAMS };   /* 16 .. 2 bits; sample and hold 1x .. 50x; tone: a tilt after */
extern const PendelDSP PD_FX_CRUSH;

enum { PD_FLG_RATE, PD_FLG_FEEDBACK, PD_FLG_DEPTH, PD_FLG_PARAMS };   /* 0.6 .. 6.6 ms swept (depth 0 .. 1.5x of that), with the dry */
extern const PendelDSP PD_FX_FLANGER;

enum { PD_PHS_RATE, PD_PHS_DEPTH, PD_PHS_FEEDBACK, PD_PHS_PARAMS };   /* four all-pass stages around 260 Hz .. 1.6 kHz; feedback 0 .. 0.8 */
extern const PendelDSP PD_FX_PHASER;

enum { PD_TRM_RATE, PD_TRM_DEPTH, PD_TRM_WAVE, PD_TRM_PARAMS };   /* the level, by a sine; WAVE (a switch) on: a square (edges eased) */
extern const PendelDSP PD_FX_TREMOLO;

/* tape wow and flutter: the pitch, by two sines; SYNC (a switch): RATE steps through the tempo's divisions (PD_HOST_TEMPO), a
 * wow a division */
enum { PD_WOB_RATE, PD_WOB_DEPTH, PD_WOB_SYNC, PD_WOB_PARAMS };
extern const PendelDSP PD_FX_WOBBLE;

enum { PD_NOISE_HISS, PD_NOISE_CRACKLE, PD_NOISE_TONE, PD_NOISE_PARAMS };   /* tape hiss and record crackle, added; tone: theirs, a tilt */
extern const PendelDSP PD_FX_NOISE;

enum { PD_PIT_SEMI, PD_PIT_FEEDBACK, PD_PIT_FINE, PD_PIT_PARAMS };   /* -12 .. +12 semitones (two crossfaded taps), with the dry; fine
                                                                 * -50 .. +50 cents (pd_pitch_cents) */
extern const PendelDSP PD_FX_PITCH;

enum { PD_ROOM_SIZE, PD_ROOM_DECAY, PD_ROOM_TONE, PD_ROOM_PARAMS };   /* a small room: 15 .. 60 ms, an early tap and a dark tail; tone:
                                                                 * the tail's damping, darker below the middle, brighter above */
extern const PendelDSP PD_FX_ROOM;

/* an amp: preamp stages on a 12AX7 curve (PD_TUBE), a three-band tone stack (a state-variable filter's low, band and
 * high, centred where the model's stack dips), a power stage's soft limit, a speaker cabinet (a resonant high-pass at
 * 80 Hz, two low-passes); MODEL OFF passes the sound bit for bit. MODEL and CAB are choices (the app sets the middle of
 * their ranges). */
enum { PD_AMP_MODEL, PD_AMP_GAIN, PD_AMP_BASS, PD_AMP_MID, PD_AMP_TREBLE, PD_AMP_LEVEL, PD_AMP_CAB, PD_AMP_PARAMS };
enum { PD_AMP_OFF, PD_AMP_CLEAN, PD_AMP_CRUNCH, PD_AMP_LEAD, PD_AMP_MODELS };
extern const PendelDSP PD_FX_AMP;

/* the semitones a PD_PIT_SEMI value stands for (-12 .. 12), for the display */
static inline int32_t pd_pitch_semis(int32_t v) { return ((pd_clamp(v, 0, PD_PARAM_MAX) * 24 + 16383) >> 15) - 12; }
/* the cents a PD_PIT_FINE value stands for (-50 .. 50, 0 at the middle) */
static inline int32_t pd_pitch_cents(int32_t v)
{
    const int32_t d = pd_clamp(v, 0, PD_PARAM_MAX) - PD_PARAM_MID;
    return (d * 50 + (d < 0 ? -8192 : 8192)) / 16384;
}

#endif
