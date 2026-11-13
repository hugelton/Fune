/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * From Pendel (the author's own), relicensed MPL-2.0 for Kreisel, 2026-11-07. */
/* More effects for the rack (pd_effects.h). Q15, no floating point, no 64-bit multiplies; every product is kept
 * below 2^31 by clamping what goes into it (the notes give the bounds). */
#include "pd_effects.h"
#include "pd_osc.h"

#define LIM 65535                       /* what a stage takes in: +-2.0 */

static inline int32_t lim(int32_t x) { return pd_clamp(x, -LIM, LIM); }

#define soft pd_soft

/* A tone tilt, a value centred: at the middle nothing (the sound bit for bit), below it a one-pole low-pass closing to ~430 Hz,
 * above it the highs over ~860 Hz raised up to 2x. In and out +-65535 (the products stay below 2^31). */
typedef struct {
    int32_t c, amt, lp;         /* the low-pass's coefficient (Q15), the highs' boost (Q14), its state */
    uint8_t mode;               /* 0 flat, 1 darker, 2 brighter */
} tilt_t;

static void tilt_set(tilt_t *t, int32_t v)
{
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    t->mode = (uint8_t)(v == PD_PARAM_MID ? 0 : v < PD_PARAM_MID ? 1 : 2);
    t->c = t->mode == 1 ? 2000 + ((v * 30767) >> 14) : 4000;   /* 2000 .. 32765 */
    t->amt = t->mode == 2 ? (v - PD_PARAM_MID) * 2 : 0;          /* .. 32766 */
}

static inline int32_t tilt(tilt_t *t, int32_t x)
{
    int32_t lp = t->lp;
    if (!t->mode)
        return x;
    x = lim(x);
    lp += (((x - lp) >> 1) * t->c) >> 14;                    /* |x - lp| / 2 < 2^16, c < 2^15 */
    t->lp = lp;
    return t->mode == 1 ? lp : lim(x + ((((x - lp) >> 1) * t->amt) >> 14));
}

static uint32_t rng_next(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* a delay line of int16, size a power of two; read d (Q8 samples) behind the write index w, interpolated */
static inline int32_t tap(const int16_t *b, uint32_t mask, uint32_t w, uint32_t d_q8)
{
    uint32_t r = (w << 8) - d_q8, i = (r >> 8) & mask;
    int32_t a = b[i], c = b[(i + 1u) & mask];
    return a + (((c - a) * (int32_t)(r & 255u)) >> 8);
}

/* a rate parameter as a phase increment a sample (pd_effects.h) */
static uint32_t rate_inc(int32_t v) { return pd_lut32(PD_LFO_INC, pd_clamp(v, 0, PD_PARAM_MAX)); }

/* ----------------------------------------------------------------- fuzz --- */

static const char *const FUZZ_NAMES[PD_FUZZ_PARAMS] = { "Fuzz", "Bias", "Tone" };

typedef struct {
    int32_t gain, xmax;         /* Q8: 4x .. 1024x; the input that still fits gain * x in 32 bits */
    int32_t bias, neg;          /* Q15 */
    int32_t x1, y1;             /* the DC blocker */
    tilt_t tone;
} fuzz_t;

static void fuzz_set(void *st, unsigned id, int32_t v)
{
    fuzz_t *f = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    if (id == PD_FUZZ_FUZZ) {                               /* 4x * 2^(8 v): +12 .. +60 dB */
        uint32_t e = (uint32_t)v * 8u, oct = e / 32767u, fr = e % 32767u * 2u, base = (4u << 8) << oct;
        f->gain = (int32_t)(base + (((base >> 8) * fr) >> 8));   /* 2^frac ~ 1 + frac (fr Q16) */
        f->xmax = 0x7FFFFFFF / f->gain;
    } else if (id == PD_FUZZ_BIAS) {
        f->bias = (v * 19661) >> 15;                        /* 0 .. 0.6 */
        f->neg = 32767 - ((f->bias * 26214) >> 15);         /* the negative limit: 1 .. 0.52 */
    } else if (id == PD_FUZZ_TONE) {
        tilt_set(&f->tone, v);
    }
}

static void fuzz_reset(void *st) { ((fuzz_t *)st)->x1 = ((fuzz_t *)st)->y1 = ((fuzz_t *)st)->tone.lp = 0; }

static void fuzz_init(void *st, void *mem, uint32_t sr)
{
    (void)mem;
    (void)sr;
    fuzz_set(st, PD_FUZZ_FUZZ, 16000);
    fuzz_set(st, PD_FUZZ_BIAS, 6000);
    fuzz_set(st, PD_FUZZ_TONE, PD_PARAM_MID);
}

static void fuzz_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    fuzz_t *f = st;
    int32_t x1 = f->x1, y1 = f->y1, g = f->gain, b = f->bias, neg = -f->neg, xm = f->xmax;
    for (unsigned i = 0; i < frames; i++) {
        int32_t y = pd_clamp(((pd_clamp(in[i], -xm, xm) * g) >> 8) + b, neg, 32767);
        int32_t o = y - x1 + ((y1 * 32735) >> 15);          /* DC blocker, 0.999 */
        /* Q15 feedback can settle at a small nonzero integer indefinitely:
         * flush an inaudible residual after the input and DC history vanish. */
        if (!y && !x1 && o >= -128 && o <= 128) {
            /* Truncation can stall the feedback around a few dozen LSBs.
             * Remove one LSB of low-level residue per sample, without a divider. */
            if (o > 0) --o;
            else if (o < 0) ++o;
        }
        x1 = y;
        y1 = lim(o);
        out[i] = tilt(&f->tone, (y1 * 22938) >> 15);        /* 0.7 */
    }
    f->x1 = x1;
    f->y1 = y1;
}

const PendelDSP PD_FX_FUZZ = {
    "Fuzz", sizeof(fuzz_t), NULL, PD_FUZZ_PARAMS, FUZZ_NAMES,
    fuzz_init, fuzz_process, fuzz_reset, fuzz_set,
};

/* ------------------------------------------------------------- auto-wah --- */

static const char *const WAH_NAMES[PD_WAH_PARAMS] = { "Sensitivity", "Resonance", "LFO" };

typedef struct {
    int32_t sens, damp;         /* Q15 */
    int32_t env, low, band;
    uint32_t ph, inc;           /* the LFO (MODE on): SENS its rate */
    uint8_t lfo;
} wah_t;

static void wah_set(void *st, unsigned id, int32_t v)
{
    wah_t *w = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    if (id == PD_WAH_SENS) {
        w->sens = v;
        w->inc = rate_inc(v);
    } else if (id == PD_WAH_MODE)
        w->lfo = (uint8_t)pd_on(v);
    else if (id == PD_WAH_RESO)
        w->damp = 32767 - ((v * 29491) >> 15);              /* damping 1.0 .. 0.1 */
}

static void wah_reset(void *st)
{
    wah_t *w = st;
    w->env = w->low = w->band = 0;
}

static void wah_init(void *st, void *mem, uint32_t sr)
{
    (void)mem;
    (void)sr;
    wah_set(st, PD_WAH_SENS, 20000);
    wah_set(st, PD_WAH_RESO, 20000);
}

static void wah_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    wah_t *w = st;
    int32_t env = w->env, low = w->low, band = w->band, sens = w->sens, q = w->damp;
    uint32_t ph = w->ph;
    for (unsigned i = 0; i < frames; i++) {
        int32_t x = lim(in[i]), a = pd_clamp(x < 0 ? -x : x, 0, 32767);
        env += a > env ? (a - env) >> 7 : (a - env) >> 12;  /* ~3 ms up, ~90 ms down */
        /* 2 sin(pi fc / fs) ~ 2 pi fc / fs, Q15: 200 Hz + env * sens * 12 kHz, held at 3.5 kHz; the LFO: 200 Hz .. 3.5 kHz */
        int32_t f = w->lfo ? 933 + ((((pd_sine(ph) + 32768) >> 1) * 482) >> 10) : 933 + ((((env * sens) >> 15) * 1750) >> 10);
        f = f > 16334 ? 16334 : f;
        ph += w->inc;
        low = lim(low + ((f * band) >> 15));
        int32_t high = lim(x - low - ((q * band) >> 15));
        band = lim(band + ((f * high) >> 15));
        /* Integer state-variable filters otherwise retain low-level limit cycles
         * on silence. Leak one LSB when the envelope has completely settled. */
        if (!x && !env) {
            if (low > 0) --low;
            else if (low < 0) ++low;
            if (band > 0) --band;
            else if (band < 0) ++band;
        }
        out[i] = ((band * 26214) >> 14) + (x >> 2);         /* 1.6 x the band, a quarter of the dry */
    }
    w->env = env;
    w->low = low;
    w->band = band;
    if (w->lfo)
        w->ph = ph;
}

