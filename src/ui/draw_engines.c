/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The engines' own views on the 240 x 240 screen (ORGAN, DRUM, TRIO, PHASE, ENSEMBLE, FM2) and the envelopes and
 * the LFO as pictures. */
#include "draw_int.h"
int synth_view_korgan(KApp *a)
{
    const int row = a->win / 4, wx = whole.x;
    int k;
    if (k_track_missing(a, a->track)) return 0;
#ifndef KREISEL_BITMAP_FONT
    if (cv.w == 320 && cv.h == 180) return synth_view_korgan_wide(a);
#endif
    if (!sk_geom()) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    static const char *const ROT[3] = {"0", "S", "F"}, *const PRC[3] = {"0", "2", "3"};   /* off, slow, fast; off, 2nd, 3rd */
    {   /* K2 ROTR, K3 VIB, K4 PERC, left to right, with the same room at the sides and between them; VIB / CHO's box in the middle */
        const int gap = (whole.w - 52 - 52 - 52) / 4;
        sk_switch_h(a, K_PAGE_SOUND, 0, wx + gap, SK_LY(0), 52, "Rotary", ROT, 1);
        ko_vibcho(a, 1, wx + (whole.w - 52) / 2, SK_LY(0), 1);
        sk_switch_h(a, K_PAGE_SOUND, 2, wx + whole.w - gap - 52, SK_LY(0), 52, "Perc", PRC, 1);
    }
    {   /* the bars sit in a dark well, not on the wood */
        const int y0 = SK_LY(1) + 3, y1 = whole.y + whole.h - 4;   /* (the FM-1's: 104) */
        g_rrect_frame(&cv, wx + 7, y0, whole.w - 14, y1 - y0, 5, 0x0B0806, KO_WELL);
        g_hline_a(&cv, wx + 10, y0 + 1, whole.w - 20, 0x000000, 120);   /* its shadow under the wood's edge */
    }
    for (k = 0; k < 9; k++) {   /* the bars, 24 apart, evenly, the nine centred */
        int i = 3 + k, cx = wx + (whole.w - 8 * 24) / 2 + k * 24;
        ko_drawbar(a, i, cx, SK_LY(1) + 6, 1);
    }
    return 1;
}
/* ORGAN's EDIT: PVOL PDCY CLICK DRV | LEVEL VEL, the two rows in the middle of the panel (no screen: the bars are on SOUND) */
int edit_view_korgan(KApp *a)
{
    static const char *const L[6] = {"PercVol", "PercDcy", "Click", "Drive", "Level", "Vel"};
        int i;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    for (i = 0; i < 6; i++)
        sk_control(a, K_PAGE_EDIT, i, SK_CX(i & 3), SK_LY(i < 4 ? 0 : 1) + sg_mid, SK_KY(i < 4 ? 0 : 1) + sg_mid, L[i], 1);   /* (the FM-1's: 65 / 117) */
    return 1;
}
/* DRUM's SOUND, in ANALOG's black and white (no wood): the lane in hand. The engine's bar; its PIECE on a little screen (the
 * piece large, the lane's number), TUNE, TONE (named by what the piece's TONE moves); then DCAY, LVL, and the eight lanes as pads
 * (the lane in hand outlined, a lane lit on its step while playing) */
