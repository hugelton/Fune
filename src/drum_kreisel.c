/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's drum voices. A lane picks a piece; a piece is a recipe for one generic voice: up to three oscillators (sine, triangle
 * or square, each with a pitch envelope that falls to its note), up to two noise paths (white noise through two state-variable
 * filters, the first may strike again as a clap's teeth), a metal bank (up to eight squares or sines, with their XOR, through two
 * filters and a drive), a clip (soft or one-sided), an output low-pass and high-pass. Every envelope may wait, rise, hold, decay,
 * and at a cut fall on with another time. The lane's TUNE moves the oscillators, its DCAY stretches the envelopes marked for it,
 * its TONE moves what the piece says (the snappy, a filter, the click, the metal, the sweep), LVL its level.
 *
 * The pieces are Kreisel's own integer voices after the owner's JRMS models (T8, T9, K5, K7, V1: JRMS's TD-80, TD-90, KD-55,
 * KP-77, VZ-1). They are simplified, not JRMS reconstructions. Integer in the sample loops; the envelopes
 * and the pitch run a block (32 samples) and are ramped across it; coefficients are made when a piece strikes.
 * Included by kreisel_audio.c. */

/* ----------------------------------------------------------- recipes --- */
enum { DW_SINE, DW_TRI, DW_SQR };                         /* an oscillator's wave */
enum { DF_NONE, DF_LP, DF_BP, DF_HP };                    /* a filter's mode */
enum { DC_NONE, DC_SOFT, DC_ASYM };                       /* the clip */
enum { TK_NONE, TK_LP, TK_NOISE, TK_CLICK, TK_METAL, TK_BAND, TK_SWEEP, TK_MIX };   /* what TONE moves */
#define DE_DECAY 1u                                       /* the lane's DCAY stretches this envelope */
/* times in 0.1 ms; levels Q12 (4096 = 1); frequencies in Hz (oscillators in 1/4 Hz) */
typedef struct { uint16_t dly, att, hold, tau, cut, tau2; uint8_t fl; } dpe_t;
typedef struct { uint8_t wave; uint16_t f4; int16_t lvl, pamt; uint16_t ptau; dpe_t e; } dpo_t;
typedef struct { uint8_t m1, q1; uint16_t f1; uint8_t m2, q2; uint16_t f2; } dpf_t;   /* q: Q x 10 */
typedef struct { dpf_t f; int16_t lvl; dpe_t e; uint8_t teeth; } dpn_t;               /* teeth: 0 none, else a DP_TEETH row + 1 */
typedef struct { uint8_t n, sine, duty, xr; uint16_t hz[8]; dpf_t f; int16_t lvl; uint8_t drive; dpe_t e; } dpm_t;
typedef struct {
    const char *name; char lab[3];
    dpo_t o[3]; dpn_t n[2]; dpm_t m;
    uint8_t clip, cdrive; uint16_t lp, hp;                 /* the clip and its drive (x 4), the output filters (Hz; 0 none) */
    uint8_t tone; uint16_t tlo, thi;                       /* what TONE moves, and between what */
    uint8_t range;                                         /* TUNE: +- semitones */
    uint8_t choke;                                         /* 1 closes the track's open hats, 2 an open hat */
    int16_t gain;
    uint8_t smp;                                           /* a sample piece: K_DRUM_SAMPLE[smp - 1] (drum_samples.c); 0 a recipe */
} dpiece_t;
#include "drum_samples.c"                                  /* (the samples, made by tools/gen_drum_samples.py) */

#define MS(x) ((uint16_t)((x) * 10.0 + .5))
#define LV(x) ((int16_t)((x) * 4096.0 + .5))
#define HZ4(x) ((uint16_t)((x) * 4.0 + .5))
#define ENV(d, a, h, t, c, t2, f) {MS(d), MS(a), MS(h), MS(t), MS(c), MS(t2), f}
#define EXPD(t) ENV(0, 0, 0, t, 0, 0, DE_DECAY)
#define OSC(w, hz, l, pa, pt, e) {w, HZ4(hz), LV(l), LV(pa), MS(pt), e}
#define FLT(m1, f1, q1, m2, f2, q2) {m1, (uint8_t)((q1) * 10), f1, m2, (uint8_t)((q2) * 10), f2}
#define NOI(f, l, e, th) {f, LV(l), e, th}
#define NOF FLT(DF_NONE, 0, 0, DF_NONE, 0, 0)

/* a clap's teeth: up to eight strikes (time, level) */
typedef struct { uint8_t n; uint16_t t[8]; int16_t l[8]; uint16_t tau; } dteeth_t;
static const dteeth_t DP_TEETH[2] = {
    {3, {MS(0), MS(11), MS(23)}, {LV(1), LV(1), LV(1)}, MS(3)},                                     /* T8 */
    {8, {MS(0), MS(6.5), MS(14.2), MS(19), MS(25.5), MS(30.2), MS(43.9), MS(50.9)},
        {LV(.92), LV(1), LV(.86), LV(.94), LV(.92), LV(.56), LV(.40), LV(.42)}, MS(6)},               /* K7 */
};