const PendelDSP PD_FX_WAH = {
    "Auto Wah", sizeof(wah_t), NULL, PD_WAH_PARAMS, WAH_NAMES,
    wah_init, wah_process, wah_reset, wah_set,
};

/* ---------------------------------------------------------------- crush --- */

static const char *const CRUSH_NAMES[PD_CRUSH_PARAMS] = { "Bits", "Downsample", "Tone" };

typedef struct {
    uint32_t shift;             /* bits taken off a 16-bit sample: 0 .. 14 */
    uint32_t inc, ph;           /* the hold's clock, Q16 */
    int32_t hold;
    tilt_t tone;
} crush_t;

static void crush_set(void *st, unsigned id, int32_t v)
{
    crush_t *c = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    if (id == PD_CRUSH_BITS)
        c->shift = (uint32_t)(v * 14 + 16383) >> 15;
    else if (id == PD_CRUSH_DOWN)                           /* one sample in 1 .. 50 */
        c->inc = 65536u * 32767u / (32767u + (uint32_t)v * 49u);
    else if (id == PD_CRUSH_TONE)
        tilt_set(&c->tone, v);
}

static void crush_reset(void *st) { ((crush_t *)st)->ph = 65536u; ((crush_t *)st)->tone.lp = 0; }

static void crush_init(void *st, void *mem, uint32_t sr)
{
    (void)mem;
    (void)sr;
    crush_set(st, PD_CRUSH_BITS, 16384);
    crush_set(st, PD_CRUSH_DOWN, 6000);
    crush_set(st, PD_CRUSH_TONE, PD_PARAM_MID);
    crush_reset(st);
}