static const char *drum_tone_name(const char *piece)   /* what TONE moves (src/drum_kreisel.c: each piece's TK_*) */
{
    static const struct { const char *p, *n; } T[] = {
        {"T8 BD", "Tone"}, {"T9 BD", "Click"}, {"K5 BD", "Click"}, {"T8 SD", "Snappy"}, {"T9 SD", "Snappy"}, {"K7 SD", "Snappy"},
        {"T8 TOM", "Noise"}, {"T9 TOM", "Stick"}, {"K7 TOM", "Sweep"}, {"T8 CNG", "Tone"}, {"T8 CP", "Band"}, {"K7 CP", "Band"},
        {"T8 CH", "Metal"}, {"T8 OH", "Metal"}, {"K5 CH", "Metal"}, {"K5 OH", "Metal"}, {"K7 CH", "Metal"}, {"K7 OH", "Metal"},
        {"K5 CY", "Metal"}, {"K7 CY", "Metal"}};
    unsigned i;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(T[i].p, piece)) return T[i].n;
    return 0;   /* (TONE does nothing for it) */
}
int drum_view_sel(KApp *a)
{
    const int row = a->win / 4, t = a->track, ln = a->cur_lane[t];
    const KEngine *e = k_engine_at(a->sound[t]);
    const char *piece = e->edit[0].names[k_param_clamp(&e->edit[0], a->lane_p[t][ln][0])], *tn = drum_tone_name(piece);
    int k;
    if (!sk_geom() || k_track_missing(a, t)) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    {   /* row 1: the lane in hand (orange) over its PIECE in a box; LEVEL, PAN, TUNE */
        const int x = SK_WX(0), y = SK_LY(0) + sg_wd + 1, w = SK_WW, h = sg_lane_h, hot = hot_param(a, 0) || touched_recent(a, 0);   /* (the FM-1's: 9, 66, 48, 26) */
        char lane[12];
        snprintf(lane, sizeof lane, "LANE %d", ln + 1);
        g_text_box(&cv, x, SK_LY(0), w, g_text_h(GF_S), GF_S, lane, SK_ARC, 0);
        g_rrect_frame(&cv, x, y, w, h, 3, hot ? SK_ARC : sk.lcd_edge, sk.lcd);
        g_text_box(&cv, x, y, w, h, GF_S, piece, SK_LCD_WAVE, 0);
        k_set_bounds(a, K_NODE_PARAM, 0, (KRect){x, SK_LY(0), w, y + h - SK_LY(0)});
    }
    {   /* LVL PAN TUNE | TONE DCAY CLIK ATK | PDEP PTIM, four a row as the knobs take them */
        static const char *const L[9] = {"Level", "Pan", "Tune", 0, "Decay", "Click", "Attack", "P.Depth", "P.Time"};
        int c;
        for (c = 0; c < 9; c++) {
            const int i = 1 + c, col = i & 3, rw = i / 4;
            sk_control(a, K_PAGE_SOUND, i, SK_CX(col), SK_LY(rw), SK_KY(rw),
                       c == 3 ? (tn ? tn : "Tone") : L[c], c != 3 || tn != 0);
        }
    }
    (void)k;
    return 1;
}
/* DRUM's EDIT: the kit. Its eight lanes on the screen (each its piece), then the kit's TUNE DCAY CRSH HUMN */
int drum_view_edit(KApp *a)
{
    static const char *const L[8] = {"Tune", "Decay", "Crush", "Human", 0, 0, 0, 0};
    const int row = a->win / 4, t = a->track; (void)row;   /* (the FM-1 build has no use for it) */
    const KEngine *e = k_engine_at(a->sound[t]);
    KRect r;
    int k;
    if (!sk_geom() || k_track_missing(a, t)) return 0;
    sk_frame(a);
    r = sk_screen("KIT", 0, 1);
    for (k = 0; k < K_LANES; k++) {   /* two columns of four: the lane, its piece */
        const int x = sg_fm1 ? r.x + 8 + (k / 4) * (r.w / 2) : r.x + 6 + (k % 4) * ((r.w - 6) / 4), y = sg_fm1 ? r.y + 17 + (k % 4) * 13 : r.y + 17 + (k / 4) * 14, mine = k == a->cur_lane[t];   /* (the Zero: four across, two down) */
        char b[16];
        snprintf(b, sizeof b, "%d %s", k + 1, e->edit[0].names[k_param_clamp(&e->edit[0], a->lane_p[t][k][0])]);
        g_text(&cv, x, y, GF_S, b, mine ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 70));
    }
    sk_rows(a, K_PAGE_EDIT, L, 4);
    return 1;
}
/* a drum track's ENV: the lane in hand's attack, pitch sweep (depth and time) and click. The screen draws the hit: its level (the
 * attack, then the fall) and its pitch falling to the note over it */
int drum_env_view(KApp *a)
{
    static const char *const L[8] = {"Attack", "P.Depth", "P.Time", "Click", 0, 0, 0, 0};
    const int t = a->track, ln = a->cur_lane[t], lit = 1;
    const KVal *E = &a->lane_p[t][ln][K_LANE_ENV];
    const KEngine *e = k_engine_at(a->sound[t]);
    char tag[16];
    KRect r;
    int q;
    if (cv.w != 240 || cv.h != 240 || !drum_track(a)) return 0;
    sk_frame(a);
    snprintf(tag, sizeof tag, "LANE %d", ln + 1);
    r = sk_screen(tag, e->edit[0].names[k_param_clamp(&e->edit[0], a->lane_p[t][ln][0])], lit);
    {   /* (in integers, Q16: the attack at = .12 E0 / 100, then e^(-4 (t - at)); the pitch .2 + .7 pd / 2 e^(-t / pt)) */
        int p16[2 * 61];
        const int at = E[0] * 786432 / 10000, pt = 3277 + E[2] * 196608 / 1000, yb = (r.y + r.h - 6) * 16, hh = r.h - 22;
        for (q = 0; q <= 60; q++) {   /* the level */
            const int tt = q * 65536 / 60, z = tt > at ? tt - at : 0, lin = at > 0 && tt < at ? tt * 65536 / at : 65536;
            const int ex = (int)g_exp2(-k_mulsh(z * 4 << 8, LOG2E_Q30, 30), 16), y = lin >= 65536 ? ex : (int)((uint32_t)lin * (uint32_t)ex >> 16);
            p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 60;
            p16[2 * q + 1] = (yb * 65536 - y * hh * 16) >> 16;
        }
        sk_line16(p16, 61, lit);
        for (q = 0; q <= 60; q++) {   /* the pitch, falling to the note (dim) */
            const int tt = q * 65536 / 60, z = ((tt << 14) / pt) << 2;
            const int y = 13107 + E[1] * (int)g_exp2(-k_mulsh(z << 8, LOG2E_Q30, 30), 16) * 7 / 1000;
            p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 60;
            p16[2 * q + 1] = (yb * 65536 - y * hh * 16) >> 16;
        }
        g_curve16(&cv, p16, 61, 16, k_mix(sk.lcd, SK_LCD_WAVE, 62));   /* (1 px) */
    }
    if (E[3]) g_fill(&cv, r.x + 8, r.y + 16, 2, (r.h - 22) * E[3] / 100, SK_LCD_WAVE);   /* the click: a tick at the strike */
    sk_rows(a, K_PAGE_ENV1, L, 4);
    return 1;
}
/* TRIO's SOUND (240 x 240): the engine's bar, then INT2 INT3 DETUNE MOD | each oscillator's wave on a little screen,
 * and PW | each one's level: a column an oscillator in the last two rows */
