/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's PHASE: phase distortion. Two lines, each a cosine read through a bent phase: DCW (the digitally
 * controlled wave) bends it from a sine (0) towards the line's wave (127): SAW, SQR, PLS, DSIN (a double sine), SPLS (saw-pulse),
 * and the three resonant waves RSAW RTRI RTRP (a sine at a rising ratio, windowed by a saw, a triangle, a trapezoid). Line 2 a
 * number of semitones from line 1 (PITCH) and detuned (DTN); LINE mixes them or ring-modulates line 1 by line 2. As phase-distortion synths do: three
 * envelopes, each shared by the two lines and each line taking its own amount: DCO (pitch: DCO1, DCO2 semitones at the top), DCW
 * (the wave: DCW1, DCW2 how far it opens at the top) and DCA (the level: DCA1, DCA2; the host's amplitude envelope, ENV 2's A D S R).
 * The DCW envelope is ENV 1's A D S R, the DCO's ENV 2's items 4 .. 7 (P_X_A ..); both run here, once a block. A sub square an
 * octave under, a wavefolder, noise and a drive; glide. Its parameters, on P_E0 .. :
 *   SOUND  0 PITCH, 1 GLIDE, 2 DTN, 3 LINE (MIX RING) | 4 WAVE1, 5 DCO1, 6 DCW1, 7 DCA1 | 8 WAVE2, 9 DCO2, 10 DCW2, 11 DCA2
 *   EDIT   12 SUB, 13 FOLD, 14 NOIS, 15 DRV | 16 KTR (the DCW with the key), 17 LEVEL, 18 VEL
 * Kreisel's own, after Felucca-Engines' PHASE (MIT, the same author). Integer only; the bent phase's divisions are taken once a
 * block (reciprocals), none a sample. Included by kreisel_audio.c as the other engines are. */

static void kphase_note_on(track_t *t, voice_t *v)
{
    v->ph[1] = v->ph[0];
    v->ph[2] = 0;
    /* the DCW envelope's level (Q24, s[3]) and its stage (s[4]), the DCO's (s[5], s[6]): each attacks again. A voice that was
     * silent starts them from 0; one still sounding (a retrigger or a steal: the host keeps its DCA's env, which it zeroes before
     * this for a silent one) rises from where they are, as the DCA does, or the wave and the pitch would jump. */
    if (!v->env) { v->s[3] = 0; v->s[5] = 0; }
    else { v->s[3] = clamp(v->s[3], 0, 1 << 24); v->s[5] = clamp(v->s[5], 0, 1 << 24); }
    v->s[4] = 1;
    v->s[6] = 1;
    v->s[7] = t->glide_last ? t->glide_last : INT32_MIN;   /* glide: from the last note's pitch (1/16 st << 8), or none yet */
    if (!v->s[2]) v->s[2] = 0x5A5A5A5 + (int32_t)v->age;
}

/* a line's bend: the point d (a phase) where the first half of the cosine ends, and the reciprocals that map the two parts */
typedef struct { uint32_t d, k1, k2; } kpd_t;   /* (k1 = 2^47 / d < 2^22, k2 = 2^47 / (2^32 - d) <= 2^16: 32 bits) */
static inline void kphase_bend(kpd_t *b, int32_t dcw)   /* dcw 0 .. 32767 */
{
    b->d = 0x80000000u - (uint32_t)(k_smul(dcw, 0x7C000000) / 32767);   /* 0.5 (no bend) .. ~0.016 */
    b->k1 = (uint32_t)(((uint64_t)0x80000000u << 16) / b->d);
    b->k2 = (uint32_t)(((uint64_t)0x80000000u << 16) / (0xFFFFFFFFu - b->d + 1u));
}
static inline uint32_t kphase_warp(const kpd_t *b, uint32_t ph)   /* the bent phase */
{
    return ph < b->d ? k_umulsh(ph, b->k1, 16) : 0x80000000u + k_umulsh(ph - b->d, b->k2, 16);
}
static inline int32_t kphase_cos(uint32_t q) { return -sine_i(q + 0x40000000u); }   /* -cos: from the bottom */