static void crush_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    crush_t *c = st;
    uint32_t ph = c->ph, inc = c->inc, s = c->shift;
    int32_t h = c->hold, half = s ? 1 << (s - 1u) : 0;
    for (unsigned i = 0; i < frames; i++) {
        ph += inc;
        if (ph >= 65536u) {
            ph -= 65536u;
            h = pd_sat16(in[i]);
            h = (int32_t)((uint32_t)(h + half + 32768) >> s << s) - 32768;   /* rounded to the step */
        }
        out[i] = tilt(&c->tone, h);
    }
    c->ph = ph;
    c->hold = h;
}

const PendelDSP PD_FX_CRUSH = {
    "Crush", sizeof(crush_t), NULL, PD_CRUSH_PARAMS, CRUSH_NAMES,
    crush_init, crush_process, crush_reset, crush_set,
};

/* -------------------------------------------------------------- flanger --- */

static const char *const FLG_NAMES[PD_FLG_PARAMS] = { "Rate", "Feedback", "Depth" };

#define FLG_LEN 512u            /* 11.6 ms: the sweep reaches 6.6 */

typedef struct {
    int16_t *buf;
    uint32_t w, ph, inc;
    int32_t fb;                 /* Q15, at most 0.85 */
    uint32_t depth;             /* Q14: the sweep 0 .. 1.5x (1x at the middle: as before DEPTH) */
} flanger_t;

static size_t flg_mem(uint32_t sr) { (void)sr; return FLG_LEN * sizeof(int16_t); }

static void flg_set(void *st, unsigned id, int32_t v)
{
    flanger_t *f = st;
    if (id == PD_FLG_RATE)
        f->inc = rate_inc(v);
    else if (id == PD_FLG_FEEDBACK)
        f->fb = (pd_clamp(v, 0, PD_PARAM_MAX) * 27853) >> 15;
    else if (id == PD_FLG_DEPTH) {
        v = pd_clamp(v, 0, PD_PARAM_MAX);
        f->depth = (uint32_t)(v <= PD_PARAM_MID ? v : PD_PARAM_MID + ((v - PD_PARAM_MID) >> 1));
    }
}

static void flg_reset(void *st)
{
    flanger_t *f = st;
    for (uint32_t i = 0; i < FLG_LEN; i++)
        f->buf[i] = 0;
}

static void flg_init(void *st, void *mem, uint32_t sr)
{
    flanger_t *f = st;
    (void)sr;
    f->buf = mem;
    flg_set(f, PD_FLG_RATE, 9000);
    flg_set(f, PD_FLG_FEEDBACK, 16000);
    flg_set(f, PD_FLG_DEPTH, PD_PARAM_MID);
}

static void flg_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    flanger_t *f = st;
    uint32_t w = f->w, ph = f->ph, inc = f->inc, dp = f->depth;
    int32_t fb = f->fb;
    for (unsigned i = 0; i < frames; i++) {
        /* 0.6 ms + 6 ms * (0.5 + 0.5 sin) * depth, in Q8 samples: 26.5 .. 291 (1.5x: 424) */
        uint32_t d = 6777u + (((uint32_t)((pd_sine(ph) + 32768) * 1059 >> 10) * dp) >> 14);   /* < 67776 * 24576 */
        int32_t x = lim(in[i]), wet = tap(f->buf, FLG_LEN - 1u, w, d);
        f->buf[w] = (int16_t)pd_sat16(x + ((wet * fb) >> 15));
        out[i] = ((x + wet) * 21299) >> 15;                 /* 0.65 */
        w = (w + 1u) & (FLG_LEN - 1u);
        ph += inc;
    }
    f->w = w;
    f->ph = ph;
}

const PendelDSP PD_FX_FLANGER = {
    "Flanger", sizeof(flanger_t), flg_mem, PD_FLG_PARAMS, FLG_NAMES,
    flg_init, flg_process, flg_reset, flg_set,
};

/* --------------------------------------------------------------- phaser --- */

static const char *const PHS_NAMES[PD_PHS_PARAMS] = { "Rate", "Depth", "Feedback" };

/* the stages' centres on the cutoff scale (20 Hz * 2^(10 v / 32767)): 260, 480, 880, 1600 Hz */
static const int32_t PHS_CENTRE[4] = { 12126, 15025, 17890, 20717 };

typedef struct {
    uint32_t ph, inc;
    int32_t span;               /* the sweep, +- octaves on the cutoff scale (3277 an octave): 0.2 .. 1.55 */
    int32_t z[4], fb;
    int32_t fbk;                /* Q15: 0 .. 0.8, 0.34 at the middle (as before FDBK) */
} phaser_t;

static void phs_set(void *st, unsigned id, int32_t v)
{
    phaser_t *p = st;
    if (id == PD_PHS_RATE)
        p->inc = rate_inc(v);
    else if (id == PD_PHS_DEPTH)
        p->span = 655 + ((pd_clamp(v, 0, PD_PARAM_MAX) * 4424) >> 15);
    else if (id == PD_PHS_FEEDBACK) {
        v = pd_clamp(v, 0, PD_PARAM_MAX);
        p->fbk = v < PD_PARAM_MID ? (v * 11141) >> 14 : 11141 + (((v - PD_PARAM_MID) * 15073) >> 14);
    }
}

