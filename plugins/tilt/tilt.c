/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* TILT, an example effect that lays out its own insert (docs/PLUGIN_SDK.md: Screens, K_LPAGE_FX): a tilt EQ (the lows down as
 * the highs go up, about 700 Hz the pivot), a drive, a sample hold (the rate down by 1 .. 16) and a level. Id `tilt`.
 * Its parameters (an insert keeps them 0 .. 100; set_param gets 0 .. 32767):
 *   0 TILT (- lows, + highs), 1 DRV, 2 HOLD, 3 LVL; the insert's MIX is the host's (K_WP_MIX in a layout)
 * Its table: the knobs' row together (the FM-1's K1 .. K4 take TILT DRV HOLD MIX), its own picture (the tilt's curve) beside LVL.
 * Integer; no division in the sample loop; its state is all it keeps (no delay memory). */
#include "kreisel_sdk.h"
#include "kreisel_effect.h"

typedef struct {
    int32_t lp;          /* the one-pole low part */
    int32_t lo_g, hi_g;  /* the tilt's gains, Q15 (16383 .. 49151: -6 .. +3.5 dB) */
    int32_t drive;       /* Q12: 4096 .. 4096 * 8 */
    int32_t level;       /* Q15 */
    int32_t held;        /* the sample held */
    uint16_t hold, left; /* samples a value is held; how many are left */
} Tilt;

static void tilt_set(void *st, unsigned id, int32_t v)
{
    Tilt *s = (Tilt *)st;
    v = v < 0 ? 0 : v > 32767 ? 32767 : v;
    switch (id) {
    case 0: { const int32_t t = v - 16384; s->lo_g = 32767 - t; s->hi_g = 32767 + t; break; }
    case 1: s->drive = 4096 + v; break;   /* 1 x .. 9 x */
    case 2: s->hold = (uint16_t)(1 + (v * 15 >> 15)); break;
    case 3: s->level = v; break;
    default: break;
    }
}
static void tilt_reset(void *st)
{
    Tilt *s = (Tilt *)st;
    s->lp = 0; s->held = 0; s->left = 0;
}
static void tilt_init(void *st, void *mem, uint32_t fs)
{
    Tilt *s = (Tilt *)st;
    (void)mem; (void)fs;
    tilt_reset(st);
    tilt_set(st, 0, 16384); tilt_set(st, 1, 0); tilt_set(st, 2, 0); tilt_set(st, 3, 26214);
    s->left = 0;
}
static void tilt_process(void *st, const int32_t *in, int32_t *out, unsigned n)
{
    Tilt *s = (Tilt *)st;
    int32_t lp = s->lp, held = s->held;
    unsigned i, left = s->left;
    for (i = 0; i < n; i++) {
        int32_t x = in[i], y;
        lp += (x - lp) * 3000 >> 15;                                   /* (about 700 Hz at 48 kHz) */
        y = (int32_t)(((int64_t)lp * s->lo_g + (int64_t)(x - lp) * s->hi_g) >> 15);
        if (s->drive > 4096) y = softclip((int32_t)((int64_t)y * s->drive >> 12));
        if (!left) { held = y; left = s->hold; }
        left--;
        out[i] = (int32_t)((int64_t)held * s->level >> 15);
    }
    s->lp = lp; s->held = held; s->left = (uint16_t)left;
}
static size_t tilt_mem(uint32_t fs) { (void)fs; return 0; }

static const char *const NAMES[] = {"TILT", "DRV", "HOLD", "LVL"};
static const KEffectDSP TILT_DSP = {"TILT", sizeof(Tilt), tilt_mem, 4, NAMES, tilt_init, tilt_process, tilt_reset, tilt_set};

static const KEngine TILT_DESC = {
    .name = "TILT",
    .uid = K_UID('t', 'i', 'l', 't'),
    .page_title = {"", "", ""},
    .edit = {
        {"TILT", KF_CENTER, 0, 100, 50, 0, 0},
        {"DRV", KF_PCT, 0, 100, 0, 0, 0},
        {"HOLD", KF_PCT, 0, 100, 0, 0, 0},
        {"LVL", KF_PCT, 0, 100, 80, 0, 0},
    },
    .macro = {0, 1, 2, 3},
    .nparams = 4,
    .page2 = 4,
};

/* The insert's tables. The first row: TILT DRV HOLD MIX, as the knobs take them; then the picture and LVL */
#define KN(k, x, y, w, h) {K_W_KNOB, 0, k, x, y, w, h, 0}
static const KWidget SQUARE_W[] = {   /* the FM-1's insert: a box beside the effect's name; the knobs' row tall, then the picture */
    KN(0, 0, 0, 1, 2), KN(1, 1, 0, 1, 2), KN(2, 2, 0, 1, 2), KN(K_WP_MIX, 3, 0, 1, 2),
    {K_W_PICTURE, K_ST_FLAT, 0, 0, 2, 3, 1, 0}, {K_W_VALUE, 0, 3, 3, 2, 1, 1, 0},
};
static const KWidget SMALL_W[] = {    /* a wide, short screen: one row, flat sliders lying under their labels */
    {K_W_PICTURE, K_ST_FLAT, 0, 0, 0, 2, 1, 0},
    {K_W_SLIDER, K_ST_FLAT, 0, 2, 0, 1, 1, 0}, {K_W_SLIDER, K_ST_FLAT, 1, 3, 0, 1, 1, 0}, {K_W_SLIDER, K_ST_FLAT, 2, 4, 0, 1, 1, 0},
    {K_W_SLIDER, K_ST_FLAT, K_WP_MIX, 5, 0, 1, 1, 0}, {K_W_SLIDER, K_ST_FLAT, 3, 6, 0, 1, 1, 0},
};
static const KWidget LARGE_W[] = {    /* a rack's unit: the picture over the knobs */
    {K_W_PICTURE, K_ST_FLAT | K_ST_FRAME, 0, 0, 0, 5, 2, 0},
    KN(0, 0, 2, 1, 2), KN(1, 1, 2, 1, 2), KN(2, 2, 2, 1, 2), KN(3, 3, 2, 1, 2), KN(K_WP_MIX, 4, 2, 1, 2),
};
static const KLayout LAYOUTS[] = {
    {K_SCR_SQUARE, K_LPAGE_FX, 4, 3, 6, SQUARE_W},
    {K_SCR_SMALL, K_LPAGE_FX, 7, 1, 6, SMALL_W},
    {K_SCR_LARGE, K_LPAGE_FX, 5, 4, 6, LARGE_W},
};

/* picture 0: the tilt's curve, lows flat at the left, highs flat at the right, 0 dB the middle line */
static void tilt_draw(const KDraw *g, int id, const int16_t *val, int n)
{
    const int t = n > 0 ? (val[0] < 0 ? 0 : val[0] > 100 ? 100 : val[0]) - 50 : 0;   /* -50 .. 50 */
    const int mid = g->h / 2, amp = g->h / 2 - 2, col = g->lit ? K_COL_ACCENT : K_COL_MID;
    const int yl = mid + t * amp / 50, yr = mid - t * amp / 50, x1 = g->w / 3, x2 = g->w - 1 - g->w / 3;
    if (id != 0 || g->w < 8 || g->h < 6) return;
    g->line(g, 0, mid, g->w - 1, mid, K_COL_FAINT);
    g->line(g, 0, yl, x1, yl, col);
    g->line(g, x1, yl, x2, yr, col);
    g->line(g, x2, yr, g->w - 1, yr, col);
}

static const KPluginExt TILT_EXT = {sizeof(KPluginExt), K_PLUGIN_EXT_VERSION, 0, LAYOUTS,
                                    (uint8_t)(sizeof LAYOUTS / sizeof LAYOUTS[0]), tilt_draw};
static const KPlugin TILT = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_EFFECT, &TILT_DESC, &TILT_DSP,
                             0, sizeof(Tilt), 6, 0, 0, &TILT_EXT};   /* any rate; ~6 us a block (an estimate) */

KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &TILT; }