static inline int32_t kphase_line(uint32_t w, uint32_t ph, const kpd_t *b, int32_t dcw)
{
    switch (w) {
    case 1: {   /* SQR: each half bent alike */
        uint32_t h = ph << 1, q = kphase_warp(b, h) >> 1;
        return kphase_cos((ph & 0x80000000u) ? 0x80000000u + q : q);
    }
    case 2: { uint32_t q = kphase_warp(b, ph); return kphase_cos(q < 0x80000000u ? q : 0x80000000u); }   /* PLS: held at the top */
    case 3: return kphase_cos(kphase_warp(b, ph << 1));                                                 /* DSIN */
    case 4: { uint32_t q = kphase_warp(b, ph); return ph < 0x80000000u ? kphase_cos(q) : kphase_cos(q >> 1); }   /* SPLS */
    case 5: case 6: case 7: {   /* the resonant three: a sine at 1 + DCW x 14 times the pitch, under a window */
        uint32_t r = 65536u + (uint32_t)dcw * 28u, q = k_umulsh(ph, r, 16);
        int32_t win = w == 5 ? 32767 - (int32_t)(ph >> 17) : w == 6 ? (ph < 0x80000000u ? (int32_t)(ph >> 16) : (int32_t)(~ph >> 16)) :
                      (ph < 0x40000000u ? (int32_t)(ph >> 15) : ph > 0xC0000000u ? (int32_t)(~ph >> 15) : 32767);
        return mulq15(kphase_cos(q) + 32767, win > 32767 ? 32767 : win) - 16384;
    }
    default: return kphase_cos(kphase_warp(b, ph));   /* SAW */
    }
}

/* PHASE DCW is a true second ADSR. It runs once per 32-sample block,
 * independently of the DCA envelope's v->stage and v->env. The host maps
 * ENV1 A/D/S/R into P_E2_DLY/HOLD/CRV/AMT for PHASE only.
 * Q24 level / Q15 result, the same integer rate tables as the host DCA. */
/* an envelope of the engine's own (DCW, DCO), A D S R as the host's tables give them, once a block; s[lv] its level (Q24),
 * s[lv + 1] its stage. The release follows the note-off. Its value: 0 .. 32767 */
static int32_t kphase_eg_tick(voice_t *v, int lv, int16_t a, int16_t d, int16_t su, int16_t r)
{
    int32_t level = v->s[lv], stage = v->s[lv + 1];
    const int32_t sustain = (int32_t)su << 17;
    if (!v->gate && stage != 3) stage = 3;
    switch (stage) {
    case 1:
        level += (int32_t)ENV_LIN[a & 127];
        if (level >= (1 << 24)) { level = 1 << 24; stage = 2; }
        break;
    case 2:
        level += mulq16(sustain - level, ENV_EXP[d & 127]);
        break;
    case 3:
        level -= mulq16(level, ENV_EXP[r & 127]);
        if (level < (1 << 12)) { level = 0; stage = 0; }
        break;
    default:
        level = 0;
        break;
    }
    v->s[lv] = level;
    v->s[lv + 1] = stage;
    return level >> 9;
}

