/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's LOFI: a game console's voice. One chip oscillator (a pulse of four widths, the stepped triangle, a stepped saw),
 * moved the way such chips were moved, once a frame (60 a second): a pitch sweep, a vibrato, an arpeggio; and a noise channel
 * beside it (white, the long and the short LFSR of such chips, dust), its clock set by COLOR; then fewer bits, a lower sample
 * rate, a one-pole TONE, and an envelope that may move in sixteen steps. NOISE is no engine of its own any more: it is here.
 * Its parameters, on P_E0 .. (the order SYNTH turns them, after ENGINE):
 *   SOUND  0 WAVE (PLS TRI SAW OFF: the noise alone), 1 DUTY (12 25 50 75 %), 2 CRSH (the bits: 16 .. 1), 3 SRATE (0 .. 127: the sample held longer) |
 *          4 SWP (-64 .. 63), 5 VIB (0 .. 15), 6 ARP (OFF OCT MAJ MIN 5TH), 7 RATE (1 .. 8 frames an arp step) |
 *          8 NOISE (OFF WHITE LONG SHORT DUST), 9 NLVL, 10 COLOR, 11 TONE (0 .. 127)
 *   EDIT   12 STEP (off / on), 13 LEVEL, 14 VEL
 * Kreisel's own, after Felucca-Engines' LOFI and NOISE (MIT, the same author). Integer only; the chip's edges are kept (no band
 * limiting: the aliasing is the sound). Included by kreisel_audio.c as the other engines are. */
#define KL_FRAME (FS / 60)                            /* samples a frame */

static void klofi_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = 0;
    if (!v->s[0]) v->s[0] = 0x4001 + (int32_t)(v->age & 0x3FFF);   /* the LFSR (never 0) */
    v->s[1] = 0;                                      /* TONE's state */
    v->s[2] = v->s[3] = 0;                            /* the sample hold: count, value */
    v->s[4] = 0;                                      /* the sweep (1/16 st << 8) */
    v->s[5] = 0;                                      /* samples into the frame */
    v->s[6] = 0;                                      /* frames since the note: the arp and the vibrato */
    v->s[7] = 0;                                      /* dust: its decay */
}

/* the stepped envelope: sixteen levels, as such chips' volume */
static int32_t klofi_amp(track_t *t, voice_t *v, int32_t adsr)
{
    (void)v;
    return t->p[P_E12] ? (adsr >> 11) << 11 : adsr;
}