#define T8_METAL {170, 346, 315, 486, 824, 558}
#define K5_METAL {8035, 8245, 8568, 9486, 11885, 14685}
#define K7_RING {3651, 4185, 5033, 9676, 11615, 13475}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"   /* (a piece names only the parts it has) */
static const dpiece_t DP[] = {
    /* kicks */
    {"T8 BD", "BD", {OSC(DW_SINE, 52, 1.0, 1.4, 1.5, EXPD(65))}, {{NOF}}, {0}, DC_SOFT, 6, 0, 0, TK_LP, 305, 7200, 7, 0, LV(1.1)},
    {"T9 BD", "BD", {OSC(DW_TRI, 49, 1.0, 4.5, 17, ENV(0, 0, 40, 44, 0, 0, DE_DECAY))},
        {NOI(FLT(DF_LP, 6000, .7, DF_NONE, 0, 0), .5, ENV(0, 0, 1.5, 4, 0, 0, 0), 0)}, {0}, DC_SOFT, 7, 0, 0, TK_CLICK, 0, 0, 7, 0, LV(1.0)},
    {"K5 BD", "BD", {OSC(DW_SINE, 55.5, 1.0, .4, 4, ENV(0, 0, 0, 26, 45, 82, DE_DECAY))},
        {NOI(FLT(DF_HP, 4200, .7, DF_LP, 5200, .7), .3, EXPD(1.6), 0)}, {0}, DC_SOFT, 5, 0, 0, TK_CLICK, 0, 0, 7, 0, LV(1.1)},
    /* snares */
    {"T8 SD", "SD", {OSC(DW_SINE, 173.4, .8, 0, 0, EXPD(15)), OSC(DW_SINE, 336, .55, 0, 0, EXPD(10))},
        {NOI(FLT(DF_HP, 2750, .7, DF_LP, 5500, .7), .9, EXPD(30), 0)}, {0}, DC_SOFT, 4, 0, 0, TK_NOISE, 0, 0, 7, 0, LV(1.0)},
    {"T9 SD", "SD", {OSC(DW_TRI, 180, .8, .045, 20, ENV(2, 0, 0, 32, 0, 0, DE_DECAY)), OSC(DW_TRI, 285, .35, .045, 20, ENV(2, 0, 0, 32, 0, 0, DE_DECAY))},
        {NOI(FLT(DF_HP, 4050, 1.3, DF_NONE, 0, 0), .5, EXPD(4), 0), NOI(FLT(DF_LP, 8500, 1.9, DF_HP, 1540, .7), .7, ENV(0, 0, 24, 40, 0, 0, DE_DECAY), 0)},
        {0}, DC_SOFT, 6, 0, 0, TK_NOISE, 0, 0, 7, 0, LV(1.0)},
    {"K7 SD", "SD", {OSC(DW_SINE, 226, .7, 0, 0, EXPD(37))},
        {NOI(FLT(DF_HP, 2400, .7, DF_LP, 12000, .7), 1.0, ENV(0, 0, 0, 45, 90, 12, DE_DECAY), 0)}, {0}, DC_SOFT, 6, 7100, 0, TK_NOISE, 0, 0, 7, 0, LV(1.0)},
    /* toms, conga */
    {"T8 TOM", "TM", {OSC(DW_SINE, 125, 1.0, .2, 3, EXPD(60))},
        {NOI(FLT(DF_LP, 380, .7, DF_NONE, 0, 0), .35, EXPD(75), 0)}, {0}, DC_SOFT, 4, 0, 0, TK_NOISE, 0, 0, 12, 0, LV(1.0)},
    {"T9 TOM", "TM", {OSC(DW_TRI, 110, 1.0, .1, 180, EXPD(110)), OSC(DW_TRI, 198, .55, .1, 180, EXPD(50)), OSC(DW_TRI, 330, .42, .1, 180, EXPD(22))},
        {NOI(FLT(DF_BP, 3000, 1.2, DF_NONE, 0, 0), .6, EXPD(2), 0)}, {0}, DC_SOFT, 5, 0, 0, TK_NOISE, 0, 0, 12, 0, LV(.9)},
    {"K7 TOM", "TM", {OSC(DW_SINE, 129, 1.0, .19, 60, EXPD(88))},
        {NOI(FLT(DF_BP, 774, 1.1, DF_NONE, 0, 0), .15, EXPD(12), 0)}, {0}, DC_SOFT, 4, 0, 0, TK_SWEEP, 0, 0, 12, 0, LV(1.0)},
    {"T8 CNG", "CG", {OSC(DW_SINE, 250, 1.0, .2, 2, EXPD(40))}, {{NOF}}, {0}, DC_SOFT, 4, 0, 0, TK_LP, 600, 9000, 12, 0, LV(1.0)},
    /* rims, claves */
    {"T8 RS", "RS", {OSC(DW_SINE, 455, 1.0, 0, 0, EXPD(2.2)), OSC(DW_SINE, 1667, .2, 0, 0, EXPD(2.2))}, {{NOF}}, {0},
        DC_ASYM, 12, 0, 150, TK_NONE, 0, 0, 7, 0, LV(.7)},
    {"T9 RS", "RS", {OSC(DW_SINE, 221, 1.0, 0, 0, EXPD(17)), OSC(DW_SINE, 459, .6, 0, 0, EXPD(8.3))},
        {NOI(FLT(DF_HP, 3000, .7, DF_NONE, 0, 0), .12, EXPD(1), 0)}, {0}, DC_ASYM, 30, 0, 150, TK_NONE, 0, 0, 7, 0, LV(.6)},
    {"K5 RS", "RS", {OSC(DW_SINE, 500, 1.0, 0, 0, ENV(0, .5, 5.2, 3.6, 0, 0, DE_DECAY)), OSC(DW_SINE, 1467, .22, 0, 0, ENV(0, .5, 5.2, 3.6, 0, 0, DE_DECAY)),
        OSC(DW_SINE, 2400, .07, 0, 0, ENV(0, .5, 5.2, 3.6, 0, 0, DE_DECAY))}, {{NOF}}, {0}, DC_SOFT, 6, 0, 0, TK_NONE, 0, 0, 7, 0, LV(.9)},
    {"T8 CL", "CL", {OSC(DW_SINE, 2420, 1.0, 0, 0, EXPD(7.6))}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 7, 0, LV(.8)},
    {"K5 CL", "CL", {OSC(DW_SINE, 2470, 1.0, 0, 0, ENV(0, 0, 3, 2, 0, 0, DE_DECAY)), OSC(DW_SINE, 6897, .025, 0, 0, ENV(0, 0, 3, 2, 0, 0, DE_DECAY)),
        OSC(DW_SINE, 11506, .05, 0, 0, ENV(0, 0, 3, 2, 0, 0, DE_DECAY))}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 7, 0, LV(.8)},
    /* claps */
    {"T8 CP", "CP", {{0}}, {NOI(FLT(DF_BP, 1000, 1, DF_NONE, 0, 0), 1.0, ENV(0, 0, 0, 3, 0, 0, 0), 1),
        NOI(FLT(DF_BP, 1000, 1, DF_LP, 2200, .7), .7, ENV(30, 0, 0, 20, 50, 170, DE_DECAY), 0)}, {0}, DC_SOFT, 6, 0, 0, TK_BAND, 0, 0, 0, 0, LV(1.9)},
    {"K7 CP", "CP", {{0}}, {NOI(FLT(DF_HP, 765, .7, DF_BP, 1450, .74), 1.0, ENV(0, 0, 0, 6, 0, 0, 0), 2),
        NOI(FLT(DF_HP, 765, .7, DF_BP, 1450, .74), .5, ENV(57, 0, 0, 70, 0, 0, DE_DECAY), 0)}, {0}, DC_SOFT, 7, 10100, 0, TK_BAND, 0, 0, 0, 0, LV(1.7)},
    /* bells, shakers */
    {"T8 CB", "CB", {{0}}, {{NOF}}, {2, 0, 43, 0, {824, 558}, FLT(DF_BP, 960, 3, DF_NONE, 0, 0), LV(1.0), 0, ENV(0, 0, 0, 4.5, 5, 88, DE_DECAY)},
        DC_SOFT, 4, 0, 0, TK_NONE, 0, 0, 7, 0, LV(1.4)},
    {"K5 CB", "CB", {{0}}, {{NOF}}, {2, 0, 50, 1, {731, 3946}, FLT(DF_HP, 400, .7, DF_LP, 10000, .7), LV(1.3), 0, ENV(0, 0, 0, 30, 0, 0, DE_DECAY)},
        DC_SOFT, 4, 0, 0, TK_NONE, 0, 0, 7, 0, LV(1.0)},
    {"T8 MA", "MA", {{0}}, {NOI(FLT(DF_HP, 6500, .7, DF_LP, 15000, .7), 1.0, ENV(0, 10, 19, 2.2, 0, 0, DE_DECAY), 0)}, {0},
        DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 0, 0, LV(1.2)},
    {"V1 SHA", "SH", {{0}}, {NOI(FLT(DF_LP, 9500, .7, DF_NONE, 0, 0), 1.0, ENV(0, 0, 0, 34.8, 160, .3, DE_DECAY), 0)}, {0},
        DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 0, 0, LV(1.0)},
    /* hats, cymbals */
    {"T8 CH", "CH", {{0}}, {{NOF}}, {6, 0, 48, 0, T8_METAL, FLT(DF_BP, 7100, 1.5, DF_HP, 9000, .7), LV(0.9), 16, ENV(0, .3, 0, 16, 0, 0, DE_DECAY)},
        DC_NONE, 0, 17000, 0, TK_METAL, 0, 0, 0, 1, LV(1.3)},
    {"T8 OH", "OH", {{0}}, {{NOF}}, {6, 0, 48, 0, T8_METAL, FLT(DF_BP, 7100, 1.5, DF_HP, 5500, .7), LV(0.9), 16, ENV(0, .3, 0, 295, 340, 4, DE_DECAY)},
        DC_NONE, 0, 16000, 0, TK_METAL, 0, 0, 0, 2, LV(1.3)},
    {"K5 CH", "CH", {{0}}, {NOI(FLT(DF_HP, 16000, .7, DF_NONE, 0, 0), 1.2, ENV(0, 0, 0, 13, 25, 45, DE_DECAY), 0)},
        {6, 1, 50, 0, K5_METAL, NOF, LV(.8), 0, ENV(0, 0, 0, 13, 25, 45, DE_DECAY)}, DC_NONE, 0, 0, 0, TK_METAL, 0, 0, 0, 1, LV(.8)},
    {"K5 OH", "OH", {{0}}, {NOI(FLT(DF_HP, 16000, .7, DF_NONE, 0, 0), 1.2, ENV(0, 0, 0, 400, 170, 25, DE_DECAY), 0)},
        {6, 1, 50, 0, K5_METAL, NOF, LV(.8), 0, ENV(0, 0, 0, 400, 170, 25, DE_DECAY)}, DC_NONE, 0, 0, 0, TK_METAL, 0, 0, 0, 2, LV(.8)},
    {"K7 CH", "CH", {{0}}, {NOI(FLT(DF_HP, 5200, .7, DF_LP, 16500, .7), .25, ENV(0, 0, 0, 100, 0, 0, DE_DECAY), 0)},
        {6, 0, 43, 0, K7_RING, FLT(DF_HP, 2600, .7, DF_LP, 15500, .7), LV(.8), 6, ENV(0, 0, 0, 100, 0, 0, DE_DECAY)}, DC_SOFT, 6, 0, 0, TK_METAL, 0, 0, 0, 1, LV(1.0)},
    {"K7 OH", "OH", {{0}}, {NOI(FLT(DF_HP, 5200, .7, DF_LP, 16500, .7), .25, ENV(0, 0, 0, 200, 900, 150, DE_DECAY), 0)},
        {6, 0, 43, 0, K7_RING, FLT(DF_HP, 2600, .7, DF_LP, 15500, .7), LV(.8), 6, ENV(0, 0, 0, 200, 900, 150, DE_DECAY)}, DC_SOFT, 6, 0, 0, TK_METAL, 0, 0, 0, 2, LV(1.0)},
    {"K5 CY", "CY", {{0}}, {NOI(FLT(DF_HP, 9000, .7, DF_NONE, 0, 0), .9, ENV(0, 0, 0, 14, 25, 145, DE_DECAY), 0)},
        {6, 1, 50, 0, K5_METAL, FLT(DF_HP, 9000, .7, DF_NONE, 0, 0), LV(.8), 0, ENV(0, 0, 0, 14, 25, 145, DE_DECAY)}, DC_NONE, 0, 0, 0, TK_METAL, 0, 0, 0, 0, LV(.9)},
    {"K7 CY", "CY", {{0}}, {NOI(FLT(DF_HP, 5200, .7, DF_NONE, 0, 0), .15, ENV(0, 0, 0, 40, 60, 500, DE_DECAY), 0)},
        {6, 0, 44, 0, {3650, 4184, 5032, 9676, 11615, 13474}, FLT(DF_HP, 2500, .7, DF_LP, 16500, .7), LV(.9), 7, ENV(0, 0, 0, 40, 60, 500, DE_DECAY)},
        DC_SOFT, 7, 0, 0, TK_METAL, 0, 0, 0, 0, LV(1.0)},
    /* digital */
    {"V1 PO", "PO", {OSC(DW_SQR, 769, .6, 0, 0, ENV(0, 0, 0, 9.4, 30, .2, DE_DECAY))}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 12, 0, LV(1.4)},
    {"V1 PI", "PI", {OSC(DW_SQR, 1667, .6, 0, 0, ENV(0, 0, 0, 6.25, 20, .2, DE_DECAY))}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_NONE, 0, 0, 12, 0, LV(1.4)},
    /* samples (assets/drums: the owner's, after Virtuosity Drums, CC0): TUNE the speed (+-12 st), TONE a low-pass, DCAY shortens */
    {"SP BD", "BD", {{0}}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_LP, 300, 18000, 12, 0, LV(1.0), 1},
    {"SP CH", "CH", {{0}}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_LP, 4000, 20000, 12, 1, LV(1.5), 2},
    {"SP OH", "OH", {{0}}, {{NOF}}, {0}, DC_NONE, 0, 0, 0, TK_LP, 4000, 20000, 12, 2, LV(2.2), 3},
    /* TP-1 ("Techno Pop 1": no machine, the owner's): a zap, a sine dropping ~3 octaves in ~20 ms; TONE how far it falls */
    {"TP ZP", "ZP", {OSC(DW_SINE, 120, 1.0, 7.9, 18, EXPD(140))}, {{NOF}}, {0}, DC_SOFT, 3, 0, 0, TK_SWEEP, 0, 0, 12, 0, LV(1.2)},
};
#pragma GCC diagnostic pop
#define DP_COUNT ((int)(sizeof DP / sizeof DP[0]))
_Static_assert(sizeof DP / sizeof DP[0] == K_DRUM_PIECES, "K_DRUM_PIECES counts DP[]");

