/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* BEEP, the SDK's smallest example (docs/PLUGIN_SDK.md: A minimal engine): a sine, with white noise mixed in.
 * Three parameters: NOIS (how much noise), LVL (the level) on SOUND, DTN (a detune in cents) on EDIT.
 * Integer only, no division in the sample loop; no state but the voice's own (ph[0], s[0]). tests/sdk_example_test.c loads it. */
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