static void klofi_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    static const int8_t ARP[5][4] = {{0, 0, 0, 0}, {0, 12, 0, 12}, {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 7, 12, 7}};
    static const uint32_t DUTY[4] = {0x20000000u, 0x40000000u, 0x80000000u, 0xC0000000u};
    const int16_t *p = t->p;
    const uint32_t wave = (uint32_t)clamp(p[P_E0], 0, 3), duty = DUTY[clamp(p[P_E1], 0, 3)], noise = (uint32_t)clamp(p[P_E8], 0, 4);
    const int32_t bits = 16 - clamp(p[P_E2], 0, 7) * 2 - (p[P_E2] >= 7 ? 1 : 0), swp = clamp(p[P_E4], -64, 63), vib = clamp(p[P_E5], 0, 15);
    const int32_t arp = clamp(p[P_E6], 0, 4), rate = clamp(p[P_E7], 1, 8), nlvl = clamp(p[P_E9], 0, 127) * 200, color = clamp(p[P_E10], 0, 127);
    const int32_t tone = 2048 + clamp(p[P_E11], 0, 127) * 30000 / 127, hold = clamp(p[P_E3], 0, 127) / 8;   /* TONE: the one-pole's step, Q15 */
    const int32_t lev = clamp(p[P_E13], 0, 127) * 258, vs = clamp(p[P_E14], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    const uint32_t ninc = pitch_inc((uint32_t)clamp(400 + color * 12, 0, 2047)) << 2;   /* the noise's clock: low rumble .. hiss */
    uint32_t ph = v->ph[0], nph = v->ph[1], i, inc;
    int32_t lfsr = v->s[0], lp = v->s[1], hc = v->s[2], hv = v->s[3], frame = v->s[5], fcount = v->s[6], dust = v->s[7];
    {   /* once a frame: the sweep, the arp's step, the vibrato */
        int32_t p16, semi, vb;
        frame += (int32_t)n;
        while (frame >= KL_FRAME) { frame -= KL_FRAME; fcount++; v->s[4] += swp * 24; }
        v->s[4] = clamp(v->s[4], -48 * 16 * 256, 48 * 16 * 256);
        semi = ARP[arp][(fcount / rate) & 3];
        vb = vib ? k_mulsh(sine_i((uint32_t)fcount * 0x1999999Au), vib, 15) : 0;   /* 6 Hz (a tenth of a turn a frame, 60 a second), up to a semitone either way */
        p16 = m->pitch16 + semi * 16 + (v->s[4] >> 8) + vb;
        inc = pitch_inc((uint32_t)clamp(p16, 0, 2047));
        if (m->fine) inc += (uint32_t)((int32_t)(inc >> 12) * m->fine);
    }
    for (i = 0; i < n; i++) {
        int32_t s, nz = 0;
        switch (wave) {
        case 1: { int32_t st = (int32_t)(ph >> 27); s = (st < 16 ? st : 31 - st) * 2184 - 16380; } break;   /* the triangle, 32 steps */
        case 2: s = (int32_t)(ph >> 28) * 2184 - 16380; break;                                              /* a saw, 16 steps */
        case 3: s = 0; break;                                                                               /* OFF: the noise alone */
        default: s = ph < duty ? 14000 : -14000; break;                                                     /* the pulse */
        }
        ph += inc;
        if (noise) {
            uint32_t nx = nph + ninc;
            if (nx < nph) {   /* the noise's clock ticks */
                if (noise == 1) { uint32_t x = (uint32_t)lfsr; x ^= x << 7; x ^= x >> 9; x ^= x << 8; lfsr = (int32_t)x; }   /* white (unsigned: no signed overflow) */
                else if (noise == 4) { lfsr = (int32_t)((uint32_t)lfsr * 1103515245u + 12345u); if (((uint32_t)lfsr >> 24) < (uint32_t)(4 + color / 4)) dust = 16000; }
                else {   /* the LFSR: 15 bits, its feedback from bit 1 (long) or bit 6 (short: 93 steps) */
                    int32_t fb = (lfsr ^ (lfsr >> (noise == 3 ? 6 : 1))) & 1;
                    lfsr = ((lfsr >> 1) | (fb << 14)) & 0x7FFF;
                    if (!lfsr) lfsr = 1;
                }
            }
            nph = nx;
            nz = noise == 4 ? dust : noise == 1 ? (int32_t)((uint32_t)lfsr >> 17) - 16384 : (lfsr & 1) ? 14000 : -14000;
            if (noise == 4) dust -= dust >> 6;
            s += mulq15(nz, nlvl);
        }
        if (bits < 16) s = (int32_t)((uint32_t)s & ~((1u << (16 - bits)) - 1u));   /* fewer bits (the low ones cleared: as >> then <<, no shift of a negative) */
        if (hold) { if (hc-- <= 0) { hc = hold; hv = s; } s = hv; }   /* a lower sample rate */
        lp += mulq15(s - lp, tone);                                 /* TONE */
        s = mulq15(lp, lev);
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i);
    }
    v->ph[0] = ph;
    v->ph[1] = nph;
    v->s[0] = lfsr;
    v->s[1] = lp;
    v->s[2] = hc;
    v->s[3] = hv;
    v->s[5] = frame;
    v->s[6] = fcount;
    v->s[7] = dust;
}

static const engine_t ENG_KLOFI = {   /* (the host takes its parameters from Kreisel's table, src/engines_data.c) */
    .name = K_ENGINE_NAME_KLOF,
    .page_title = {"PULSE", "NOISE"},
    .note_on = klofi_note_on,
    .render = klofi_render,
    .amp = klofi_amp,
    .knob = {P_E1, P_E2, P_ATK, P_REL},
};