static void phs_reset(void *st)
{
    phaser_t *p = st;
    p->z[0] = p->z[1] = p->z[2] = p->z[3] = p->fb = 0;
}

static void phs_init(void *st, void *mem, uint32_t sr)
{
    (void)mem;
    (void)sr;
    phs_set(st, PD_PHS_RATE, 9000);
    phs_set(st, PD_PHS_DEPTH, 20000);
    phs_set(st, PD_PHS_FEEDBACK, PD_PARAM_MID);
}

static void phs_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    phaser_t *p = st;
    int32_t a[4], z0 = p->z[0], z1 = p->z[1], z2 = p->z[2], z3 = p->z[3], fb = p->fb, fbk = p->fbk;
    int32_t off = (pd_sine(p->ph) * p->span) >> 15;         /* the coefficients once a block (<= 0.7 ms) */
    for (unsigned s = 0; s < 4u; s++)
        a[s] = pd_lut16(PD_AP_A, pd_clamp(PHS_CENTRE[s] + off, 0, PD_PARAM_MAX));
    for (unsigned i = 0; i < frames; i++) {
        int32_t x = lim(in[i]), y;
        /* first-order all-pass: y = -a x + z, z = x + a y; |a| < 1, |x| <= 2 */
        y = lim(x + ((fb * fbk) >> 15));                    /* some of the last output back in */
#define AP(z, k) { int32_t o = lim(z - ((a[k] * y) >> 15)); z = lim(y + ((a[k] * o) >> 15)); y = o; }
        AP(z0, 0) AP(z1, 1) AP(z2, 2) AP(z3, 3)
#undef AP
        fb = y;
        out[i] = (((x + y) >> 1) * 19005) >> 14;            /* 0.58 */
    }
    p->z[0] = z0;
    p->z[1] = z1;
    p->z[2] = z2;
    p->z[3] = z3;
    p->fb = fb;
    p->ph += p->inc * frames;
}

const PendelDSP PD_FX_PHASER = {
    "Phaser", sizeof(phaser_t), NULL, PD_PHS_PARAMS, PHS_NAMES,
    phs_init, phs_process, phs_reset, phs_set,
};

/* -------------------------------------------------------------- tremolo --- */

static const char *const TRM_NAMES[PD_TRM_PARAMS] = { "Rate", "Depth", "Square" };

typedef struct {
    uint32_t ph, inc;
    int32_t depth;              /* Q15 */
    uint8_t square;
} tremolo_t;

static void trm_set(void *st, unsigned id, int32_t v)
{
    tremolo_t *t = st;
    if (id == PD_TRM_RATE)
        t->inc = rate_inc(v);
    else if (id == PD_TRM_DEPTH)
        t->depth = pd_clamp(v, 0, PD_PARAM_MAX);
    else if (id == PD_TRM_WAVE)
        t->square = (uint8_t)pd_on(v);
}

static void trm_reset(void *st) { (void)st; }

static void trm_init(void *st, void *mem, uint32_t sr)
{
    (void)mem;
    (void)sr;
    trm_set(st, PD_TRM_RATE, 19000);
    trm_set(st, PD_TRM_DEPTH, 20000);
}

static void trm_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    tremolo_t *t = st;
    uint32_t ph = t->ph, inc = t->inc;
    for (unsigned i = 0; i < frames; i++) {
        int32_t s = pd_sine(ph), g;
        if (t->square)                                      /* the sine x 4, clipped: a square, its edges ~8 % of the cycle */
            s = pd_clamp(s * 4, -32768, 32767);
        g = 32767 - ((t->depth * ((s + 32768) >> 1)) >> 15);   /* 1 - depth (0.5 + 0.5 sin) */
        out[i] = (lim(in[i]) * g) >> 15;
        ph += inc;
    }
    t->ph = ph;
}

const PendelDSP PD_FX_TREMOLO = {
    "Tremolo", sizeof(tremolo_t), NULL, PD_TRM_PARAMS, TRM_NAMES,
    trm_init, trm_process, trm_reset, trm_set,
};

/* --------------------------------------------------------------- wobble --- */

static const char *const WOB_NAMES[PD_WOB_PARAMS] = { "Rate", "Depth", "Sync" };

#define WOB_LEN 1024u           /* 23 ms: 12 ms + 8 ms * 1.33 at most */

typedef struct {
    int16_t *buf;
    uint32_t w, ph, ph2, inc;
    int32_t depth;              /* Q8 samples: 0 .. 8 ms */
    int32_t rate;               /* RATE as set */
    uint32_t fs;
    uint8_t sync, bpm;          /* SYNC on: a wow a division of the tempo (PD_HOST_TEMPO) */
} wobble_t;

static size_t wob_mem(uint32_t sr) { (void)sr; return WOB_LEN * sizeof(int16_t); }

