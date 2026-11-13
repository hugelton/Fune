/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's ANALOG: two oscillators, each with its own wave and level; OSC 2 a number of semitones from OSC 1 (PITCH), detuned
 * in cents (DTN) and, with SYNC, hard-synced to it; a sub oscillator (a square two octaves under, one under or one over OSC 1);
 * noise, white or darker (COLOR); drive; a two-pole state-variable filter (low-, band- or high-pass) with resonance and key
 * tracking; glide; the voice's level and how much the velocity moves it. Its parameters, on the engine's slots P_E0 .. (the
 * order the knobs turn them, K1 on):
 *   SOUND  0 PITCH (-24 .. 24 st), 1 GLIDE (0 .. 127, 0 none), 2 DTN (0 .. 127 ct), 3 SYNC (off / on),
 *          4 WAVE1, 5 LVL1, 6 WAVE2, 7 LVL2 (SAW SQR TRI SIN P12 P25 P37: the pulses 12.5, 25, 37.5 %; SQR 50 %),
 *          8 SUB, 9 SUBP (-2 -1 +1 octaves), 10 NOIS, 11 COLOR (0 white .. 127 dark)
 *   EDIT   12 CUT, 13 RES, 14 DRV, 15 KTR, 16 TYPE (LP BP HP), 17 ENV (the host's ENV 1 -> filter: not read here),
 *          18 LEVEL, 19 VEL (0: every note at full level, 127: as hard as it is played)
 * The host's SHAPE modulation (ENV DEST, LFO) moves the pulses' width (P12 .. P37).
 * Written for Kreisel after Felucca-Engines' ANALOG (MIT, the same author: its oscillators, drive, filter taps and soft knee,
 * from dsp.c), included by kreisel_audio.c after the Felucca-Engines, as they are: a host's translation unit. Integer only. */

static void kanalog_note_on(track_t *t, voice_t *v)
{
    v->ph[1] = v->ph[0];                              /* (in phase: SYNC starts them together) */
    v->s[0] = v->s[1] = 0;                            /* filter */
    if (!v->s[2])
        v->s[2] = 0x2345671 + (int32_t)v->age;        /* noise state */
    v->s[4] = t->glide_last ? t->glide_last : INT32_MIN;   /* glide: from the last note's pitch (1/16 st << 8), or none yet */
}

static inline int32_t kanalog_wave(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (w) {
    case 1: return osc_pulse(ph, inc, 0x80000000u);
    case 2: return osc_tri(ph);
    case 3: return osc_sine(ph);
    case 4: case 5: case 6: return osc_pulse(ph, inc, pw + (w - 4u) * 0x20000000u);   /* 12.5, 25, 37.5 % (+ SHAPE) */
    default: return osc_saw(ph, inc);
    }
}

/* the SVF's three taps (Simper's trapezoidal state-variable filter, as Felucca-Engines' analog_tap): 0 LP, 1 BP, 2 HP */
static inline int32_t kanalog_tap(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t ft)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return ft == 0u ? v2 : ft == 1u ? v1 : in - ((k * v1) >> 12) - v2;
}

