/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's TRIO: three oscillators after the three-voice sound chips of the early home computers: each its own wave (triangle,
 * saw, pulse, pitched noise, and the chip's combined waves saw AND triangle, pulse AND saw) and level; OSC 2 and 3 a number of
 * semitones from OSC 1 (INT2, INT3) and detuned either way (DTN); MOD: OSC 1 ring-modulated by OSC 3 (its sign flipped by OSC 3's
 * top phase bit, as the chip did), OSC 2 hard-synced to OSC 1, or both; then a 12 dB state-variable filter, low-, band-,
 * high-pass or notch, with key tracking, a drive, the level and the velocity. Its parameters, on P_E0 .. (SYNTH's order):
 *   SOUND  0 INT2, 1 INT3 (-24 .. 24 st), 2 DTN (0 .. 50 ct), 3 MOD (OFF RING SYNC BOTH) | 4 W1, 5 W2, 6 W3 (TRI SAW PLS NOI
 *          S&T P&S), 7 PW | 8 LVL1, 9 LVL2, 10 LVL3
 *   EDIT   11 MODE (LP BP HP NOTCH), 12 CUT, 13 RES, 14 KTR, 15 DRV, 16 ENV (the host's ENV 1 -> filter: not read here),
 *          17 LEVEL, 18 VEL
 * Kreisel's own, after Felucca-Engines' TRIO (MIT, the same author), which picked the three waves together from sixteen sets.
 * The saw and the pulse are band-limited (dsp.c's polyBLEP), the triangle, the noise and the combined waves alias as the chip's
 * did. Integer only; included by kreisel_audio.c as the other engines are. */
enum { KT_TRI, KT_SAW, KT_PLS, KT_NOI, KT_ST, KT_PS };

static void ktrio_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0];
    v->ph[2] = v->ph[0];
    v->s[0] = v->s[1] = 0;                            /* filter */
    if (!v->s[2]) v->s[2] = 0x3456789 + (int32_t)v->age;   /* the noise's state */
    v->s[3] = 0;                                      /* the noise's held value */
}

static inline int32_t ktrio_wave(uint32_t w, uint32_t ph, uint32_t inc, uint32_t pw, const int32_t *noise)
{
    switch (w) {
    case KT_TRI: return osc_tri(ph);
    case KT_SAW: return osc_saw(ph, inc);
    case KT_PLS: return osc_pulse(ph, inc, pw);
    case KT_NOI: return *noise;
    case KT_ST: { int32_t tri = (int32_t)(((ph & 0x80000000u) ? ~ph : ph) >> 15) & 0xFFFF; return ((int32_t)(ph >> 16) & tri) - 16384; }
    default: return (ph >= pw ? (int32_t)(ph >> 16) : 0) - 16384;   /* pulse AND saw */
    }
}

static void ktrio_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const uint32_t w1 = (uint32_t)clamp(p[P_E4], 0, 5), w2 = (uint32_t)clamp(p[P_E5], 0, 5), w3 = (uint32_t)clamp(p[P_E6], 0, 5);
    const int32_t i2 = clamp(p[P_E0], -24, 24), i3 = clamp(p[P_E1], -24, 24), det = clamp(p[P_E2], 0, 50);
    const int32_t l1 = clamp(p[P_E8], 0, 127) * 110, l2 = clamp(p[P_E9], 0, 127) * 110, l3 = clamp(p[P_E10], 0, 127) * 110;   /* Q15: three at 127 ~ 1.3 (the filter's knee) */
    const int32_t mod = clamp(p[P_E3], 0, 3), ring = mod & 1, sync = mod >> 1;
    const uint32_t mode = (uint32_t)clamp(p[P_E11], 0, 3);
    const int32_t res = clamp(p[P_E13], 0, 120), fk = 8192 - res * 7600 / 127, drv = clamp(p[P_E15], 0, 127), drive = 32768 + drv * 512;
    const int32_t lev = clamp(p[P_E17], 0, 127) * 258, vs = clamp(p[P_E18], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    int32_t cut = (clamp(p[P_E12], 0, 127) << 8) + m->cutoff + (clamp(p[P_E14], 0, 127) * (v->pitch16 - 60 * 16) >> 4);
    int32_t pwd = (int32_t)((uint32_t)(clamp(p[P_E7], 0, 127) - 64) << 25) + (int32_t)((uint32_t)(m->shape - (64 << 8)) << 15);
    const uint32_t inc1 = m->inc;
    uint32_t inc2 = cents_inc(clamp(m->pitch16 + i2 * 16, 0, 2047), det, 0), inc3 = cents_inc(clamp(m->pitch16 + i3 * 16, 0, 2047), -det, 0), pw, i;
    uint32_t ph1 = v->ph[0], ph2 = v->ph[1], ph3 = v->ph[2];
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], nv = v->s[3];
    tsvf_t flt;
    if (m->fine) { inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine); inc3 += (uint32_t)((int32_t)(inc3 >> 12) * m->fine); }
    pwd = clamp(pwd, -0x70000000, 0x70000000);
    pw = 0x80000000u + (uint32_t)pwd;
    tsvf_coef(&flt, cut, res);
    for (i = 0; i < n; i++) {
        int32_t a, b, c, s;
        uint32_t nx1 = ph1 + inc1, nx3 = ph3 + inc3;
        if ((ph1 >> 28) != (nx1 >> 28))   /* the noise: a new value 16 times a cycle of OSC 1 */
            nv = (int32_t)(noise32(&nst) >> 17) - 16384;
        a = ktrio_wave(w1, ph1, inc1, pw, &nv);
        b = ktrio_wave(w2, ph2, inc2, pw, &nv);
        c = ktrio_wave(w3, ph3, inc3, pw, &nv);
        if (ring && (ph3 & 0x80000000u)) a = -a;      /* RING: OSC 3's top bit flips OSC 1 */
        if (sync && nx1 < ph1) ph2 = 0; else ph2 += inc2;   /* SYNC: OSC 2 restarts with OSC 1 */
        ph1 = nx1;
        ph3 = nx3;
        s = mulq15(a, l1) + mulq15(b, l2) + mulq15(c, l3);
        if (drv) s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        {   /* the filter's four outputs from the one SVF; the input saturates over half scale, as the chip's did */
            int32_t in = softclip(s) >> 1, v3, v1, v2, y, aa;
            v3 = in - ic2;
            v1 = (flt.a1 * ic1 + flt.a2 * v3) >> 13;
            v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3) >> 13);
            ic1 = clamp(2 * v1 - ic1, -150000, 150000);
            ic2 = clamp(2 * v2 - ic2, -150000, 150000);
            y = mode == 0 ? v2 : mode == 1 ? v1 : mode == 2 ? in - ((fk * v1) >> 12) - v2 : in - ((fk * v1) >> 12);
            aa = y < 0 ? -y : y;
            if (aa > 16000) { aa = 16000 + (softclip((aa - 16000) * 2) >> 1); y = y < 0 ? -aa : aa; }
            s = y * 2;
        }
        s = mulq15(s, lev);
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i) * 2;
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = ph3;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = nv;
}

static const engine_t ENG_KTRIO = {   /* (the host takes its parameters from Kreisel's table, src/engines_data.c) */
    .name = K_ENGINE_NAME_KTRI,
    .page_title = {"OSC", "FILTER"},
    .note_on = ktrio_note_on,
    .render = ktrio_render,
    .knob = {P_E12, P_E13, P_ATK, P_REL},
    .keep = 0x03,
};