static void wob_set(void *st, unsigned id, int32_t v)
{
    wobble_t *o = st;
    if (id == PD_WOB_RATE)
        o->rate = pd_clamp(v, 0, PD_PARAM_MAX);
    else if (id == PD_WOB_DEPTH)
        o->depth = (pd_clamp(v, 0, PD_PARAM_MAX) * 22) >> 3;   /* 8 ms = 353 samples = 90368 Q8 */
    else if (id == PD_WOB_SYNC)
        o->sync = (uint8_t)pd_on(v);
    else if (id == PD_HOST_TEMPO)
        o->bpm = (uint8_t)pd_clamp(v, 20, 255);
    else
        return;
    if (id != PD_WOB_DEPTH)
        o->inc = o->sync ? 0xFFFFFFFFu / pd_div_samples(pd_div_index(o->rate), o->bpm, o->fs) : rate_inc(o->rate);
}

static void wob_reset(void *st)
{
    wobble_t *o = st;
    for (uint32_t i = 0; i < WOB_LEN; i++)
        o->buf[i] = 0;
}

static void wob_init(void *st, void *mem, uint32_t sr)
{
    wobble_t *o = st;
    o->buf = mem;
    o->fs = sr;
    o->bpm = 120;
    wob_set(o, PD_WOB_RATE, 12000);
    wob_set(o, PD_WOB_DEPTH, 8000);
}

static void wob_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    wobble_t *o = st;
    uint32_t w = o->w, ph = o->ph, ph2 = o->ph2, inc = o->inc, inc2 = (inc >> 3) * 58u;   /* flutter: 7.25x */
    int32_t depth = o->depth;
    for (unsigned i = 0; i < frames; i++) {
        int32_t m = pd_sine(ph) + ((pd_sine(ph2) * 10813) >> 15);   /* wow + 0.33 flutter: +-1.33 */
        o->buf[w] = (int16_t)pd_sat16(in[i]);
        out[i] = tap(o->buf, WOB_LEN - 1u, w, (uint32_t)(135518 + ((m * (depth >> 2)) >> 13)));   /* 12 ms base */
        w = (w + 1u) & (WOB_LEN - 1u);
        ph += inc;
        ph2 += inc2;
    }
    o->w = w;
    o->ph = ph;
    o->ph2 = ph2;
}

const PendelDSP PD_FX_WOBBLE = {
    "Wobble", sizeof(wobble_t), wob_mem, PD_WOB_PARAMS, WOB_NAMES,
    wob_init, wob_process, wob_reset, wob_set,
};

/* ---------------------------------------------------------------- noise --- */

static const char *const NOISE_NAMES[PD_NOISE_PARAMS] = { "Hiss", "Crackle", "Tone" };

typedef struct {
    uint32_t rng, thr;          /* a click's chance a sample, of 2^32 */
    int32_t hiss, amp, click;   /* Q15 */
    tilt_t tone;                /* the noise's, not the sound's */
} noise_t;

static void noise_set(void *st, unsigned id, int32_t v)
{
    noise_t *n = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    if (id == PD_NOISE_HISS) {
        n->hiss = (((v * v) >> 15) * 1638) >> 15;           /* v^2 * 0.05: -26 dBFS at the top */
    } else if (id == PD_NOISE_CRACKLE) {
        uint32_t d2 = (uint32_t)((v * v) >> 15);
        n->thr = d2 * 524u;                                 /* d^2 * 0.004 */
        n->amp = 8192 + ((v * 14746) >> 15);                /* 0.25 .. 0.7 */
    } else if (id == PD_NOISE_TONE) {
        tilt_set(&n->tone, v);
    }
}

static void noise_reset(void *st) { ((noise_t *)st)->click = ((noise_t *)st)->tone.lp = 0; }

static void noise_init(void *st, void *mem, uint32_t sr)
{
    noise_t *n = st;
    (void)mem;
    (void)sr;
    n->rng = 0x1234567u;
    noise_set(n, PD_NOISE_HISS, 12000);
    noise_set(n, PD_NOISE_CRACKLE, 12000);
    noise_set(n, PD_NOISE_TONE, PD_PARAM_MID);
}

static void noise_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    noise_t *n = st;
    uint32_t rng = n->rng, thr = n->thr;
    int32_t c = n->click, hiss = n->hiss, amp = n->amp;
    for (unsigned i = 0; i < frames; i++) {
        if (rng_next(&rng) < thr)
            c = ((int32_t)(rng_next(&rng) >> 16) - 32768) * amp >> 15;
        c = (c * 23593) >> 15;                              /* 0.72 a sample: a tick, not one sample */
        out[i] = in[i] + tilt(&n->tone, ((((int32_t)(rng_next(&rng) >> 16) - 32768) * hiss) >> 15) + c);
    }
    n->rng = rng;
    n->click = c;
}

const PendelDSP PD_FX_NOISE = {
    "Noise", sizeof(noise_t), NULL, PD_NOISE_PARAMS, NOISE_NAMES,
    noise_init, noise_process, noise_reset, noise_set,
};

/* ---------------------------------------------------------------- pitch --- */

static const char *const PIT_NAMES[PD_PIT_PARAMS] = { "Semitones", "Feedback", "Fine" };

#define PIT_LEN 4096u           /* the window (2048) and the base (128) behind the write index */