int synth_view_ktrio(KApp *a)
{
    static const char *const L1[4] = {"Int 2", "Int 3", "Detune", "Mod"}, *const W[3] = {"W1", "W2", "W3"}, *const L3[3] = {"Level 1", "Level 2", "Level 3"};
    const int row = a->win / 4;
    int c;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    for (c = 0; c < 4; c++) sk_control(a, K_PAGE_SOUND, c, SK_CX(c), SK_LY(0), SK_KY(0), L1[c], 1);
    for (c = 0; c < 3; c++) sk_wave_w(a, 4 + c, SK_WX(c), SK_LY(1), SK_LY(1) + sg_wd, SK_WW, W[c], 1, 1, 0);
    sk_control(a, K_PAGE_SOUND, 7, SK_CX(3), SK_LY(1), SK_KY(1), "PW", 1);
    for (c = 0; c < 3; c++) sk_control(a, K_PAGE_SOUND, 8 + c, SK_CX(c), SK_LY(2), SK_KY(2), L3[c], 1);
    if (sk.rainbow) {   /* the mark: five bands standing in the empty corner (the last row's fourth place), their tops level (not stepped) */
        static const uint32_t P[5] = {0xE0403C, 0xF08A24, 0xE8C21E, 0x3EA84A, 0x2F7FD0};
        const int bw = 5, rx = SK_CX(3) - (5 * bw) / 2, rb = SK_LY(2) + (sg_fm1 ? 43 : sg_rp - 4), rh = sg_fm1 ? 34 : sg_rp - 12;   /* (the FM-1's: 196, 34) */
        int k2;
        for (k2 = 0; k2 < 5; k2++) {
            const int top = rb - rh;
            g_fill(&cv, rx + k2 * bw, top, bw, rb - top, P[k2]);
        }
    }
    return 1;
}
/* PHASE's SOUND (240 x 240), as its two lines: the engine's bar, then PITCH GLIDE DETUNE LINE | line 1: its wave on a little
 * screen (as its DCW opens it), its DCO, DCW and DCA amounts | line 2 alike */
int synth_view_kphase(KApp *a)
{
    static const char *const L1[4] = {"Pitch", "Glide", "Detune", "Line"}, *const LL[3] = {"DCO", "DCW", "DCA"};
    const int row = a->win / 4;
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    int c, l;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    for (c = 0; c < 4; c++) sk_control(a, K_PAGE_SOUND, c, SK_CX(c), SK_LY(0), SK_KY(0), L1[c], 1);
    for (l = 0; l < 2; l++) {   /* a row a line: WAVE, DCO, DCW, DCA */
        const int base = 4 + 4 * l, ly = SK_LY(1 + l);
        sk_wave_w(a, base, SK_WX(0), ly, ly + sg_wd, SK_WW, l ? "L2" : "L1", 1, 2, P[base + 2]);
        for (c = 0; c < 3; c++) sk_control(a, K_PAGE_SOUND, base + 1 + c, SK_CX(c + 1), ly, ly + sg_kd, LL[c], 1);
    }
    return 1;
}
/* PHASE's EDIT: line 1's wave on the screen as its DCW opens it (three traces: shut, half way, at the top), then SUB FOLD NOISE
 * DRIVE | KEYTRK LEVEL VEL */
int edit_view_kphase(KApp *a)
{
    static const char *const L[8] = {"Sub", "Fold", "Noise", "Drive", "KeyTrk", "Level", "Vel", ""};
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    char rd[16];
    const int dv = P[6] < 0 ? 0 : P[6] > 127 ? 127 : P[6];
    KRect r;
    int q, k;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    snprintf(rd, sizeof rd, "%d%%", (200 * dv + 127) / 254);   /* (its DCW in %, rounded) */
    r = sk_screen("DCW 1", rd, 1);
    for (k = 0; k < 3; k++) {   /* (in integers) */
        int p16[2 * 121];
        const uint32_t d16 = (uint32_t)dv * 65536u / 127u, d = k == 2 ? d16 : d16 * (uint32_t)k / 3u;
        for (q = 0; q <= 120; q++) {
            const int y = sk_wave_i(2, P[4], (uint32_t)q * 65536u / 60u, d);   /* (two cycles) */
            p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 120;
            p16[2 * q + 1] = ((r.y + r.h / 2 + 7) * 16 * 65536 - y * (r.h / 2 - 14) * 16) >> 16;
        }
        g_curve16(&cv, p16, 121, 17, k_mix(sk.lcd, SK_LCD_WAVE, k == 2 ? 100 : 35 + k * 20));
    }
    sk_rows(a, K_PAGE_EDIT, L, 7);
    return 1;
}
/* ENSEMBLE: the same 240-square control language as PHASE, with a pale
 * string-machine panel. The SOUND page has three rows of registrations and
 * chorus controls; EDIT shows the chorus motion and tone-shaping controls. */