const char *k_audio_drum_piece(int p) { return DP[p < 0 ? 0 : p >= DP_COUNT ? DP_COUNT - 1 : p].name; }   /* (tests: the DSP's names, in its order) */

/* ----------------------------------------------------------- helpers --- */
/* 2^(x / 65536), x in [-16 << 16, 16 << 16], Q16 (a cubic for the fraction) */
static uint32_t dexp2_q16(int32_t x)
{
    int32_t ip = x >> 16, f = x & 0xFFFF;   /* x = ip + f / 65536, f >= 0 */
    uint32_t m;
    /* 2^f on [0, 1): 1 + f (0.6951 + f (0.2262 + 0.0787 f)), Q16 */
    m = 65536u + (uint32_t)k_mulsh(f, 45551 + k_mulsh(f, 14825 + k_mulsh(f, 5158, 16), 16), 16);
    if (ip >= 0) return ip > 15 ? 0xFFFFFFFFu : m << ip;
    return ip < -16 ? 0 : m >> -ip;
}
/* the per-block multiplier (Q16) of an exponential with time constant tau (0.1 ms): e^(-32 / (tau * 4.41)) */
static uint32_t dkblock(uint32_t tau10)
{
    if (!tau10) return 0;
    /* -32 / (4.41 tau) / ln 2 in Q16 = -(32 * 65536 / (4.41 * 0.6931)) / tau = -686178 / tau */
    return dexp2_q16(-(int32_t)(686178u / tau10 > (16u << 16) ? (16u << 16) : 686178u / tau10));
}
/* a filter's coefficients from Hz: g = tan(pi f / fs) (Q12, from the sine table), k = 1 / Q (Q12) */
static void dsvf_set(KDrumSvf *c, uint32_t hz, uint32_t q10)
{
    uint32_t ph;
    int32_t s, co, g, den;
    if (hz > 19500u) hz = 19500u;
    if (hz < 20u) hz = 20u;
    ph = hz * 48696u;                                    /* pi f / fs as a phase: f / (2 fs) of a turn */
    s = sine_i(ph);
    co = sine_i(ph + 0x40000000u);
    if (co < 64) co = 64;
    g = (s << 12) / co;
    if (g > 26000) g = 26000;
    c->k = q10 ? (int32_t)(40960u / q10) : 5793;          /* 1 / Q (Q 0.7 when not given) */
    den = 4096 + ((g * (g + c->k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}
static inline int32_t dsvf(const KDrumSvf *c, int mode, int32_t in, int32_t *s)   /* s[2]: its state; mode DF_* */
{
    int32_t v3 = in - s[1], v1 = (c->a1 * s[0] + c->a2 * v3) >> 13, v2 = s[1] + ((c->a2 * s[0] + c->a3 * v3) >> 13);
    s[0] = clamp(2 * v1 - s[0], -150000, 150000);
    s[1] = clamp(2 * v2 - s[1], -150000, 150000);
    return mode == DF_LP ? v2 : mode == DF_BP ? v1 : in - ((c->k * v1) >> 12) - v2;
}
static inline uint32_t drng(uint32_t *r) { uint32_t s = *r; s ^= s << 13; s ^= s >> 17; s ^= s << 5; *r = s; return s; }

/* --------------------------------------------------------- envelopes --- */
/* one envelope: its value (Q30) at the block's end, from its recipe; dk scales its times (Q12, DCAY) */
static void denv_start(KDrumEnv *e, const dpe_t *r, uint32_t dk)
{
    const uint32_t s = (r->fl & DE_DECAY) ? dk : 4096u;
    memset(e, 0, sizeof *e);
    e->dly = (uint32_t)r->dly * 441u / 100u / CTL;                          /* blocks */
    e->hold = (uint32_t)r->hold * 441u / 100u / CTL;
    e->cut = r->cut ? (uint32_t)(k_umul(r->cut, s) / 4096u) * 441u / 100u / CTL : 0u;
    e->k1 = dkblock((uint32_t)(k_umul(r->tau, s) / 4096u) + 1u);
    e->k2 = r->tau2 ? dkblock(r->tau2) : e->k1;
    e->ka = r->att ? dkblock(r->att) : 0u;
    e->v = r->att || e->dly ? 0 : 1 << 30;
}
static void denv_block(KDrumEnv *e)   /* the next block's value */
{
    const uint32_t t = e->t++;
    if (t < e->dly) { e->v = 0; return; }
    if (t == e->dly && !e->ka) e->v = 1 << 30;
    if (t < e->dly + e->hold || e->ka) {   /* the hold (rising while it has an attack), or an attack still under full */
        if (e->ka) {
            e->v = (1 << 30) - k_mulsh((1 << 30) - e->v, (int32_t)e->ka, 16);
            if (t >= e->dly + e->hold && e->v > (1 << 30) - (1 << 25)) e->ka = 0;   /* (risen: it decays from the next block) */
        }
        return;
    }
    e->v = k_mulsh(e->v, (int32_t)(e->cut && t >= e->cut ? e->k2 : e->k1), 16);
}

/* ----------------------------------------------------------- strike --- */
/* A stolen voice's tail: a hit that finds the pool full takes the oldest voice's slot, and cutting that voice leaves a step (its
 * last output to nothing): a click. So the slot carries that last value down to 0 over two blocks (~1.5 ms), added to what the
 * slot then plays. File-level, as the engines' state is (one KAudio plays at a time); d_pool says whose slots they are. */
#define DTAIL (2 * CTL)
static const KDrumVoice *d_pool;
static int32_t d_last[K_DRUM_POOL], d_tail[K_DRUM_POOL];   /* a slot's last output (its voice and tail), the value its tail falls from */
static uint8_t d_tn[K_DRUM_POOL];                          /* the tail's samples left */
static uint8_t d_tt[K_DRUM_POOL];                          /* the tail's track: the stolen voice's (drum_tails adds it there, not
                                                            * to the track the slot now plays for: it once leaked into that one) */
static int dslot(const KDrumVoice *v)   /* the slot in d_pool, or -1 (compared, not divided: a division by the voice's size
                                         * would be a multiply by its reciprocal, 32 x 32 -> 64 inline) */
{
    int i;
    if (d_pool) for (i = 0; i < K_DRUM_POOL; i++) if (v == d_pool + i) return i;
    return -1;
}
#define DRUM_METAL 4   /* metal voices sounding at once (the FM-1's time: eight long hats would take most of a block) */
static void drum_on(KAudio *a, int t, int lane)
{
    const int16_t *lp = a->lane_p[t][lane];
    const uint8_t *kit = a->dp[t];
    const dpiece_t *P = &DP[lp[0] < DP_COUNT ? lp[0] : 0];
    uint32_t hr = 0x2545F491u ^ (uint32_t)a->frame ^ ((uint32_t)lane * 0x9E3779B9u);   /* HUMN's dice */
    int32_t lfo = 0, st16, dk16, tone = lp[2], lvlq = 32767;
    uint32_t dk, tr;
    KDrumVoice *v = 0, *old = 0;
    int i, steal = 0;
    if (kit[4] == 0 || kit[4] == lane + 1) lfo = mulq15(trk[t].lfo_val, trk[t].lfo_fade);   /* the LFO, as it stands at the strike */
    /* TUNE: the lane's (its piece's range) and the kit's (+-12 st), 1/16 semitone; DCAY: the lane's and the kit's, Q16 of log2 */
    st16 = ((int32_t)lp[1] - 64) * P->range * 16 / 64 + ((int32_t)kit[0] - 64) * 192 / 64;
    dk16 = ((int32_t)lp[3] - 64) * 2048 + ((int32_t)kit[1] - 64) * 2048;   /* (* not <<: a negative value's shift is undefined) */
    if (kit[3]) {   /* HUMN: each hit a little off: up to +-40 cents, +-16 of TONE, -25 % level */
        int32_t h = kit[3];
        st16 += ((int32_t)(drng(&hr) >> 24) - 128) * h / (127 * 20);       /* (1/16 st: 6.4 = 40 cents) */
        tone += ((int32_t)(drng(&hr) >> 24) - 128) * h / (127 * 8);
        lvlq -= (int32_t)(drng(&hr) >> 24) * h * 32 / 127;
    }
    if (lfo) {   /* the LFO's routings, read at the strike: pitch +-12 st, TONE +-64, DCAY x 1/2 .. x 2, level */
        st16 += (lfo * a->tp[t][P_LD_PIT]) / (63 * 32768 / 192);
        tone += (lfo * a->tp[t][P_LD_FLT]) / (63 * 512);
        dk16 += (lfo * a->tp[t][P_LD_SHP]) / 63 * 2;
        lvlq = clamp(lvlq + (lfo * a->tp[t][P_LD_AMP]) / 63, 0, 32767);
    }
    tone = clamp(tone, 0, 127);
    dk = dexp2_q16(clamp(dk16, -(3 << 16), 3 << 16)) >> 4;               /* x 1/8 .. x 8, Q12 */
    tr = dexp2_q16(st16 * 65536 / 192);                                /* Q16 */
    for (i = 0; i < K_DRUM_POOL; i++) {
        KDrumVoice *d = &a->drum[i];
        if (d->active && d->track == t && d->lane == lane) { v = d; break; }
        if (!v && !d->active) v = d;
        if (d->active && (!old || d->age > old->age)) old = d;
    }
    if (!v) { v = old; steal = 1; }   /* (the pool full: the oldest gives its slot, its tail below) */
    if (P->m.n) {   /* at most DRUM_METAL metal voices at once (each is the costliest: squares, filters, noise): the oldest gives way */
        int nmet = 0;
        KDrumVoice *om = 0;
        for (i = 0; i < K_DRUM_POOL; i++) {
            KDrumVoice *d = &a->drum[i];
            if (d == v || !d->active || !d->mn) continue;
            nmet++;
            if (!d->choked && (!om || d->age > om->age)) om = d;   /* (one already giving way is not chosen twice) */
        }
        if (nmet >= DRUM_METAL && om) om->choked = 1;   /* (the choke's short fade, not a cut: no click) */
    }
    if (P->choke == 1)                                                 /* a closed hat closes the track's open ones */
        for (i = 0; i < K_DRUM_POOL; i++) {
            KDrumVoice *d = &a->drum[i];
            if (d->active && d->track == t && DP[d->piece].choke == 2) d->choked = 1;
        }
    for (i = 0; i < K_DRUM_POOL; i++) if (a->drum[i].active && a->drum[i].age < 250) a->drum[i].age++;
    if (d_pool != a->drum) { d_pool = a->drum; memset(d_tn, 0, sizeof d_tn); memset(d_last, 0, sizeof d_last); }
    {   /* a stolen voice's last output falls away over DTAIL (drum_block); any other slot starts clean */
        const int k = dslot(v);
        d_tail[k] = steal ? d_last[k] : 0;
        d_tn[k] = (uint8_t)(steal && d_last[k] ? DTAIL : 0);
        d_tt[k] = v->track;   /* (v is still the stolen voice here) */
        d_last[k] = 0;
    }
    memset(v, 0, sizeof *v);
    v->active = 1; v->track = (uint8_t)t; v->lane = (uint8_t)lane; v->piece = lp[0] < DP_COUNT ? lp[0] : 0;
    v->rng = 0x9E3779B9u ^ (uint32_t)a->frame ^ ((uint32_t)lane << 8) ^ ((uint32_t)t << 16);
    if (!v->rng) v->rng = 1;
    v->gain = mulq15(k_mulsh(lp[4] * 258, P->gain * 8, 15), lvlq);   /* (the first product past 2^31 for a loud piece, SP OH at LVL 116: 64 bits) */          /* LVL x the piece's level (Q12 -> Q15), HUMN, the LFO */
    v->crush = kit[2] >> 4;
    v->clk = lp[K_LANE_ENV + 3] * 327;                                 /* CLIK */
    {   /* PAN: a balance, both full at the centre (as loud as before there was a pan), the far side falling to 0 at the end */
        const int pn = lp[K_LANE_PAN] > 100 ? 100 : lp[K_LANE_PAN];
        v->pan_l = (int16_t)(pn <= 50 ? 4096 : (100 - pn) * 4096 / 50);
        v->pan_r = (int16_t)(pn >= 50 ? 4096 : pn * 4096 / 50);
    }
    for (i = 0; i < 3; i++) {
        const dpo_t *o = &P->o[i];
        int32_t lvl = o->lvl, pamt = o->pamt;
        if (!o->f4 || !lvl) continue;
        if (P->tone == TK_SWEEP) pamt = (pamt * tone) >> 6;
        {   /* the lane's PDEP (x 1/4 .. x 4, and a sweep of its own above the middle) */
            const int32_t pd = lp[K_LANE_ENV + 1] - 50;
            pamt = k_mulsh(pamt, (int32_t)dexp2_q16(pd * 2621), 16) + (pd > 0 ? pd * 82 : 0);
        }
        if (P->tone == TK_MIX) lvl = i ? (lvl * tone) >> 6 : (lvl * (128 - tone)) >> 6;
        v->inc[i] = k_umulsh((uint32_t)o->f4 * 24348u, tr, 16);   /* (f4 * 24348 < 2^31 to 22 kHz) */
        v->lvl[i] = (int16_t)lvl;
        v->pe[i] = pamt;                                               /* Q12, falls with ptau */
        v->kp[i] = dkblock(k_umulsh(o->ptau ? o->ptau : MS(3), dexp2_q16((lp[K_LANE_ENV + 2] - 50) * 2621), 16) + 1u);   /* PTIM */
        denv_start(&v->e[i], &o->e, dk);
    }
    for (i = 0; i < 2; i++) {
        const dpn_t *n = &P->n[i];
        int32_t lvl = n->lvl;
        uint32_t f1 = n->f.f1, f2 = n->f.f2;
        if (!lvl) continue;
        if (P->tone == TK_NOISE || P->tone == TK_CLICK) lvl = (lvl * tone) >> 6;
        if (P->tone == TK_METAL) lvl = (lvl * (128 - tone)) >> 6;
        if (P->tone == TK_BAND) { const uint32_t b = dexp2_q16((tone - 64) * 1024); f1 = (f1 * b) >> 16; f2 = (f2 * b) >> 16; }
        v->nlvl[i] = (int16_t)lvl;
        v->nm[i][0] = n->f.m1; v->nm[i][1] = n->f.m2;
        if (n->f.m1) dsvf_set(&v->nf[i][0], f1, n->f.q1);
        if (n->f.m2) dsvf_set(&v->nf[i][1], f2, n->f.q2);
        denv_start(&v->e[3 + i], &n->e, dk);
        if (n->teeth) { v->teeth = n->teeth; v->tooth = 1; v->e[3 + i].k1 = v->e[3 + i].k2 = dkblock(DP_TEETH[n->teeth - 1].tau); }
    }
    if (P->m.n) {
        const dpm_t *m = &P->m;
        int32_t lvl = m->lvl;
        if (P->tone == TK_METAL && P->n[0].lvl) lvl = (lvl * tone) >> 6;   /* (with a noise path: TONE balances them) */
        v->mlvl = (int16_t)lvl;
        v->mn = m->n; v->msine = m->sine; v->mxor = m->xr;
        v->mdrive = (uint8_t)(P->tone == TK_METAL && !P->n[0].lvl ? 1 + (m->drive * tone >> 6) : m->drive);
        for (i = 0; i < m->n && i < 8; i++) v->minc[i] = m->hz[i] * 97392u;
        v->mduty = (uint32_t)m->duty * 42949673u;                       /* duty / 100 of a turn */
        v->mm[0] = m->f.m1; v->mm[1] = m->f.m2;
        if (m->f.m1) dsvf_set(&v->mf[0], m->f.f1, m->f.q1);
        if (m->f.m2) dsvf_set(&v->mf[1], m->f.f2, m->f.q2);
        denv_start(&v->e[5], &m->e, dk);
    }
    if (P->smp) {   /* a sample: read from its start at the speed TUNE gives; DCAY under its middle shortens it, ATK fades it in */
        const int32_t dk12 = (int32_t)dk;   /* (Q12: x 1/8 .. x 8, 4096 at the middles) */
        v->ph[0] = 0;
        v->inc[0] = tr;                    /* Q16 a sample */
        v->pe[0] = 32767;                  /* its gain, Q15, falling a block at a time */
        v->kp[0] = dk12 >= 4096 ? 65536u :  /* (at or over the middle: as recorded) */
                   dexp2_q16(-(int32_t)((65536LL * CTL * 4096) / (k_smul(dk12, 6615 * 69) / 100 + 1)));   /* tau = 150 ms x dk */
        v->ph[1] = 0;
        v->ph[2] = (uint32_t)lp[K_LANE_ENV] * 882u / 100u;   /* ATK: 0 .. 20 ms of samples */
        v->pe[1] = 0;                                         /* ATK's gain, Q24, rising by kp[1] a sample: ph[1] / ph[2] */
        v->kp[1] = v->ph[2] ? ((1u << 24) + v->ph[2] / 2u) / v->ph[2] : 0u;   /* (the division once here, not a sample) */
    }
    if (lp[K_LANE_ENV]) {   /* the lane's ATK: every envelope rises (0 .. 20 ms) instead of striking */
        const uint32_t ka = dkblock((uint32_t)lp[K_LANE_ENV] * 2u / 3u + 1u);
        for (i = 0; i < 6; i++) if (!v->e[i].dly) { v->e[i].ka = ka; v->e[i].v = 0; }
    }
    {   /* the output filters: TONE's low-pass, or the piece's */
        uint32_t lph = P->lp;
        if (P->tone == TK_LP) lph = P->tlo + k_umulsh(P->thi - P->tlo, (uint32_t)(tone * tone), 14);
        if (lph) { uint32_t w = lph * 4664u >> 10; v->lpk = (int32_t)(w > 32767u ? 32767u : w); }   /* ~2 pi f / fs, Q15 */
        if (P->hp) { uint32_t w = (uint32_t)P->hp * 4664u >> 10; v->hpk = (int32_t)(w > 32767u ? 32767u : w); }
    }
    a->triggers[t]++;
    a->last_trigger_frame[t] = a->frame;
}

/* ------------------------------------------------------------ render --- */
static inline int32_t dosc(int w, uint32_t ph)
{
    if (w == DW_SINE) return sine_i(ph);
    if (w == DW_TRI) { int32_t x = (int32_t)(ph >> 15) - 65536; return (x < 0 ? -x : x) - 32768; }   /* +-32767 */
    return (int32_t)ph < 0 ? -26000 : 26000;
}
/* one block of a drum voice added into out; 0 when it has ended */
/* a sample piece's block: the sample read at its speed (linear between its points), its gain, ATK, CLIK, then the output as a
 * recipe's (the low- and high-pass, the level, CRSH) */
static int drum_block_smp(KDrumVoice *v, const dpiece_t *P, int32_t *out)
{
    const KDrumSample *S = &K_DRUM_SAMPLE[P->smp - 1];
    uint32_t pos = v->ph[0], i;
    const uint32_t inc = v->inc[0];
    int32_t g = v->pe[0], s;
    if (v->choked) { v->kp[0] = 20000u; v->choked = 0; }   /* closed by a closed hat: gone in ~1.5 ms */
    if ((pos >> 16) + 1u >= S->n || g < 16) { v->active = 0; return 0; }
    for (i = 0; i < CTL; i++) {
        const uint32_t k = pos >> 16;
        if (k + 1u < S->n) {
            const int32_t a = S->d[k], b = S->d[k + 1u];
            s = a + (((b - a) * (int32_t)((pos >> 1) & 0x7FFFu)) >> 15);
        } else s = 0;
        s = mulq15(s, g);
        if (v->ph[1] < v->ph[2]) { s = k_mulsh(s, v->pe[1], 24); v->pe[1] += (int32_t)v->kp[1]; v->ph[1]++; }   /* ATK */
        if (v->clk > 64) { s += mulq15((int32_t)drng(&v->rng) >> 17, v->clk); v->clk = (v->clk * 31300) >> 15; }   /* CLIK */
        if (v->lpk) { v->lp += ((s - v->lp) * v->lpk) >> 15; s = v->lp; }
        if (v->hpk) { v->hp += ((s - v->hp) * v->hpk) >> 15; s -= v->hp; }
        s = mulq15(clamp(s, -32767, 32767), (int32_t)v->gain);
        if (v->crush) s &= ~((1 << v->crush) - 1);
        out[i] += mulq15(s, VOICE_FS);
        pos += inc;
    }
    v->ph[0] = pos;
    v->pe[0] = k_mulsh(g, (int32_t)v->kp[0], 16);
    v->age = (uint8_t)(v->age + 1 > 250 ? 250 : v->age + 1);
    return 1;
}
static int drum_block1(KDrumVoice *v, int32_t *out)
{
    const dpiece_t *P = &DP[v->piece];
    int32_t a0[6], da[6], acc[CTL], s;
    uint32_t i, inc[3];
    int c, live = 0;
    if (P->smp) return drum_block_smp(v, P, out);
    if (v->choked) {   /* closed by a closed hat: every envelope ends in ~1.5 ms */
        for (c = 0; c < 6; c++) { v->e[c].k1 = v->e[c].k2 = dkblock(15); v->e[c].hold = 0; v->e[c].cut = 0; }
        v->choked = 0;
    }
    if (v->teeth) {   /* a clap's teeth: each strikes its envelope again */
        const dteeth_t *T = &DP_TEETH[v->teeth - 1];
        const uint32_t now = v->e[3].t;
        while (v->tooth < T->n && now >= (uint32_t)T->t[v->tooth] * 441u / 100u / CTL) { v->e[3].v = (int32_t)(((int64_t)T->l[v->tooth] << 18)); v->tooth++; }
    }
    for (c = 0; c < 6; c++) {   /* the ramps: this block from the last value to the next */
        const int on = c < 3 ? v->inc[c] != 0 : c < 5 ? v->nlvl[c - 3] != 0 : v->mn != 0;
        if (!on) { a0[c] = da[c] = 0; continue; }
        a0[c] = v->e[c].v >> 15;
        denv_block(&v->e[c]);
        da[c] = ((v->e[c].v >> 15) - a0[c]) / CTL;
        if (v->e[c].v > (1 << 17) || v->e[c].t <= v->e[c].dly + v->e[c].hold) live = 1;
    }
    if (!live) { v->active = 0; return 0; }
    for (c = 0; c < 3; c++) {   /* the pitch: the note, above it by the sweep falling */
        inc[c] = v->inc[c];
        if (inc[c] && v->pe[c]) {
            inc[c] += k_umulsh(inc[c], (uint32_t)v->pe[c], 12);
            v->pe[c] = k_mulsh(v->pe[c], (int32_t)v->kp[c], 16);
        }
    }
    for (i = 0; i < CTL; i++) acc[i] = 0;
    for (c = 0; c < 3; c++) {   /* the oscillators */
        uint32_t ph = v->ph[c];
        int32_t g = a0[c], d = da[c];
        const int32_t l = v->lvl[c];
        const int w = P->o[c].wave;
        if (!inc[c]) continue;
        if (v->e[c].t <= v->e[c].dly) { v->ph[c] = 0; continue; }       /* (waits, its phase at 0: the shell's late start) */
        for (i = 0; i < CTL; i++, g += d) { acc[i] += (((dosc(w, ph) * l) >> 12) * g) >> 15; ph += inc[c]; }
        v->ph[c] = ph;
    }
    for (c = 0; c < 2; c++) {   /* the noise paths */
        int32_t g = a0[3 + c], d = da[3 + c], *s0 = v->ns[c][0], *s1 = v->ns[c][1];
        const int32_t l = v->nlvl[c];
        const int m1 = v->nm[c][0], m2 = v->nm[c][1];
        if (!l || (!g && !d)) continue;
        for (i = 0; i < CTL; i++, g += d) {
            int32_t x = (int32_t)drng(&v->rng) >> 17, y;                /* white, +-16384 */
            if (m1) x = dsvf(&v->nf[c][0], m1, x, s0);
            if (m2) x = dsvf(&v->nf[c][1], m2, x, s1);
            y = clamp((x * l) >> 11, -65535, 65535);                    /* (x 2: filtered noise is quieter than a tone) */
            acc[i] += (y * (g >> 1)) >> 14;
        }
    }
    if (v->mn) {   /* the metal: squares (or sines), their XOR, two filters, a drive */
        int32_t g = a0[5], d = da[5];
        const int32_t l = v->mlvl, dr = v->mdrive;
        const uint32_t duty = v->mduty;
        for (i = 0; i < CTL; i++, g += d) {
            int32_t x = 0;
            int k;
            for (k = 0; k < v->mn; k++) {
                v->mph[k] += v->minc[k];
                x += v->msine ? sine_i(v->mph[k]) >> 2 : (v->mph[k] < duty ? 8192 : -8192);
            }
            if (v->mxor) x += ((v->mph[0] < duty) ^ (v->mph[1] < duty)) ? 9000 : -9000;
            if (v->mm[0]) x = dsvf(&v->mf[0], v->mm[0], x, v->ms[0]);
            if (v->mm[1]) x = dsvf(&v->mf[1], v->mm[1], x, v->ms[1]);
            if (dr) x = softclip(x * dr);
            x = clamp((x * l) >> 12, -65535, 65535);
            acc[i] += (x * (g >> 1)) >> 14;
        }
    }
    if (v->clk > 64)   /* the lane's CLIK: white noise, falling ~1 ms */
        for (i = 0; i < CTL; i++) { acc[i] += mulq15((int32_t)drng(&v->rng) >> 17, v->clk); v->clk = (v->clk * 31300) >> 15; }
    for (i = 0; i < CTL; i++) {   /* the clip, the output filters, the level */
        s = acc[i];
        if (P->clip == DC_SOFT) s = softclip(s * P->cdrive >> 2) * 3 >> 2;
        else if (P->clip == DC_ASYM) { s = softclip((s * P->cdrive >> 2) + 4000) - softclip(4000); }
        if (v->lpk) { v->lp += ((s - v->lp) * v->lpk) >> 15; s = v->lp; }
        if (v->hpk) { v->hp += ((s - v->hp) * v->hpk) >> 15; s -= v->hp; }
        s = mulq15(clamp(s, -32767, 32767), (int32_t)v->gain);
        if (v->crush) s &= ~((1 << v->crush) - 1);
        out[i] += mulq15(s, VOICE_FS);
    }
    v->age = (uint8_t)(v->age + 1 > 250 ? 250 : v->age + 1);
    return 1;
}
/* a voice's block; its slot's last output kept for a steal */
static int drum_block(KDrumVoice *v, int32_t *out)
{
    const int k = dslot(v);
    const int32_t o0 = out[CTL - 1];
    const int r = drum_block1(v, out);
    if (k >= 0) d_last[k] = r ? out[CTL - 1] - o0 : 0;
    return r;
}
/* the tails of track t's stolen voices (their last output falling to 0) into its block: 1 while one sounds */
static int drum_tails(KAudio *a, int t, int32_t *out)
{
    int k, i, any = 0;
    if (d_pool != a->drum) return 0;
    for (k = 0; k < K_DRUM_POOL; k++) {
        const int32_t y = d_tail[k];
        int n = d_tn[k];
        if (!n || d_tt[k] != t) continue;
        for (i = 0; i < CTL && n > 0; i++) out[i] += y * --n / DTAIL;
        d_tn[k] = (uint8_t)n;
        any = 1;
    }
    return any;
}