/* 2^(s / 12) for s = -12 .. 12, Q16 */
static const uint32_t PIT_RATIO[25] = {
    32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858, 65536,
    69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};

typedef struct {
    int16_t *buf;
    uint32_t w, ph;             /* ph: the first tap's place in the window, 2^32 = 2048 samples */
    int32_t step;               /* (1 - ratio) a sample, in that scale */
    int32_t fb;                 /* Q15, at most 0.82 */
    int8_t semi, cents;
} pitch_t;

static size_t pit_mem(uint32_t sr) { (void)sr; return PIT_LEN * sizeof(int16_t); }

static void pit_set(void *st, unsigned id, int32_t v)
{
    pitch_t *p = st;
    uint32_t ratio;
    if (id == PD_PIT_SEMI)
        p->semi = (int8_t)pd_pitch_semis(v);
    else if (id == PD_PIT_FINE)
        p->cents = (int8_t)pd_pitch_cents(v);
    else if (id == PD_PIT_FEEDBACK)
        p->fb = (pd_clamp(v, 0, PD_PARAM_MAX) * 26870) >> 15;
    if (id != PD_PIT_SEMI && id != PD_PIT_FINE)
        return;
    ratio = PIT_RATIO[p->semi + 12];
    if (p->cents) {   /* x 2^(c / 1200) ~ 1 + y + y^2 / 2, y = c ln 2 / 1200 (Q16: 37.85 a cent); 32-bit, the ratio split */
        const int32_t y = (p->cents * 9691) >> 8;
        const uint32_t f = (uint32_t)(65536 + y + ((y * y) >> 17));
        ratio = (ratio >> 16) * f + ((((ratio & 0xFFFFu) >> 1) * f) >> 15);
    }
    p->step = (int32_t)(65536u - ratio) * 32;   /* 2^32 / 2048 = 2^21 = 2^16 * 32 */
}

static void pit_reset(void *st)
{
    pitch_t *p = st;
    for (uint32_t i = 0; i < PIT_LEN; i++)
        p->buf[i] = 0;
}

static void pit_init(void *st, void *mem, uint32_t sr)
{
    pitch_t *p = st;
    (void)sr;
    p->buf = mem;
    pit_set(p, PD_PIT_SEMI, 24576);                         /* +6 .. (the middle is 0: no shift) */
    pit_set(p, PD_PIT_FEEDBACK, 0);
}

static void pit_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    pitch_t *p = st;
    uint32_t w = p->w, ph = p->ph, step = (uint32_t)p->step;
    int32_t fb = p->fb;
    for (unsigned i = 0; i < frames; i++) {
        uint32_t pb = ph + 0x80000000u;
        int32_t wa = (32768 - pd_sine(ph + 0x40000000u)) >> 1;   /* 0.5 - 0.5 cos: the taps fade at the window's ends */
        int32_t a = tap(p->buf, PIT_LEN - 1u, w, (128u << 8) + (ph >> 13));
        int32_t b = tap(p->buf, PIT_LEN - 1u, w, (128u << 8) + (pb >> 13));
        int32_t sh = (a * wa + b * (32768 - wa)) >> 15;     /* |a| < 2^15 */
        int32_t x = lim(in[i]);
        p->buf[w] = (int16_t)pd_sat16((soft(x + ((sh * fb) >> 15)) * 39322) >> 15);   /* 1.2 */
        out[i] = ((x * 18022) >> 15) + ((sh * 22938) >> 15);   /* 0.55 dry, 0.7 shifted */
        w = (w + 1u) & (PIT_LEN - 1u);
        ph += step;
    }
    p->w = w;
    p->ph = ph;
}

const PendelDSP PD_FX_PITCH = {
    "Pitch", sizeof(pitch_t), pit_mem, PD_PIT_PARAMS, PIT_NAMES,
    pit_init, pit_process, pit_reset, pit_set,
};

/* ----------------------------------------------------------------- room --- */

static const char *const ROOM_NAMES[PD_ROOM_PARAMS] = { "Size", "Decay", "Tone" };

#define ROOM_LEN 4096u          /* 93 ms: the tail reads 60 at most */

typedef struct {
    int16_t *buf;
    uint32_t w, tail, early;    /* samples */
    int32_t fb, damp;           /* Q15 at most 0.72; the darkening low-pass */
    int32_t dk;                 /* its coefficient, Q15: 1500 .. 26000, 5898 (0.18) at the middle */
} room_t;

static size_t room_mem(uint32_t sr) { (void)sr; return ROOM_LEN * sizeof(int16_t); }

static void room_set(void *st, unsigned id, int32_t v)
{
    room_t *r = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    if (id == PD_ROOM_SIZE) {                               /* 15 .. 60 ms */
        r->tail = (15000u + (uint32_t)v * 45000u / 32767u) * PD_FS / 1000000u;
        r->early = r->tail / 2u + 173u;                     /* a short reflection, off the tail's harmonics */
    } else if (id == PD_ROOM_DECAY) {
        r->fb = (v * 23593) >> 15;
    } else if (id == PD_ROOM_TONE) {
        r->dk = v < PD_PARAM_MID ? 1500 + ((v * 4398) >> 14) : 5898 + (((v - PD_PARAM_MID) * 20102) >> 14);
    }
}