int synth_view_kensemble(KApp *a)
{
    static const char *const L[11] = {
        "Violin", "Brass", "Tone", "Ensemble",
        "Rate", "Depth", "Detune", "Level"
    };
    const int row = a->win / 4, wx = whole.x, wy = whole.y;
    int c;
    if (cv.w != 240 || cv.h != 240 || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    sk_control(a, K_PAGE_SOUND, 0, wx + 38, wy + 49, wy + 76, "Bass", 1);
    sk_control(a, K_PAGE_SOUND, 1, wx + 112, wy + 49, wy + 76, "Cello", 1);
    sk_control(a, K_PAGE_SOUND, 2, wx + 186, wy + 49, wy + 76, "Viola", 1);
    for (c = 0; c < 8; c++)
        sk_control(a, K_PAGE_SOUND, 3 + c,
                   wx + 33 + 56 * (c & 3),
                   wy + (c < 4 ? 101 : 153),
                   wy + (c < 4 ? 128 : 180),
                   L[c], 1);
    return 1;
}
int edit_view_kensemble(KApp *a)
{
    static const char *const L[8] = {"Highpass", "Drive", "Noise", "Env", "Velocity", "", "", ""};
    const int row = a->win / 4; (void)row;   /* (the FM-1 build has no use for it) */
    const int depth = a->params[a->track][K_PAGE_SOUND][4 + 8];
    KRect r;
    char val[16];
    int k, q;
    if (cv.w != 240 || cv.h != 240 || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    snprintf(val, sizeof val, "%d%%", depth * 100 / 127);
    r = sk_screen("ENSEMBLE", val, 1);
    for (k = 0; k < 3; k++) {   /* (in integers) */
        int p16[2 * 97];
        const int yc = (r.y + r.h / 2 + 6) * 16 << 14, amp = (r.h / 2 - 16) * 16;
        for (q = 0; q <= 96; q++) {
            const int y = g_isin((uint32_t)q * 44739243u + (uint32_t)k * 1431655765u);   /* a cycle over the 96, a third apart */
            p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 96;
            p16[2 * q + 1] = (yc - y * depth * amp / 127) >> 14;
        }
        g_curve16(&cv, p16, 97, 17, k_mix(sk.lcd, SK_LCD_WAVE, 95 - 23 * k));
    }
    sk_rows(a, K_PAGE_EDIT, L, 5);
    return 1;
}
/* FM2 uses PHASE's host-owned panel controls. The custom view is limited
 * to 240x240; other sizes use the same generic descriptor-driven UI. */
static void kfm2_scope(const KApp *a, KRect r, int lit)
{
    const KVal *p = a->params[a->track][K_PAGE_SOUND] + 4;
    const int ratio = p[2] < 0 ? 0 : p[2] > 7 ? 7 : p[2];   /* (RATIO, INDEX: the engine's 2 and 4) */
    static const uint8_t RATIO2[8] = {1, 2, 4, 6, 8, 12, 16, 32};   /* (twice the ratio) */
    const int ix = p[4] < 0 ? 0 : p[4] > 127 ? 127 : p[4];
    int p16[2 * 97], q;
    for (q = 0; q <= 96; q++) {   /* two cycles: sin(2 pi t + index 2 pi sin(2 pi t ratio)), t = 2 q / 96, index = 1.5 ix / 127 */
        const int m = g_isin((uint32_t)(q * RATIO2[ratio]) * 44739243u);
        const uint32_t ph = (uint32_t)q * 89478485u + (uint32_t)(k_smul(ix * m, 792624) >> 8);   /* (the index's turns: 1.5 2^18 / 127, Q8) */
        p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 96;
        p16[2 * q + 1] = (((r.y + r.h / 2) * 16 << 14) - g_isin(ph) * (r.h / 2 - 5) * 16) >> 14;
    }
    g_curve16(&cv, p16, 97, 17, lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62));
}
/* FM2's SOUND (240 x 240): the engine's bar, then PITCH TUNE RATIO GLIDE | INDEX ENV FEEDBACK KEYTRK, the wave on a screen under them */
int synth_view_kfm2(KApp *a)
{
    static const char *const L[8] = {"Pitch", "Tune", "Ratio", "Glide", "Index", "Env", "Feedback", "KeyTrk"};
    const int row = a->win / 4, wx = whole.x, wy = whole.y;
    KRect r;
    int i;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    sk_engine_bar(a, row);
    for (i = 0; i < 8; i++) sk_control(a, K_PAGE_SOUND, i, SK_CX(i & 3), SK_LY(i / 4), SK_KY(i / 4), L[i], 1);
    r = (KRect){wx + 11, SK_LY(2) + 4, whole.w - 23, sg_fm1 ? 46 : wy + whole.h - 3 - (SK_LY(2) + 4)};   /* (the FM-1's: 157, 46 tall) */
    g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 3, sk.lcd_edge, sk.lcd);
    kfm2_scope(a, r, 1);
    g_text(&cv, r.x + 5, r.y + 2, GF_S, "OP2 > OP1", SK_LCD_WAVE);
    return 1;
}
/* FM2's EDIT: the wave on the screen, then MIX DRIVE LEVEL VEL */
int edit_view_kfm2(KApp *a)
{
    static const char *const L[4] = {"Mix", "Drive", "Level", "Vel"};
    KRect r;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    r = sk_screen(K_ENGINE_NAME_KFM2, "OP2 > OP1", 1);
    kfm2_scope(a, r, 1);
    sk_rows(a, K_PAGE_EDIT, L, 4);
    return 1;
}
/* ENV 1's line in a screen: A D S R, a square at each corner */
void env1_curve(const KVal *E, KRect r, int lit)
{
    {   /* (in integers: the times in hundredths, A D 0.35 R, each + 0.02 but the hold) */
        const int A = E[0] + 2, D = E[1] + 2, R = E[3] + 2, T = A + D + 35 + R, x0 = r.x + 8, w = r.w - 16, yb = r.y + r.h - 6, h = r.h - 22;
        const int X[5] = {0, A, A + D, A + D + 35, T}, Y[5] = {0, 100, E[2], E[2], 0};
        int p16[10], q;
        for (q = 0; q < 5; q++) { p16[2 * q] = x0 * 16 + X[q] * w * 16 / T; p16[2 * q + 1] = yb * 16 - (Y[q] * h * 16 + 99) / 100; }
        sk_line16(p16, 5, lit);
        for (q = 1; q <= 3; q++) g_fill(&cv, lrdiv(x0 * T + X[q] * w, T) - 2, lrdiv(yb * 100 - Y[q] * h, 100) - 2, 4, 4, lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62));   /* (a square at each corner) */
        return;
    }
}
/* ENV 2's: a delay, then held up, then a fall along its curve */
static void env2_curve(const KVal *E, KRect r, int lit)
{
    {   /* (in integers: the times in 1/200, a delay and a hold of .05 + E / 200 each, the fall .7; its curve t^(.5 + 2 cr)) */
        const int DL = E[0] + 10, HD = E[1] + 10, T = DL + HD + 140, x0 = r.x + 8, w = r.w - 16, yb = r.y + r.h - 6, h = r.h - 22, ex = 50 + 2 * E[2];
        int p16[2 * 24], q;
        p16[0] = x0 * 16; p16[1] = yb * 16;
        p16[2] = x0 * 16 + DL * w * 16 / T; p16[3] = yb * 16;
        p16[4] = p16[2]; p16[5] = (yb - h) * 16;
        p16[6] = x0 * 16 + (DL + HD) * w * 16 / T; p16[7] = (yb - h) * 16;
        for (q = 1; q <= 20; q++) {   /* the fall: its curve bends it */
            const uint32_t pw = g_exp2(g_log2((uint32_t)q * 65536u / 20u) / 100 * ex, 16);
            p16[2 * (3 + q)] = x0 * 16 + ((DL + HD) * 20 + 140 * q) * w * 16 / (20 * T);
            p16[2 * (3 + q) + 1] = ((yb - h) * 16 * 65536 + h * 16 * (int)pw) >> 16;
        }
        sk_line16(p16, 24, lit);
        {   const uint32_t cl = lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62);   /* (a square at each corner) */
            g_fill(&cv, lrdiv(x0 * T + DL * w, T) - 2, yb - 2, 4, 4, cl);
            g_fill(&cv, lrdiv(x0 * T + DL * w, T) - 2, yb - h - 2, 4, 4, cl);
            g_fill(&cv, lrdiv(x0 * T + (DL + HD) * w, T) - 2, yb - h - 2, 4, 4, cl); }
        return;
    }
}
/* ENV 1: its line on the screen; then A D S R, then where it goes (PIT FLT SHP) */
/* PHASE's envelopes: DCO (pitch), DCW (wave), DCA (level), each a graph of its A D S R and no knobs drawn: SELECT
 * picks the envelope (a row), K1 .. K4 turn its A D S R. Three rows on the 240 x 240 panel, three side by side on a wide one. */
