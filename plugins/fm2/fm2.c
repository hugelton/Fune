/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM2, an outside plugin (not built in): two sine operators in series, OP2 (the modulator, with feedback) into OP1 (the
 * carrier). From the FM2 engine of feat/fm2-operator-engine (PR #4), made a plugin: built as a shared library that the
 * desktop hosts load (kreisel_plugin_load.c), id `kfm2`. Its parameters, on P_E0 .. :
 *   SOUND  0 PITCH, 1 RATIO (1/2 .. 16), 2 INDEX, 3 FEED, 4 ENV (the index follows the host's ADSR), 5 TUNE (cents), 6 MIX
 *   EDIT   7 LEVEL, 8 VEL, 9 KTR (the index by the key), 10 DRIVE
 * The host's FLT modulation (ENV 1, the LFO) moves the index, as PHASE's moves its DCW. Integer; no division in the sample loop. */
#include "kreisel_sdk.h"

static void fm2_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0];
    v->s[0] = 0;   /* OP2's feedback sample */
}

static void fm2_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const uint16_t RATIO_Q8[8] = {128, 256, 512, 768, 1024, 1536, 2048, 4096};
    const int16_t *p = t->p;
    const int32_t semi = clamp(p[P_E0], -24, 24), ratio = clamp(p[P_E1], 0, 7);
    const int32_t depth = clamp(p[P_E2], 0, 127), feedback = clamp(p[P_E3], 0, 127);
    const int32_t env = clamp(p[P_E4], 0, 127), tune = clamp(p[P_E5], -50, 50);
    const int32_t mix = clamp(p[P_E6], 0, 127), level = clamp(p[P_E7], 0, 127) * 258;
    const int32_t vs = clamp(p[P_E8], 0, 127), ktr = clamp(p[P_E9], 0, 127);
    const int32_t drive = 128 + clamp(p[P_E10], 0, 127) * 3;
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    uint32_t inc1 = semi ? cents_inc(clamp(m->pitch16 + semi * 16, 0, 2047), 0, 0) : m->inc;
    uint32_t inc2, ph1 = v->ph[0], ph2 = v->ph[1], i;
    int32_t last = v->s[0], index;
    uint64_t mi;
    if (semi && m->fine) inc1 = (uint32_t)((int64_t)inc1 * (4096 + m->fine) / 4096);
    /* the modulator: the carrier's pitch, detuned by TUNE, times the ratio (held under Nyquist, never wrapped) */
    mi = ((uint64_t)inc1 * (uint32_t)(32768 + tune * 19) * RATIO_Q8[ratio]) >> 23;
    inc2 = (uint32_t)(mi > 0x7FFFFFFFu ? 0x7FFFFFFFu : mi);
    /* the index: ENV 0 holds it, 127 follows the voice's ADSR; the host's FLT modulation and the key move it (once a block) */
    index = (int32_t)((int64_t)depth * ((127 - env) * 32767 + env * clamp(m->envq15, 0, 32767)) / (127 * 32767));
    index += m->cutoff / 256;
    index += ((int32_t)v->note - 60) * ktr / 128;
    index = clamp(index, 0, 127);
    for (i = 0; i < n; ++i) {
        const int32_t op2 = sine_i(ph2 + (uint32_t)((int64_t)last * feedback * 256));     /* FEED: OP2's phase by its last sample */
        const int32_t op1 = sine_i(ph1 + (uint32_t)((int64_t)op2 * index * 1536));        /* up to ~9.4 radians at INDEX 127 */
        int32_t s = mulq15(op1, 32767 - mix * 128) + mulq15(op2, mix * 128);
        if (drive != 128) s = softclip((s * drive) >> 7);                                  /* (a shift: no division a sample) */
        s = mulq15(s, level);
        if (velc != 4096) s = (int32_t)(((int64_t)s * velc) >> 12);
        out[i] += voice_amp(s, m, i);
        last = op2;
        ph1 += inc1;
        ph2 += inc2;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->s[0] = last;
}

static const engine_t FM2_DSP = {
    .name = "FM2",
    .page_title = {"OPERATORS", "OUTPUT"},
    .note_on = fm2_note_on,
    .render = fm2_render,
    .knob = {P_E2, P_E1, P_E3, P_E4},
};

static const char *const RATIOS[] = {"1/2", "1", "2", "3", "4", "6", "8", "16", 0};
static const KEngine FM2_DESC = {
    .name = "FM2",
    .uid = K_UID('k', 'f', 'm', '2'),
    .page_title = {"", "OPERATORS", "OUTPUT"},
    .edit = {
        {"PITCH", KF_SEMI, -24, 24, 0, 0, 0},
        {"RATIO", KF_ENUM, 0, 7, 1, RATIOS, 0},
        {"INDEX", KF_PCT, 0, 127, 50, 0, 0},
        {"FEED", KF_PCT, 0, 127, 0, 0, 0},
        {"ENV", KF_PCT, 0, 127, 80, 0, 0},
        {"TUNE", KF_INT, -50, 50, 0, 0, "ct"},
        {"MIX", KF_PCT, 0, 127, 0, 0, 0},
        {"LEVEL", KF_PCT, 0, 127, 120, 0, 0},
        {"VEL", KF_PCT, 0, 127, 64, 0, 0},
        {"KTR", KF_PCT, 0, 127, 0, 0, 0},
        {"DRIVE", KF_PCT, 0, 127, 0, 0, 0},
    },
    .macro = {2, 1, 3, 4},   /* INDEX RATIO FEED ENV */
    .nparams = 11,
    .page2 = 7,
    .nomod = K_NOMOD_SHAPE,  /* (it reads the FLT modulation, as its index) */
};
static const KPlugin FM2 = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &FM2_DESC, &FM2_DSP,
                            FS, 0, 12, 0};   /* built at the host's rate; no state of its own; ~12 us a voice a block (an estimate) */

KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &FM2; }
