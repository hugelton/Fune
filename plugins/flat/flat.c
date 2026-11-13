/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FLAT, an example plugin that lays out its own screens (docs/PLUGIN_SDK.md: Screens): flat sliders only, a scope over SOUND,
 * its own drawing (the filter's curve) over EDIT. The same tables on every device; a desktop's large screen gets a layout of its
 * own (the scope beside the sliders). Id `flat`. Its parameters, on P_E0 .. :
 *   SOUND  0 WAVE (sine, tri, saw, square), 1 SHAPE (a fold, or the square's width), 2 DETUNE (a second oscillator), 3 SUB
 *   EDIT   4 CUT, 5 RESO, 6 DRIVE, 7 LEVEL
 * Integer; no division in the sample loop. */
#include "kreisel_sdk.h"

static void flat_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->ph[2] = 0;
    v->s[0] = v->s[1] = 0;
}

static int32_t flat_osc(int wave, uint32_t ph, uint32_t inc, int32_t shape)
{
    int32_t x;
    switch (wave) {
    case 1: x = osc_tri(ph); break;
    case 2: x = osc_saw(ph, inc); break;
    case 3: return osc_pulse(ph, inc, 0x80000000u - (uint32_t)shape * 0xE00000u);   /* SHAPE narrows the square */
    default: x = sine_i(ph); break;
    }
    if (shape) {   /* a fold: louder, then folded back */
        x += mulq15(x, shape * 300);
        while (x > 32767 || x < -32767) x = x > 32767 ? 65534 - x : -65534 - x;
    }
    return x;
}

static void flat_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const int wave = clamp(p[P_E0], 0, 3);
    const int32_t shape = clamp(p[P_E1], 0, 127), det = clamp(p[P_E2], 0, 127), sub = clamp(p[P_E3], 0, 127) * 200;
    const int32_t drive = 128 + clamp(p[P_E6], 0, 127) * 4, level = clamp(p[P_E7], 0, 127) * 258;
    const uint32_t inc2 = det ? cents_inc(m->pitch16, det * 30 / 127, m->fine) : m->inc, inc3 = m->inc >> 1;
    uint32_t ph1 = v->ph[0], ph2 = v->ph[1], ph3 = v->ph[2], i;
    int32_t ic1 = v->s[0], ic2 = v->s[1];
    tsvf_t f;
    tsvf_coef(&f, clamp((clamp(p[P_E4], 0, 127) << 8) + m->cutoff, 0, 127 << 8), clamp(p[P_E5], 0, 127));
    for (i = 0; i < n; i++) {
        int32_t s = flat_osc(wave, ph1, m->inc, shape);
        if (det) s = (s + flat_osc(wave, ph2, inc2, shape)) >> 1;
        if (sub) s += mulq15(ph3 < 0x80000000u ? 20000 : -20000, sub);
        s = tsvf_lp(&f, s >> 1, &ic1, &ic2);
        if (drive != 128) s = softclip((s * drive) >> 7);
        out[i] += voice_amp(mulq15(s, level), m, i);
        ph1 += m->inc; ph2 += inc2; ph3 += inc3;
    }
    v->ph[0] = ph1; v->ph[1] = ph2; v->ph[2] = ph3;
    v->s[0] = ic1; v->s[1] = ic2;
}

static const engine_t FLAT_DSP = {.name = "FLAT", .note_on = flat_note_on, .render = flat_render};

static const char *const WAVES[] = {"Sine", "Tri", "Saw", "Sqr", 0};
static const KEngine FLAT_DESC = {
    .name = "FLAT",
    .uid = K_UID('f', 'l', 'a', 't'),
    .page_title = {"", "", ""},
    .edit = {
        {"WAVE", KF_ENUM, 0, 3, 2, WAVES, 0},
        {"SHP", KF_PCT, 0, 127, 0, 0, 0},
        {"DTN", KF_PCT, 0, 127, 30, 0, 0},
        {"SUB", KF_PCT, 0, 127, 0, 0, 0},
        {"CUT", KF_PCT, 0, 127, 90, 0, 0},
        {"RES", KF_PCT, 0, 127, 30, 0, 0},
        {"DRV", KF_PCT, 0, 127, 0, 0, 0},
        {"LVL", KF_PCT, 0, 127, 110, 0, 0},
    },
    .macro = {0, 1, 4, 5},
    .nparams = 8,
    .page2 = 4,
    .nomod = K_NOMOD_SHAPE,
};