int phase_eg_view(KApp *a)
{
    static const char *const T[3] = {"DCO", "DCW", "DCA"}, *const W[3] = {"PITCH", "WAVE", "LEVEL"};
    const KVal *E[3] = {a->params[a->track][K_PAGE_ENV2] + 4, a->params[a->track][K_PAGE_ENV1], a->params[a->track][K_PAGE_ENV2]};
    const int wide = cv.w >= 300 && cv.h <= 200, row = a->win / 4;
    int g, k;
    if (!phase_env_track(a) || a->page != K_PAGE_ENV1) return 0;
    sk_frame(a);
    for (g = 0; g < 3; g++) {
        KRect r;
        char v[32];
        const int on = g == row;
        const uint32_t amber = on ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62);
        if (wide) { const int w = (whole.w - 22 - 8) / 3; r = (KRect){whole.x + 11 + g * (w + 4), whole.y + 34, w, whole.h - 44}; }
        else { const int h = (whole.h - 36) / 3; r = (KRect){whole.x + 11, whole.y + 30 + g * (h + 2), whole.w - 22, h - 2}; }
        g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 3, sk.lcd_edge, sk.lcd);
        if (on && !(a->lay & K_LAY_SYNTH_ALL)) { const int lw = g_text_w(GF_S, T[g]); sk_pill(r.x + 2, r.y + 3, lw + 8, lw, lw + 8, T[g]); g_text(&cv, r.x + 6, r.y + 2, GF_S, T[g], sk_row_text()); }   /* the envelope in hand */
        else g_text(&cv, r.x + 6, r.y + 3, GF_S, T[g], amber);
        if (!wide) g_text(&cv, r.x + 6 + g_text_w(GF_S, T[g]) + 6, r.y + 3, GF_S, W[g], k_mix(sk.lcd, SK_LCD_WAVE, on ? 70 : 40));
        snprintf(v, sizeof v, "%d %d %d %d", E[g][0], E[g][1], E[g][2], E[g][3]);
        if (g_text_w(GF_S, T[g]) + g_text_w(GF_S, v) + 22 <= r.w) {
            g_text(&cv, r.x + r.w - 6 - g_text_w(GF_S, v), r.y + 3, GF_S, v, amber);
            env1_curve(E[g], (KRect){r.x, r.y + 4, r.w, r.h - 4}, on);
        } else {   /* (a narrow box, the Zero's three side by side: the values under the curve, not over the name) */
            g_text(&cv, r.x + (r.w - g_text_w(GF_S, v)) / 2, r.y + r.h - 14, GF_S, v, amber);
            env1_curve(E[g], (KRect){r.x, r.y + 4, r.w, r.h - 18}, on);
        }
        for (k = 0; k < 4; k++) k_set_bounds(a, K_NODE_PARAM, g * 4 + k, r);   /* (its four values: the graph) */
    }
    sk_row_frame(a);
    return 1;
}
int env1_view(KApp *a)
{
    static const char *const L[8] = {"Attack", "Decay", "Sustain", "Release", "Pitch", "Filter", "Shape", ""};
    const int lit = a->win / 4 == 0;
    if (cv.w != 240 || cv.h != 240) return 0;
    sk_frame(a);
    if (ui_mark) ui_mark(11);
    if (phase_env_track(a)) {
        static const char *const P[8] = {"Attack", "Decay", "Sustain", "Release", "Depth", 0, 0, 0};
        env1_curve(a->params[a->track][K_PAGE_ENV1], sk_screen("DCW", "WAVE SHAPE", lit), lit);
        if (ui_mark) ui_mark(12);
        sk_rows(a, K_PAGE_ENV1, P, 5);
    } else {
        env1_curve(a->params[a->track][K_PAGE_ENV1], sk_screen("ENV 1", "AMP + MOD", lit), lit);
        if (ui_mark) ui_mark(12);
        sk_rows(a, K_PAGE_ENV1, L, 7);
    }
    if (ui_mark) ui_mark(13);
    return 1;
}
/* ENV 2: its line; then DLY HOLD CRV AMT, then where it goes (the level) */
int env2_view(KApp *a)
{
    static const char *const L[8] = {"Delay", "Hold", "Curve", "Amount", "Level", "", "", ""};
    const int lit = a->win / 4 == 0;
    if (cv.w != 240 || cv.h != 240) return 0;
    sk_frame(a);
    if (phase_env_track(a)) {
        static const char *const P[8] = {"Attack", "Decay", "Sustain", "Release", 0, 0, 0, 0};
        env1_curve(a->params[a->track][K_PAGE_ENV2], sk_screen("DCA", "AMPLITUDE", lit), lit);
        sk_rows(a, K_PAGE_ENV2, P, 4);
    } else {
        env2_curve(a->params[a->track][K_PAGE_ENV2], sk_screen("ENV 2", 0, lit), lit);
        sk_rows(a, K_PAGE_ENV2, L, 5);
    }
    return 1;
}
/* a wide screen (K_LAY_ENV_PAIR): ENV 1 at the left, ENV 2 at the right, each its screen and its two rows; the rows in the order
 * the knobs take them (ENV 1's A D S R, its destinations, ENV 2's DLY HOLD CRV AMT, its level), the one in hand bright */
