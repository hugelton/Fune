/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's ORGAN: a tonewheel drawbar organ. Nine sines a voice at the drawbar footages 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5'
 * 1 1/3' 1' (0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 times the note), each bar 0 .. 8 (about 3 dB a step), on their own free-running
 * phases (a note starts wherever its wheels are); a partial above ~6 kHz folds back an octave, as the top of a tonewheel set.
 * The louder the registration the less each bar gets (a soft normalisation). PERC: a decaying 2nd (4') or 3rd (2 2/3')
 * harmonic, single-triggered (legato gets none), normal or soft (PVOL), fast or slow (PDCY); with it on the 1' bar is silent
 * and, at normal level, the bars drop ~3 dB, as on the real circuit. CLICK: high-passed noise at the key contact (half at the
 * release, a quarter). DRV: a soft overdrive a voice. VIB: the scanner's six (~6.9 Hz): V1 .. V3 a vibrato, deeper each; C1 .. C3
 * the same on half the partials, the others steady (a chorus, cheaply). ROTR: two rotors (low and high, split ~800 Hz)
 * slow or fast, ramping between, as an amplitude and Doppler modulation folded into the block's gains and increments.
 * Its parameters, on P_E0 .. :
 *   SOUND  0 ROTR (OFF SLOW FAST), 1 VIB (OFF V1 V2 V3 C1 C2 C3), 2 PERC (OFF 2ND 3RD), 3 .. 11 the bars (16' .. 1', 0 .. 8)
 *   EDIT   12 PVOL (NORM SOFT), 13 PDCY (FAST SLOW), 14 CLICK, 15 DRV, 16 LEVEL, 17 VEL
 * Kreisel's own, after Felucca-Engines' WHEEL (MIT, the same author), whose sixteen registrations and SUB / BODY / TOP offsets
 * gave way to the nine bars themselves. Integer only; the bars, the percussion and the rotors run once a block, a sample costs a
 * table lookup and a multiply-add a sounding partial. Included by kreisel_audio.c as the other engines are. */
#define KO_NP 9
#define KO_FOLD ((uint32_t)(584366000ull * 44100u / FS))    /* ~6 kHz as a phase increment */
#define KO_XOVER ((uint32_t)(77915000ull * 44100u / FS))    /* ~800 Hz: the rotors' split */
#define KO_HZ(h) ((int32_t)((h) * 3116457.0 * (44100.0 / FS)))   /* Hz -> a block's phase increment (CTL samples) */

static const uint16_t KO_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};   /* bar 0 .. 8 -> Q15 */
static const uint32_t KO_FOLD_LIM[KO_NP] = {   /* the note's increment above which the partial would pass KO_FOLD */
    KO_FOLD * 2u, KO_FOLD / 3u * 2u, KO_FOLD, KO_FOLD / 2u, KO_FOLD / 3u, KO_FOLD / 4u, KO_FOLD / 5u, KO_FOLD / 6u, KO_FOLD / 8u,
};
static const int32_t KO_ROT_SPD[2][3] = {{0, KO_HZ(0.8), KO_HZ(6.7)},    /* high rotor: OFF (stops), SLOW, FAST */
                                         {0, KO_HZ(0.7), KO_HZ(5.8)}};   /* low rotor */

static inline uint32_t ko_ratio(uint32_t k, uint32_t x)   /* the partial's increment from the note's, by shift and add */
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}

typedef struct {                 /* a part's, once a block (korgan_block) */
    int32_t g[KO_NP];            /* the bars' gains, Q15, normalised */
    int32_t pk, plev;            /* percussion: its partial (KO_NP: off), its level Q15 */
    uint32_t pdec;               /* its decay a block, Q16 */
    int32_t clk;                 /* click level, Q15 */
    int32_t dg, dcomp;           /* drive gain Q8, its make-up Q8 (0: no drive) */
    int32_t lev;                 /* LEVEL, Q14 */
    uint32_t rph[2], vph;        /* the rotors' phases (0 high, 1 low), the vibrato's */
    int32_t rsp[2];              /* the rotors' speeds */
    int32_t rwet;                /* the rotary's amount, Q15 (ramps on and off) */
    int32_t am[2], fm[2];        /* this block's amplitude (Q15) and pitch offset (Q16 of the increment), a rotor */
    int32_t vfm, chorus;         /* VIB's pitch offset (Q16), and 1: on the odd partials only (C1 .. C3) */
} ko_trk_t;