static void kphase_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const uint32_t w1 = (uint32_t)clamp(p[P_E4], 0, 7), w2 = (uint32_t)clamp(p[P_E8], 0, 7);
    const int32_t semi = clamp(p[P_E0], -24, 24), det = clamp(p[P_E2], 0, 50), ring = p[P_E3] != 0;
    const int32_t l1 = clamp(p[P_E7], 0, 127) * 193, l2 = clamp(p[P_E11], 0, 127) * 193, sub = clamp(p[P_E12], 0, 127) * 160;
    const int32_t fold = clamp(p[P_E13], 0, 127), nz = clamp(p[P_E14], 0, 127) * 120, drv = clamp(p[P_E15], 0, 127), drive = 32768 + drv * 512;
    const int32_t lev = clamp(p[P_E17], 0, 127) * 258, vs = clamp(p[P_E18], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    /* the three envelopes: DCW and DCO here, DCA the host's (voice_amp). Each line takes its own amount of the shared one. */
    const int32_t dcw_env = kphase_eg_tick(v, 3, p[P_E2_DLY], p[P_E2_HOLD], p[P_E2_CRV], p[P_E2_AMT]);
    const int32_t dco_env = kphase_eg_tick(v, 5, p[P_X_A], p[P_X_D], p[P_X_S], p[P_X_R]);
    const int32_t dmod = (clamp(p[P_E16], 0, 127) * (v->pitch16 - 60 * 16) >> 6) + m->cutoff;   /* KTR, and the LFO's FLT */
    const int32_t d1 = clamp(k_mulsh(clamp(p[P_E6], 0, 127) * 258, dcw_env, 15) + dmod, 0, 32767);
    const int32_t d2 = clamp(k_mulsh(clamp(p[P_E10], 0, 127) * 258, dcw_env, 15) + dmod, 0, 32767);
    const int32_t o1 = (clamp(p[P_E5], -24, 24) * 16 * dco_env) >> 15, o2 = (clamp(p[P_E9], -24, 24) * 16 * dco_env) >> 15;   /* 1/16 st */
    uint32_t inc1, inc2, ph1 = v->ph[0], ph2 = v->ph[1], ph3 = v->ph[2], i;
    int32_t nst = v->s[2], base = m->pitch16;
    kpd_t b1, b2;
    {   /* glide: the pitch moves from the last note's to this one's, slower as GLIDE rises */
        const int32_t g = clamp(p[P_E1], 0, 127), target = m->pitch16 * 256;
        int32_t cur = v->s[7] == INT32_MIN || !g ? target : v->s[7];
        if (g) { const int32_t dl = target - cur; const int32_t st = (int32_t)((k_smul(dl, 65536 / (1 + g * g / 8)) + 32768) >> 16); cur += st ? st : (dl > 0) - (dl < 0); }   /* (a unit at least: an upward glide reaches its note) */
        v->s[7] = cur;
        t->glide_last = cur;
        base = cur >> 8;
    }
    if (base == m->pitch16 && !o1 && !det) inc1 = m->inc;
    else {   /* (DTN half each way, line 1 down and line 2 up: the pitch heard stays on the note) */
        inc1 = det ? cents_inc(clamp(base + o1, 0, 2047), -(det / 2), 0) : pitch_inc((uint32_t)clamp(base + o1, 0, 2047));
        if (m->fine) inc1 += (uint32_t)((int32_t)(inc1 >> 12) * m->fine);
    }
    inc2 = cents_inc(clamp(base + semi * 16 + o2, 0, 2047), det - det / 2, 0);
    if (m->fine) inc2 += (uint32_t)((int32_t)(inc2 >> 12) * m->fine);
    /* DCW glides from the last block's to this one's across the block (s[0], s[1]: the last, + 1; 0 none yet): a step at the
     * block's edge was heard as a click, most on the resonant waves, whose inner sine's phase jumps with it (ph x ratio). The
     * resonant ratio follows it each sample, the bend (its divisions) each 8. */
    int32_t c1 = v->s[0] ? v->s[0] - 1 : d1, c2 = v->s[1] ? v->s[1] - 1 : d2;   /* where each line's DCW is now, << 8 below */
    const int32_t st1 = ((d1 - c1) * 256) / (int32_t)(n ? n : 1), st2 = ((d2 - c2) * 256) / (int32_t)(n ? n : 1);
    c1 *= 256; c2 *= 256;
    v->s[0] = d1 + 1; v->s[1] = d2 + 1;
    for (i = 0; i < n; i++) {
        int32_t a, b, s, e1, e2;
        c1 += st1; c2 += st2;
        e1 = c1 >> 8; e2 = c2 >> 8;
        if (!(i & 7)) { kphase_bend(&b1, e1); kphase_bend(&b2, e2); }
        a = kphase_line(w1, ph1, &b1, e1); b = kphase_line(w2, ph2, &b2, e2);
        s = ring ? mulq15(mulq15(a, b), l1) : mulq15(a, l1) + mulq15(b, l2);
        ph1 += inc1;
        ph2 += inc2;
        if (sub) { s += mulq15(ph3 < 0x80000000u ? 16000 : -16000, sub); }
        ph3 += inc1 >> 1;
        if (nz) s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (fold) {   /* a wavefolder: the signal raised, then folded back where it passes the rails */
            int32_t x = s + k_mulsh(s, fold * 400, 15);   /* (64 bits: s to ~66000 x 50800 passes 2^31) */
            while (x > 32767 || x < -32767) x = x > 32767 ? 65534 - x : -65534 - x;
            s = x;
        }
        if (drv) s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        s = mulq15(s, lev);
        s += s >> 1;   /* x 1.5 (+3.5 dB): its near-sines sounded quieter than the other engines at the same LEVEL */
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        s = s > 65535 ? 65535 : s < -65535 ? -65535 : s;   /* (voice_amp's product: below 2^31 with the amp at full) */
        out[i] += voice_amp(s, m, i);
    }
    v->ph[0] = ph1;
    v->ph[1] = ph2;
    v->ph[2] = ph3;
    v->s[2] = nst;
}

static const engine_t ENG_KPHASE = {   /* (the host takes its parameters from Kreisel's table, src/engines_data.c) */
    .name = K_ENGINE_NAME_KPHS,
    .page_title = {"LINES", "DCW"},
    .note_on = kphase_note_on,
    .render = kphase_render,
    .knob = {P_E6, P_E10, P_ATK, P_REL},
    .keep = 0x83,                /* the glide (s[7]); the DCW where it was (s[0], s[1]): a retrigger glides on from there */
};