int env_pair_view(KApp *a)
{
    static const char *const L[2][8] = {{"Atk", "Dec", "Sus", "Rel", "Pitch", "Filter", "Shape", 0}, {"Delay", "Hold", "Curve", "Amt", "Level", 0, 0, 0}};
    static const char *const PH[2][8] = {{"Atk", "Dec", "Sus", "Rel", "Depth", 0, 0, 0}, {"Atk", "Dec", "Sus", "Rel", 0, 0, 0, 0}};
    const int row = a->win / 4, wx = whole.x, wy = whole.y, half = whole.w / 2;
    int s2, c;
    if (!(a->lay & K_LAY_ENV_PAIR) || a->page != K_PAGE_ENV1 || cv.w < 300 || cv.h > 200 || drum_track(a)) return 0;   /* (a drum's ENV
                                                 * items are its lane's, not ENV 1 / ENV 2's curves: the core pairs none there either) */
    sk_skin(a);
    g_fill_around(&cv, pic.x, pic.y, pic.w, pic.h, wx, wy + 8, whole.w, whole.h - 16, th.bg);
    g_rrect_frame(&cv, wx, wy, whole.w, whole.h, 8, SK_OUT, SK_PANEL);
    if (sk.wood == 1) {   /* the cheeks */
        g_rrect(&cv, wx + 1, wy + 1, 5, whole.h - 2, 3, SK_WOOD);
        g_rrect(&cv, wx + whole.w - 6, wy + 1, 5, whole.h - 2, 3, SK_WOOD);
    }
    g_fill(&cv, wx + half, wy + 8, 1, whole.h - 16, SK_OUT);   /* ENV 1 | ENV 2 */
    sk_cw = 38; sk_kr = cv.h < 180 ? 6 : 7; sk_gap = cv.h < 180 ? 2 : 4;
    for (s2 = 0; s2 < 2; s2++) {
        const int x = wx + half * s2 + 10, w = half - 20, lit = row / 2 == s2, base = s2 * 8;
        const uint32_t amber = lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62);
        const int sh = cv.h < 180, pitch = (whole.h - 35) / 2;   /* two rows under a 26 high screen, to the panel's foot (a shorter
                                                 * screen, the Zero's: smaller knobs nearer their labels, the values inside) */
        KRect r = {x, wy + 5, w, 26};
        g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 3, sk.lcd_edge, sk.lcd);
        const int phase = phase_env_track(a);
        g_text(&cv, r.x + 5, r.y + 2, GF_S, phase ? (s2 ? "DCA" : "DCW") : (s2 ? "ENV 2" : "ENV 1"), amber);
        if (s2 && !phase) env2_curve(a->params[a->track][K_PAGE_ENV2], r, lit);
        else env1_curve(a->params[a->track][s2 ? K_PAGE_ENV2 : K_PAGE_ENV1], r, lit);
        for (c = 0; c < 8; c++) {
            const char *label = phase ? PH[s2][c] : L[s2][c];
            if (!label) continue;
            sk_control(a, K_PAGE_ENV1, base + c, x + w / 8 + w / 4 * (c & 3), wy + 35 - sh + pitch * (c / 4), wy + 63 - 4 * sh + pitch * (c / 4), label, row == (base + c) / 4);
        }
    }
    sk_cw = 56; sk_kr = 10; sk_gap = 1;
    return 1;
}
/* LFO: its wave over two and a half cycles, its rate at the right; then RATE WAVE PHS FADE, then where it goes */
int lfo_view(KApp *a)
{
    static const char *const L[8] = {"Rate", "Wave", "Phase", "Fade", "Pitch", "Filter", "Shape", "Amp"};
    const KVal *P = a->params[a->track][K_PAGE_LFO], lit = a->win / 4 == 0;
    char rate[16], rd[24];
    const char *u;
    KRect r;
    int q, wv = P[1] < 0 ? 0 : P[1] > 4 ? 4 : P[1];
    if (cv.w != 240 || cv.h != 240) return 0;
    sk_frame(a);
    if (ui_mark) ui_mark(11);
    ui_item_value(a, K_PAGE_LFO, 0, rate, sizeof rate, &u);
    snprintf(rd, sizeof rd, "%s %s", rate, u);
    if (drum_track(a)) {   /* (the rate beside the name: the lane's box takes the right) */
        char tg[32];
        snprintf(tg, sizeof tg, "LFO  %s", rd);
        r = sk_screen(tg, 0, lit);
    } else r = sk_screen("LFO", rd, lit);
    {   /* the wave, two and a half cycles; S&H from an integer hash */
        int p16[2 * 101], ww = r.w - (drum_track(a) ? 80 : 16);
        for (q = 0; q <= 100; q++) {
            const uint32_t ph = (uint32_t)q * 107374182u;   /* 2.5 turns over 100 points */
            int y;
            switch (wv) {
            case 0: y = g_isin(ph); break;
            case 1: y = ph < 0x80000000u ? (int)(ph >> 16) - 16384 : 49152 - (int)(ph >> 16); break;
            case 2: y = 16384 - (int)(ph >> 17); break;
            case 3: y = ph < 0x80000000u ? 16384 : -16384; break;
            default: { uint32_t h = (uint32_t)(q / 10) * 2654435761u; h ^= h >> 15; y = (int)(h & 32767u) - 16384; } break;
            }
            p16[2 * q] = (r.x + 8) * 16 + q * ww * 16 / 100;
            p16[2 * q + 1] = (r.y + r.h / 2 + 7) * 16 - y * (r.h / 2 - 14) / 1024;
        }
        if (ui_mark) ui_mark(12);
        g_curve16(&cv, p16, 101, 17, lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62));   /* (1.2 px) */
    }

    if (ui_mark) ui_mark(14);
    if (drum_track(a)) {   /* a drum track: the routings reach a lane's (or all lanes') pitch, TONE, DCAY, level as it is struck; which lane at
                            * the screen's right, the third row */
        static const char *const D[8] = {"Rate", "Wave", "Phase", "Fade", "Pitch", "Tone", "Decay", "Level"};
        const int row = a->win / 4;
        g_rrect_frame(&cv, r.x + r.w - 62, r.y + 14, 56, r.h - 18, 3, row == 2 ? SK_ARC : sk.lcd_edge, sk.lcd);
        sk_control(a, K_PAGE_LFO, 8, r.x + r.w - 34, r.y + 15, r.y + 40, "Lane", row == 2);
        sk_rows(a, K_PAGE_LFO, D, 8);
        return 1;
    }
    sk_rows(a, K_PAGE_LFO, L, 8);
    if (ui_mark) ui_mark(13);
    return 1;
}

