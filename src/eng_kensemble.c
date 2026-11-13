/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/*
 * ENSEMBLE: a divide-down string machine, not a circuit-level emulation.
 * Four band-limited saw registers (bass -8ve, cello 8', viola 8' slightly detuned,
 * violin +8ve) and an optional pulse/brass registration feed a single tone filter
 * and a three-tap modulated short-delay ensemble. The chorus belongs to the TRACK,
 * after summing its voices: polyphony therefore needs only one 512-sample delay
 * per part, not one per voice. No allocations or floating point.
 *
 * SOUND: BASS CELLO VIOLA VIOLIN BRASS TONE ENS RATE DEPTH DTN LEVEL
 * EDIT:  HPF DRV NOIS ENV VEL
 * ENV is the host's ENV 1 -> FLT link; it changes the brightness of the violin
 * registration. LFO -> SHP modulates the brass blend.
 */
#define KE_LEN 512u
#define KE_MASK (KE_LEN - 1u)
typedef struct {
    int16_t delay[KE_LEN];
    uint32_t slow, fast;
    uint16_t wr;
    int32_t lp, hp;
    int32_t lpcoef, hpcoef, ens, depth;   /* the last block's settings: a tail after a switch away runs on them */
    uint32_t si, fi;
    uint8_t ready, drain;                 /* drain: blocks of tail left after the track left ENSEMBLE (kensemble_drain) */
} ke_chorus_t;
static ke_chorus_t ke_ch[NPART];

static void kensemble_note_on(track_t *t, voice_t *v)
{
    (void)t;
    /* Give the four registers independent initial phases while keeping all
       phases on a retrigger (like the existing PHASE/TRIO engines). */
    if (!v->s[2]) v->s[2] = (int32_t)(0x51F15EEDu ^ v->age);
    if (!v->ph[0] && !v->ph[1] && !v->ph[2] && !v->s[0]) {
        v->ph[0] = v->age * 0x9E3779B9u;
        v->ph[1] = v->age * 0x7F4A7C15u;
        v->ph[2] = v->age * 0x6C8E9CF5u;
        v->s[0] = (int32_t)(v->age * 0xB5297A4Du);
    }
}
static void kensemble_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int32_t bass = clamp(p[P_E0], 0, 127) * 64;
    const int32_t cello = clamp(p[P_E1], 0, 127) * 64;
    const int32_t viola = clamp(p[P_E2], 0, 127) * 64;
    const int32_t violin = clamp(p[P_E3], 0, 127) * 64;
    const int32_t brass = clamp(p[P_E4] + ((m->shape - (64 << 8)) >> 9), 0, 127) * 64;
    const int32_t treble = clamp(32767 + (m->cutoff >> 1), 0, 49152);
    const int32_t level = clamp(p[P_E10], 0, 127) * 258;
    const int32_t drv = clamp(p[P_E12], 0, 127), nois = clamp(p[P_E13], 0, 127) * 45;
    const int32_t vel = clamp(p[P_E15], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vel) * 127 + vel * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    const uint32_t inc0 = m->inc >> 1, inc1 = m->inc, inc2 = m->inc << 1;
    uint32_t incv = cents_inc(clamp(m->pitch16, 0, 2047), clamp(p[P_E9], 0, 50), 0);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], phv = (uint32_t)v->s[0];
    int32_t nst = v->s[2];
    uint32_t i;
    if (m->fine) incv += (uint32_t)((int32_t)(incv >> 12) * m->fine);
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        if (bass) s += mulq15(osc_saw(ph0, inc0), bass);
        if (cello) s += mulq15(osc_saw(ph1, inc1), cello);
        if (viola) s += mulq15(osc_saw(phv, incv), viola);
        if (violin) s += k_mulsh(mulq15(osc_saw(ph2, inc2), violin), treble, 15);
        if (brass) s += mulq15(osc_pulse(phv, incv, 0x80000000u), brass);
        if (nois) s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nois);
        ph0 += inc0; ph1 += inc1; ph2 += inc2; phv += incv;
        if (drv) s = softclip(((s >> 2) * ((32768 + drv * 320) >> 2)) >> 11);
        s = mulq15(s, level);
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i);
    }
    v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
    v->s[0] = (int32_t)phv;
    v->s[2] = nst;
}

/* Fractional, interpolated reading of the 512-sample circular delay.
 * The three taps have different base delays so their LFOs never coincide. */
static inline int32_t ke_tap(const ke_chorus_t *c, uint32_t w, uint32_t dq8)
{
    uint32_t d = dq8 >> 8, f = dq8 & 255u;
    int32_t a = c->delay[(w - d) & KE_MASK], b = c->delay[(w - d - 1u) & KE_MASK];
    return a + (int32_t)(((b - a) * (int32_t)f) >> 8);
}

/* Called once after all the engine's voices have been mixed (not per voice).
 * Tone and HPF are shared, as on a paraphonic string machine; the chorus
 * continues to run after the last voice to finish its short delay tail. */