typedef struct {                 /* a voice's */
    uint32_t ph[KO_NP];
    int32_t gp[KO_NP];           /* each partial's gain at the end of the last block (the ramp's start) */
    int32_t perc, clk;           /* the percussion's and the click's envelopes, Q15 */
    int32_t nz, hp;              /* the click's noise and its high-pass */
    uint8_t gate, init;
} ko_vc_t;

static ko_trk_t ko_t[NPART];
static ko_vc_t ko_v[NPART][NVOICE];

static uint32_t ko_part(const track_t *t) { return (uint32_t)(t - trk) % NPART; }

static void korgan_block(track_t *t)
{
    ko_trk_t *T = &ko_t[ko_part(t)];
    const int16_t *p = t->p;
    static const int16_t VIBD[4] = {0, 229, 459, 918};   /* +-0.35 %, 0.7 %, 1.4 % of the pitch (Q16) */
    const uint32_t perc = (uint32_t)clamp(p[P_E2], 0, 2), rot = (uint32_t)clamp(p[P_E0], 0, 2), soft = p[P_E12] != 0;
    const int32_t vib = clamp(p[P_E1], 0, 6), vdep = VIBD[vib > 3 ? vib - 3 : vib];
    int32_t s = 0, lv[KO_NP], vfm;
    uint32_t k, norm;
    for (k = 0; k < KO_NP; k++) {
        lv[k] = perc && k == KO_NP - 1u ? 0 : KO_LV[clamp(p[P_E3 + k], 0, 8)];   /* PERC on: the 1' bar is off */
        s += lv[k];
    }
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);   /* the gains sum to 2.4 s / (s + 1.5), s in full bars */
    if (perc && !soft) norm = (norm * 23170u) >> 15;                   /* normal percussion: the bars -3 dB */
    for (k = 0; k < KO_NP; k++) T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(2u + perc) : KO_NP;                       /* 2ND: 4', 3RD: 2 2/3' */
    T->plev = soft ? 12000 : 24000;
    T->pdec = p[P_E13] ? FE_WHEEL_PDEC_SLOW : FE_WHEEL_PDEC_FAST;      /* tau 0.6 s / 0.15 s */
    T->clk = clamp(p[P_E14], 0, 127) * 180;
    if (p[P_E15] > 0) {                                                /* y = 6 / (2 + g) tanh(g x / 2) */
        T->dg = 256 + clamp(p[P_E15], 0, 127) * 6;
        T->dcomp = (6 * 256 * 256) / (512 + T->dg);
    } else T->dg = 0;
    T->lev = clamp(p[P_E16], 0, 127) * 129;
    for (k = 0; k < 2u; k++) {   /* the rotors: each speed eases to its target (high tau ~0.37 s, low ~1.5 s) */
        int32_t sp, c, sn, dep = k ? 7000 : 13000;                     /* AM depth: low 21 %, high 40 % */
        T->rsp[k] += (KO_ROT_SPD[k][rot] - T->rsp[k]) >> ((k ? 11 : 9) + FE_OCT);
        sp = T->rsp[k];
        T->rph[k] += (uint32_t)sp;
        sn = sine_i(T->rph[k]);
        c = sine_i(T->rph[k] + 0x40000000u);
        T->am[k] = 32767 - mulq15(dep, (32767 - c) >> 1);
        T->fm[k] = (sn * FE_T(sp >> (k ? 17 : 16))) >> 15;            /* Doppler: +-0.5 % fast (high), +-0.2 % (low) */
    }
    T->rwet += ((rot ? 32767 : 0) - T->rwet) >> (5 + FE_OCT);          /* ~25 ms in or out */
    T->vph += (uint32_t)KO_HZ(6.9);
    vfm = vdep ? (sine_i(T->vph) * vdep) >> 15 : 0;                    /* VIB: V / C 1 .. 3 */
    T->vfm = vfm;
    T->chorus = vib > 3;
    for (k = 0; k < 2u; k++) {
        T->am[k] = 32767 - mulq15(T->rwet, 32767 - T->am[k]);
        T->fm[k] = mulq15(T->fm[k], T->rwet);
    }
}