static void room_reset(void *st)
{
    room_t *r = st;
    for (uint32_t i = 0; i < ROOM_LEN; i++)
        r->buf[i] = 0;
    r->damp = 0;
}

static void room_init(void *st, void *mem, uint32_t sr)
{
    room_t *r = st;
    (void)sr;
    r->buf = mem;
    room_set(r, PD_ROOM_SIZE, 16384);
    room_set(r, PD_ROOM_DECAY, 18000);
    room_set(r, PD_ROOM_TONE, PD_PARAM_MID);
}

static void room_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    room_t *r = st;
    uint32_t w = r->w, m = ROOM_LEN - 1u, tl = r->tail, er = r->early;
    int32_t damp = r->damp, fb = r->fb, dk = r->dk;
    for (unsigned i = 0; i < frames; i++) {
        int32_t tail = r->buf[(w - tl) & m], early = r->buf[(w - er) & m], x = lim(in[i]);
        damp += ((tail - damp) * dk) >> 15;                /* (0.18 at the middle): the repeats recede instead of ringing */
        r->buf[w] = (int16_t)pd_sat16(soft(x + ((damp * fb) >> 15)));
        out[i] = ((x * 25559) >> 15) + ((early * 5243) >> 15) + ((damp * 17039) >> 15);   /* 0.78, 0.16, 0.52 */
        w = (w + 1u) & m;
    }
    r->w = w;
    r->damp = damp;
}

const PendelDSP PD_FX_ROOM = {
    "Room", sizeof(room_t), room_mem, PD_ROOM_PARAMS, ROOM_NAMES,
    room_init, room_process, room_reset, room_set,
};

/* ------------------------------------------------------------------ amp --- */

static const char *const AMP_NAMES[PD_AMP_PARAMS] = { "Model", "Gain", "Bass", "Mid", "Treble", "Level", "Cab" };

/* per model: the first stage's gain (Q8, low .. high), the second's (0 = none), the input's high-pass
 * (one-pole, 2 pi fc / fs Q15: tighter for more gain), the plate's low-pass (Miller, 1 - e^-w Q15), the tone stack's
 * centre (the cutoff scale), the power stage's drive (Q8) */
static const struct {
    int32_t g_lo, g_hi, g2, hp, miller, tone, power;
} AMP_MODEL[PD_AMP_MODELS] = {
    { 0, 0, 0, 0, 0, 0, 0 },
    { 256, 2560, 0, 187, 20680, 14162, 256 },             /* CLEAN: 1 .. 10x, one stage, 40 Hz, 7 kHz, 400 Hz */
    { 512, 10240, 1536, 327, 17790, 16454, 384 },         /* CRUNCH: 2 .. 40x then 6x, 70 Hz, 5.5 kHz, 650 Hz */
    { 1024, 20480, 3072, 560, 17790, 17134, 512 },        /* LEAD: 4 .. 80x then 12x, 120 Hz, 5.5 kHz, 750 Hz */
};

typedef struct {
    int32_t g, xm;              /* Q8; the input that keeps x * g in 32 bits */
    int32_t lp, dx, dy;         /* the plate's low-pass, the coupling capacitor (a DC blocker) */
} tube_stage;

typedef struct {
    uint8_t model, cab;
    int32_t gain_v;
    int32_t hp_c, hp_lp;        /* the input's high-pass */
    tube_stage st[2];
    int32_t tf, tlow, tband;    /* the tone stack: SVF coefficient, states */
    int32_t wb, wm, wt;         /* its bands' weights, Q15 */
    pd_svf cab_hp, cab_lp1, cab_lp2;
    int32_t level, cur;         /* Q16: 0 .. 0.5x (the power stage's full swing to -6 dBFS) */
} amp_t;

#define TONE_Q 39322            /* the stack's damping, 1.2 (Q15): broad bands */

/* lo .. hi along v^2: most of the knob in the low gains, as an audio taper */
static int32_t gain_q8(int32_t lo, int32_t hi, int32_t v) { return lo + (((hi - lo) * ((v * v) >> 15)) >> 15); }

static void amp_stage_gain(amp_t *a)
{
    const int32_t *m = &AMP_MODEL[a->model].g_lo;
    a->st[0].g = gain_q8(m[0], m[1], a->gain_v);
    a->st[1].g = AMP_MODEL[a->model].g2;
    for (unsigned i = 0; i < 2u; i++)
        a->st[i].xm = a->st[i].g ? 0x7FFFFFFF / a->st[i].g : 0;
}

static void amp_set(void *st, unsigned id, int32_t v)
{
    amp_t *a = st;
    v = pd_clamp(v, 0, PD_PARAM_MAX);
    switch (id) {
    case PD_AMP_MODEL:
        a->model = (uint8_t)((v * PD_AMP_MODELS) >> 15);
        a->hp_c = AMP_MODEL[a->model].hp;
        a->tf = pd_lut16(PD_SVF_F, AMP_MODEL[a->model].tone);
        amp_stage_gain(a);
        break;
    case PD_AMP_GAIN:
        a->gain_v = v;
        amp_stage_gain(a);
        break;
    case PD_AMP_BASS: a->wb = v; break;
    case PD_AMP_MID: a->wm = v; break;
    case PD_AMP_TREBLE: a->wt = v; break;
    case PD_AMP_LEVEL: a->level = v; break;
    case PD_AMP_CAB: a->cab = (uint8_t)(v >= 16384); break;
    default: break;
    }
}