/* The screens: a grid of cells, the host's widgets on it. Every screen but a large one: the picture over the four sliders */
#define SL(k, x, y, w, h) {K_W_SLIDER, K_ST_FLAT, k, x, y, w, h, 0}
static const KWidget SOUND_W[] = {
    {K_W_SCOPE, K_ST_FLAT, -1, 0, 0, 4, 2, 0},
    SL(0, 0, 2, 1, 3), SL(1, 1, 2, 1, 3), SL(2, 2, 2, 1, 3), SL(3, 3, 2, 1, 3),
};
static const KWidget EDIT_W[] = {
    {K_W_PICTURE, K_ST_FLAT, 0, 0, 0, 4, 2, 0},   /* picture 0: the filter */
    SL(4, 0, 2, 1, 3), SL(5, 1, 2, 1, 3), SL(6, 2, 2, 1, 3), SL(7, 3, 2, 1, 3),
};
/* a large screen: the picture at the left, the sliders beside it */
static const KWidget SOUND_L[] = {
    {K_W_SCOPE, K_ST_FLAT | K_ST_FRAME, -1, 0, 0, 5, 4, 0},
    SL(0, 5, 0, 1, 4), SL(1, 6, 0, 1, 4), SL(2, 7, 0, 1, 4), SL(3, 8, 0, 1, 4),
};
static const KWidget EDIT_L[] = {
    {K_W_PICTURE, K_ST_FLAT | K_ST_FRAME, 0, 0, 0, 5, 4, 0},
    SL(4, 5, 0, 1, 4), SL(5, 6, 0, 1, 4), SL(6, 7, 0, 1, 4), SL(7, 8, 0, 1, 4),
};
static const KLayout LAYOUTS[] = {
    {K_SCR_LARGE, K_LPAGE_SOUND, 9, 4, 5, SOUND_L},
    {K_SCR_LARGE, K_LPAGE_EDIT, 9, 4, 5, EDIT_L},
    {K_SCR_ANY, K_LPAGE_SOUND, 4, 5, 5, SOUND_W},   /* (the first that matches is taken: these for the rest) */
    {K_SCR_ANY, K_LPAGE_EDIT, 4, 5, 5, EDIT_W},
};

static const KHostApi *host;
static void flat_attach(const KHostApi *h) { host = h; }

/* picture 0: the low-pass's curve from CUT and RESO (dB against the pitch, 0 dB at the middle line), the cutoff marked */
static void flat_draw(const KDraw *g, int id, const int16_t *val, int n)
{
    const int cut = n > 5 ? clamp(val[4], 0, 127) : 90, res = n > 5 ? clamp(val[5], 0, 127) : 30;
    const int col = g->lit ? K_COL_ACCENT : K_COL_MID, zero = g->h / 3;
    /* a square screen draws every other point (fewer calls; the host says which screen it is) */
    const int step = (host && host->screen == K_SCR_SQUARE) || g->screen == K_SCR_SQUARE ? 2 : 1;
    int x, px = -1, py = 0, cx;
    if (id != 0 || g->w < 8 || g->h < 8) return;
    g->line(g, 0, zero, g->w - 1, zero, K_COL_FAINT);
    for (x = 0; x < g->w; x += step) {
        const int f = x * 127 / (g->w - 1), d = f - cut;   /* the pitch, and how far above the cutoff (~12.7 an octave) */
        int db4 = 0, y;                                   /* dB x 4 */
        const int ad = d < 0 ? -d : d;
        if (ad < 10) db4 += res * 48 / 127 * (10 - ad) / 10;   /* the resonance's bump: up to +12 dB */
        if (d > 0) db4 -= d * 4 * 12 * 10 / 127;                /* 12 dB an octave above */
        y = zero - db4 * g->h / (4 * 48);
        if (y < 0) y = 0;
        if (y > g->h - 1) y = g->h - 1;
        if (px >= 0) g->line(g, px, py, x, y, col);
        px = x; py = y;
    }
    cx = cut * (g->w - 1) / 127;
    g->line(g, cx, zero + 2, cx, g->h - 1, K_COL_FAINT);
    g->text(g, 2, g->h - g->text_h, "LP 12", K_COL_MID);
}

static const KPluginExt FLAT_EXT = {sizeof(KPluginExt), K_PLUGIN_EXT_VERSION, flat_attach, LAYOUTS,
                                    (uint8_t)(sizeof LAYOUTS / sizeof LAYOUTS[0]), flat_draw};
static const KPlugin FLAT = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &FLAT_DESC, &FLAT_DSP,
                             FS, 0, 14, 0, 0, &FLAT_EXT};   /* ~14 us a voice a block (an estimate) */

KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &FLAT; }