static void korgan_note_on(track_t *t, voice_t *v)
{
    uint32_t vi = (uint32_t)(v - t->v) % NVOICE, k, held = 0;
    ko_vc_t *V = &ko_v[ko_part(t)][vi];
    for (k = 0; k < NVOICE; k++)   /* single trigger: another key of the part held? */
        if (&t->v[k] != v && t->v[k].active && t->v[k].gate && t->v[k].stage != 4u) held = 1;
    if (!V->init) {                /* the wheels: spread start phases, then free running */
        for (k = 0; k < KO_NP; k++) V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

static void korgan_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const ko_trk_t *T = &ko_t[ko_part(t)];
    ko_vc_t *V = &ko_v[ko_part(t)][(uint32_t)(v - t->v) % NVOICE];
    const int32_t vs = clamp(t->p[P_E17], 0, 127);
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    int32_t acc[CTL], perc1, clk0, clk1;
    uint32_t i, k;
    if (n > CTL) n = CTL;
    for (i = 0; i < n; i++) acc[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < KO_NP; k++) {
        uint32_t x = m->inc, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r;
        if ((int32_t)k == T->pk) g1 += mulq15(T->plev, perc1);
        while (x > KO_FOLD_LIM[k]) x >>= 1;                            /* fold back above ~6 kHz */
        inc = ko_ratio(k, x);
        r = inc < KO_XOVER;                                            /* 1: the low rotor */
        g1 = mulq15(g1, T->am[r]);
        inc += (uint32_t)((int32_t)(inc >> 16) * (T->fm[r] + (!T->chorus || (k & 1u) ? T->vfm : 0)));   /* the rotor, VIB (C: odd partials) */
        V->gp[k] = g1;
        if (!g0 && !g1) { V->ph[k] = ph + inc * n; continue; }
        if (g0 == g1) {
            for (i = 0; i < n; i++) { ph += inc; acc[i] += (SINE[ph >> 22] * g1) >> 15; }
        } else {                                                       /* the gain ramps over the block (CTL = 32) */
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < n; i++) { ph += inc; gq += dg; acc[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15; }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    if (V->gate && !v->gate) { V->gate = 0; V->clk = 8192; }          /* the release's click, a quarter as loud (it was heard as crackle) */
    clk0 = mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * FE_WHEEL_CLKDEC) >> 15;
        clk1 = mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < n; i++) {
            int32_t z = (int32_t)(noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> (3 + FE_OCT);
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;
        }
        V->nz = nst;
        V->hp = hp;
    }
    if (T->dg) {
        const int32_t dg = T->dg, dcomp = T->dcomp;
        for (i = 0; i < n; i++) acc[i] = (softclip((acc[i] * dg) >> 9) * dcomp) >> 8;
    }
    for (i = 0; i < n; i++) {   /* acc: Q15 with headroom to 4.0 */
        int32_t s = ((acc[i] >> 1) * T->lev) >> 14;
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += shl(voice_amp(s, m, i), 1);
    }
}

static const engine_t ENG_KORGAN = {   /* (the host takes its parameters from Kreisel's table, src/engines_data.c) */
    .name = K_ENGINE_NAME_KOGN,
    .page_title = {"BARS", "TONE"},
    .note_on = korgan_note_on,
    .render = korgan_render,
    .block = korgan_block,
    .knob = {P_E5, P_E2, P_E15, P_E0},
};