static void kanalog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const uint32_t w1 = (uint32_t)p[P_E4] > 6u ? 0u : (uint32_t)p[P_E4], w2 = (uint32_t)p[P_E6] > 6u ? 0u : (uint32_t)p[P_E6];
    const int32_t semi = clamp(p[P_E0], -24, 24), sync = p[P_E3] != 0, det = clamp(p[P_E2], 0, 127);
    const int32_t l1 = clamp(p[P_E5], 0, 127) * 193, l2 = clamp(p[P_E7], 0, 127) * 193;   /* Q15, 0.75 at 127: both full drive the filter's knee */
    const int32_t nz = clamp(p[P_E10], 0, 127) * 200, sub = clamp(p[P_E8], 0, 127) * 193, subp = clamp(p[P_E9], 0, 2);
    const int32_t ncol = 32767 - clamp(p[P_E11], 0, 127) * 250;   /* the noise's one-pole: 32767 white .. ~1000 dark */
    const int32_t drv = clamp(p[P_E14], 0, 127), drive = 32768 + drv * 512, res = clamp(p[P_E13], 0, 127);
    const uint32_t ft = (uint32_t)clamp(p[P_E16], 0, 2);
    const int32_t fk = 8192 - res * 7600 / 127, lev = clamp(p[P_E18], 0, 127) * 258, vs = clamp(p[P_E19], 0, 127);
    int32_t cut = (clamp(p[P_E12], 0, 127) << 8) + m->cutoff + (clamp(p[P_E15], 0, 127) * (v->pitch16 - 60 * 16) >> 4);
    int32_t pwd = (int32_t)((uint32_t)(m->shape - (64 << 8)) << 15);   /* SHAPE: the pulses' width */
    uint32_t inc1 = m->inc, inc0, inc2, pw, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], i, isub;
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], nlp = v->s[3], velc;
    if (nlp > 32767 || nlp < -32768) nlp = 0;   /* (the slot another engine left: the noise's low-pass starts quiet) */
    tsvf_t flt;
    {   /* glide: the pitch moves from the last note's to this one's, slower as GLIDE rises (127: about a second and a half) */
        const int32_t g = clamp(p[P_E1], 0, 127), target = m->pitch16 * 256;
        int32_t cur = v->s[4] == INT32_MIN || !g ? target : v->s[4];
        if (g) { const int32_t dl = target - cur; const int32_t st = (int32_t)((k_smul(dl, 65536 / (1 + g * g / 8)) + 32768) >> 16); cur += st ? st : (dl > 0) - (dl < 0); }   /* (a unit at least: an upward glide reaches its note) */
        v->s[4] = cur;
        t->glide_last = cur;
        /* DTN spreads the two either way, half each (OSC 1 down, OSC 2 up): the pitch heard stays on the note (
         * all of it on OSC 2 sat the sound sharp, ~+25 ct at DTN 20) */
        const int32_t p16 = cur != target ? clamp(cur >> 8, 0, 2047) : m->pitch16;
        if (cur != target) {   /* inc0: the note itself (the sub's, which stays on it) */
            inc0 = pitch_inc((uint32_t)p16);
            if (m->fine) inc0 += (uint32_t)((int32_t)(inc0 >> 12) * m->fine);
        } else inc0 = m->inc;
        if (cur != target || det) {
            inc1 = det ? cents_inc(p16, -(det / 2), 0) : pitch_inc((uint32_t)p16);
            if (m->fine) inc1 += (uint32_t)((int32_t)(inc1 >> 12) * m->fine);
        }
        inc2 = cents_inc(clamp(p16 + semi * 16, 0, 2047), det - det / 2, 0);
    }
    if (m->fine) inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
    pwd = clamp(pwd, -0x0C000000, 0x50000000);   /* (12.5 % + it stays a pulse) */
    pw = 0x20000000u + (uint32_t)pwd;            /* P12's width; P25, P37 a quarter turn more each */
    isub = subp == 0 ? inc0 >> 2 : subp == 1 ? inc0 >> 1 : inc0 << 1;   /* SUBP: two octaves under, one under, one over */
    /* velocity: the host plays the voice as hard as its note (vel / 127); VEL 127 keeps that, 0 brings every note to full */
    velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;   /* Q12 */
    tsvf_coef(&flt, cut, res);
    for (i = 0; i < n; i++) {
        int32_t s = mulq15(kanalog_wave(w1, ph0, inc1, pw), l1) + mulq15(kanalog_wave(w2, ph1, inc2, pw), l2);
        uint32_t nx = ph0 + inc1;
        if (sub) s += mulq15(osc_pulse(ph2, isub, 0x80000000u), sub);
        if (sync && nx < ph0) ph1 = (uint32_t)(k_umul(nx, inc2) / (inc1 ? inc1 : 1u));   /* OSC 1 wrapped: OSC 2 restarts with it */
        else ph1 += inc2;
        ph0 = nx;
        ph2 += isub;
        if (nz) {   /* noise through a one-pole: COLOR darkens it */
            const int32_t x = (int32_t)(noise32(&nst) >> 17) - 16384;
            nlp += k_mulsh(x - nlp, ncol, 15);
            s += mulq15(ncol < 32767 ? nlp + ((nlp * (32767 - ncol)) >> 15) : x, nz);   /* (a little louder as it darkens) */
        }
        if (drv) s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        {   /* the filter, linear to half scale, then a soft knee */
            int32_t y = kanalog_tap(&flt, fk, s >> 1, &ic1, &ic2, ft), a = y < 0 ? -y : y;
            if (a > 16000) { a = 16000 + (softclip((a - 16000) * 2) >> 1); y = y < 0 ? -a : a; }
            s = y * 2;
        }
        s = mulq15(s, lev);
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = nlp;
}

static const engine_t ENG_KANALOG = {   /* (the host takes its parameters from Kreisel's table, src/engines_data.c) */
    .name = K_ENGINE_NAME_KANL,
    .page_title = {"OSC", "FILTER"},
    .note_on = kanalog_note_on,
    .render = kanalog_render,
    .knob = {P_E12, P_E13, P_ATK, P_REL},
    .keep = 0x13,                /* the filter (s[0], s[1]) and the glide (s[4]) */
};