static void amp_reset(void *st)
{
    amp_t *a = st;
    a->hp_lp = a->tlow = a->tband = 0;
    for (unsigned i = 0; i < 2u; i++)
        a->st[i].lp = a->st[i].dx = a->st[i].dy = 0;
    a->cab_hp.low = a->cab_hp.band = a->cab_lp1.low = a->cab_lp1.band = a->cab_lp2.low = a->cab_lp2.band = 0;
    a->cur = a->level;
}

static void amp_init(void *st, void *mem, uint32_t sr)
{
    amp_t *a = st;
    (void)mem;
    (void)sr;
    a->cab_hp.mode = PD_SVF_HP;
    a->cab_lp1.mode = a->cab_lp2.mode = PD_SVF_LP;
    pd_svf_set(&a->cab_hp, 6553, 22420);                   /* 80 Hz, damping 0.7: the cabinet's low bump */
    pd_svf_set(&a->cab_lp1, 25602, 10103);                 /* 4.5 kHz, damping 1.41, twice: the speaker's top */
    pd_svf_set(&a->cab_lp2, 25602, 10103);
    amp_set(a, PD_AMP_MODEL, 16384 + 4096);                 /* CRUNCH */
    amp_set(a, PD_AMP_GAIN, 16384);
    amp_set(a, PD_AMP_BASS, 16384);
    amp_set(a, PD_AMP_MID, 16384);
    amp_set(a, PD_AMP_TREBLE, 16384);
    amp_set(a, PD_AMP_LEVEL, 16384);
    amp_set(a, PD_AMP_CAB, 32767);
    amp_reset(a);
}

/* a 12AX7 stage: gain, the curve (PD_TUBE: -2 .. 2 in, halved), the plate's low-pass, the coupling capacitor */
static inline int32_t tube(tube_stage *t, int32_t x, int32_t miller)
{
    int32_t v = pd_clamp((pd_clamp(x, -t->xm, t->xm) * t->g) >> 8, -65535, 65535) + 65536;
    int32_t i = v >> 9, a = PD_TUBE[i], y = (a + (((PD_TUBE[i + 1] - a) * (v & 511)) >> 9)) * 2;
    int32_t o;
    t->lp += ((y - t->lp) * miller) >> 15;                  /* |y - lp| < 2^17, miller < 2^15 */
    o = t->lp - t->dx + ((t->dy * 32604) >> 15);           /* 0.995: ~35 Hz */
    t->dx = t->lp;
    t->dy = lim(o);
    return t->dy;
}

static void amp_process(void *st, const pd_sample *in, pd_sample *out, unsigned frames)
{
    amp_t *a = st;
    int32_t hp = a->hp_lp, hc = a->hp_c, mil = AMP_MODEL[a->model].miller, pw = AMP_MODEL[a->model].power;
    int32_t low = a->tlow, band = a->tband, f = a->tf, wb = a->wb, wm = a->wm, wt = a->wt;
    if (a->model == PD_AMP_OFF) {
        for (unsigned i = 0; i < frames; i++)
            out[i] = in[i];
        return;
    }
    for (unsigned i = 0; i < frames; i++) {
        int32_t x = lim(in[i]), y, high;
        hp += ((x - hp) * hc) >> 15;                        /* the input's coupling: lows out before the gain */
        y = tube(&a->st[0], x - hp, mil);
        if (a->st[1].g)
            y = tube(&a->st[1], y, mil);
        /* the tone stack: low, band and high of one SVF, weighted (all at full: the input back) */
        low = lim(low + ((f * band) >> 15));
        high = lim(y - low - ((TONE_Q * (band >> 1)) >> 14));
        band = lim(band + ((f * high) >> 15));
        y = ((low * wb) >> 15) + ((high * wt) >> 15) + ((((TONE_Q * (band >> 1)) >> 14) * wm) >> 15);
        out[i] = soft((pd_clamp(y, -0x7FFFFF, 0x7FFFFF) * pw) >> 8);   /* the power stage */
    }
    a->hp_lp = hp;
    a->tlow = low;
    a->tband = band;
    if (a->cab) {
        pd_svf_process(&a->cab_hp, out, out, frames);
        pd_svf_process(&a->cab_lp1, out, out, frames);
        pd_svf_process(&a->cab_lp2, out, out, frames);
    }
    {   /* the level, 0 .. 0.5x, ramped across the block */
        int32_t g = a->cur, step = pd_step_per_frame(a->level - g, frames);
        for (unsigned i = 0; i < frames; i++, g += step)
            out[i] = (lim(out[i]) * g) >> 16;
        a->cur = a->level;
    }
}

const PendelDSP PD_FX_AMP = {
    "Amp", sizeof(amp_t), NULL, PD_AMP_PARAMS, AMP_NAMES,
    amp_init, amp_process, amp_reset, amp_set,
};