static void pic_env_graph(KApp *a, int second)
{
    const KVal *P = a->params[a->track][second ? K_PAGE_ENV2 : K_PAGE_ENV1]; int n, i, gx = pic.x + PIC_PAD + 4, gy = pic.y + PIC_PAD + 4, gw = pic.w - 2 * PIC_PAD - 8, gh = pic.h - 2 * PIC_PAD - 8;
    int p16[80];
    if (!second || phase_env_track(a)) {   /* (in integers: the times in 1/10000, the levels in 1/100) */
        const int at = 400 + 28 * P[0], de = 400 + 28 * P[1], re = 400 + 28 * P[3], T = at + de + 2000 + re;
        const int U[5] = {0, at, at + de, at + de + 2000, T}, V[5] = {0, 100, P[2], P[2], 0};
        for (i = 0; i < 5; i++) { p16[2 * i] = gx * 16 + U[i] * gw / T * 16 + (U[i] * gw % T) * 16 / T; p16[2 * i + 1] = (gy + gh) * 16 - (V[i] * gh * 16 + 99) / 100; }
        n = 5;
    } else {   /* (the fall: (1 - f)^k, k = .4 + .03 P2, of amt = (100 + 3 P3) / 400) */
        const int dl = 300 + 25 * P[0], hd = 300 + 25 * P[1], T = dl + hd + 4500, k100 = 40 + 3 * P[2], amt = (100 + 3 * P[3]) * 65536 / 400;
        const int U[4] = {0, dl, dl, dl + hd};
        for (i = 0; i < 4; i++) p16[2 * i] = gx * 16 + U[i] * gw / T * 16 + (U[i] * gw % T) * 16 / T;
        p16[1] = p16[3] = (gy + gh) * 16;
        p16[5] = p16[7] = ((gy + gh) * 16 * 65536 - amt * gh * 16) >> 16;
        for (i = 1; i <= 36; i++) {
            const int un = (dl + hd) * 36 + 4500 * i, pw = i < 36 ? (int)g_exp2(g_log2((uint32_t)(36 - i) * 65536u / 36u) / 100 * k100, 16) : 0;
            p16[2 * (3 + i)] = gx * 16 + (int)((uint32_t)un * (uint32_t)gw / (uint32_t)(36 * T)) * 16 + (int)((uint32_t)un * (uint32_t)gw % (uint32_t)(36 * T) * 16u / (uint32_t)(36 * T));
            p16[2 * (3 + i) + 1] = ((gy + gh) * 16 * 65536 - (int)((uint32_t)amt * (uint32_t)pw >> 16) * gh * 16) >> 16;
        }
        n = 40;
    }
    g_fill(&cv, gx, gy + gh, gw, 1, th.line);
    g_curve_fill16(&cv, p16, n, gy + gh, th.tint);
    curve_glow16(p16, n, k_mix(th.theme, 0xFFFFFF, 30));
}
/* the two envelopes one above the other, each in a box (the synth page's way): the page's own a box in full, the other a ghost, so the
 * room is used and both shapes show. */
void pic_env(KApp *a, int second)
{
    KRect keep = pic;
    int h = (keep.h - 2) / 2, k;
    for (k = 0; k < 2; k++) {
        int top = keep.y + k * (h + 2), on = k == second;
        if (on) panel_box((KRect){keep.x, top, keep.w, h});
        else ghost_box(keep.x, top, keep.w, h);
        pic = (KRect){keep.x + 2, top + 12, keep.w - 4, h - 14};
        pic_env_graph(a, k);
        pic = keep;
        g_text(&cv, keep.x + 8, top + 2, GF_S, phase_env_track(a) ? (k ? "DCA" : "DCW") : (k ? "ENV 2" : "ENV 1"), on ? th.accent : th.dim);
    }
}