static void ke_run(ke_chorus_t *c, int32_t *out, uint32_t n)
{
    const int32_t lpcoef = c->lpcoef, hpcoef = c->hpcoef, ens = c->ens, depth = c->depth;
    const uint32_t si = c->si, fi = c->fi;
    uint32_t p0, p1, p2, d0[3], d1[3], i;
    p0 = c->slow; p1 = c->fast; p2 = p1 + 0x55555555u;
    d0[0] = (uint32_t)(206 * 256 + ((sine_i(p0) * depth * 256) >> 15));
    d0[1] = (uint32_t)(276 * 256 + ((sine_i(p1) * depth * 256) >> 15));
    d0[2] = (uint32_t)(348 * 256 + ((sine_i(p2) * depth * 256) >> 15));
    d1[0] = (uint32_t)(206 * 256 + ((sine_i(p0 + si) * depth * 256) >> 15));
    d1[1] = (uint32_t)(276 * 256 + ((sine_i(p1 + fi) * depth * 256) >> 15));
    d1[2] = (uint32_t)(348 * 256 + ((sine_i(p2 + fi) * depth * 256) >> 15));
    for (i = 0; i < n; i++) {
        uint32_t w = c->wr;
        int32_t x = softclip(out[i]), y, wet = 0;
        c->lp += ((x - c->lp) * lpcoef) >> 15;
        c->hp += ((c->lp - c->hp) * hpcoef) >> 15;
        y = clamp(c->lp - c->hp, -32767, 32767);
        c->delay[w] = (int16_t)y;
        c->wr = (uint16_t)((w + 1u) & KE_MASK);
        if (ens) {
            uint32_t k;
            for (k = 0; k < 3; k++) {
                /* The host always renders CTL=32 samples: a shift, not a per-sample divide. */
                int32_t dq = (int32_t)d0[k] + (((int32_t)d1[k] - (int32_t)d0[k]) * (int32_t)(i + 1u) >> 5);
                wet += ke_tap(c, w, (uint32_t)dq);
            }
            wet = (wet * 10923) >> 15;                          /* / 3 */
            y = (y * (127 - ens) + wet * ens) * 258 >> 15;      /* / 127 (258 / 32768: within 0.1 %) */
        }
        out[i] = y;
    }
    c->slow += si; c->fast += fi;
}
static void kensemble_post(track_t *t, uint32_t ti, int32_t *out, uint32_t n)
{
    ke_chorus_t *c = &ke_ch[ti];
    const int16_t *p = t->p;
    const int32_t tone = clamp(p[P_E5], 0, 127), hpf = clamp(p[P_E11], 0, 127);
    const uint32_t rate = (uint32_t)clamp(p[P_E7], 0, 127);
    if (!c->ready) {
        memset(c, 0, sizeof *c);  /* no stale taps on a return to this engine */
        c->ready = 1; c->slow = 0x20000000u; c->fast = 0x80000000u;
    }
    c->lpcoef = 512 + 2 * tone * tone; c->hpcoef = 12 + 18 * hpf;
    c->ens = clamp(p[P_E6], 0, 127); c->depth = clamp(p[P_E8], 0, 127) * 70 / 127;
    c->si = (uint32_t)(((uint64_t)((35u + rate) * n) << 32) / (100u * FS));
    c->fi = (uint32_t)(((uint64_t)((400u + 3u * rate) * n) << 32) / (100u * FS));
    c->drain = 16;   /* (512 samples: the longest tap) */
    ke_run(c, out, n);
}
/* the track left ENSEMBLE: its chorus and filters ring out on silence for 512 samples, fading, added to the new engine's sound
 * (they used to stop at once: a click) */
static void kensemble_drain(uint32_t ti, int32_t *out, uint32_t n)
{
    ke_chorus_t *c = &ke_ch[ti];
    int32_t tail[CTL];
    uint32_t i;
    if (!c->ready) return;
    if (!c->drain || n > CTL) { c->ready = 0; return; }
    for (i = 0; i < n; i++) tail[i] = 0;
    ke_run(c, tail, n);
    for (i = 0; i < n; i++) {
        const int32_t g = (int32_t)(c->drain * 32u - i) * 64;   /* 32768 .. 0 over the 16 blocks */
        out[i] += k_mulsh((tail[i] * (g > 32767 ? 32767 : g)) >> 15, 30000, 12);   /* (ENSEMBLE's loudness gain: kreisel_audio.c) */
    }
    c->drain--;
}
static const engine_t ENG_KENSEMBLE = {
    .name = K_ENGINE_NAME_KENS,
    .page_title = {"STRINGS", "ENSEMBLE"},
    .note_on = kensemble_note_on,
    .render = kensemble_render,
    .knob = {P_E3, P_E5, P_E6, P_E8},
    .keep = 0x01,
};
