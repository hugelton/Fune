/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The wide screens' panels: the Switch's engine panels (640 x 360) and a 320 x 180 screen's (the Zero's) SOUND, EDIT
 * and LFO. Neither is in the FM-1's build. */
#include "draw_int.h"
#ifndef KREISEL_BITMAP_FONT   /* (the Switch's and the wide screens' views: never on the FM-1, whose build leaves them out) */
/* ---- the Switch's engine panels (640 x 360, K_LAY_SYNTH_ALL): each engine drawn for this screen, not the FM-1's rows laid out
 * again; its pictures large (the waves, the filter), its controls in sections as a hardware panel's ---- */
static void sw_section(int x, int y, int w, int h, const char *title)
{
    g_rrect_frame(&cv, x, y, w, h, 6, k_mix(SK_PANEL, SK_OUT, 60), k_mix(SK_PANEL, 0x000000, 10));
    if (title && title[0]) g_text(&cv, x + 8, y + 2, GF_S, wcase(title), (prof->input & K_IN_MOUSE) ? SK_INK : k_mix(SK_PANEL, SK_INK, 70));   /* (a desktop: the heading in full ink, over lighter labels) */
}
static KRect sw_screen(int x, int y, int w, int h, const char *tag, const char *right)   /* a screen of the panel, its words in amber */
{
    g_rrect_frame(&cv, x, y, w, h, 4, sk.lcd_edge, sk.lcd);
    if (tag) g_text(&cv, x + 7, y + 4, GF_S, wcase(tag), SK_LCD_WAVE);
    if (right) g_text(&cv, x + w - 7 - g_text_w(GF_S, right), y + 4, GF_S, right, SK_LCD_WAVE);
    return (KRect){x, y, w, h};
}
/* ANALOG on the Switch: the whole voice on one panel, four bands: PITCH | OSC 1 | OSC 2 | SUB NOISE; the filter (its
 * response) and the output; ENV 1 (its curve), where it goes, ENV 2; the LFO (its wave), where it goes (bands of 42 px cells) */
#define SW_TOP ((prof->input & K_IN_MOUSE) ? 19 : 14)   /* a section's screen under its heading (a desktop: clear of it) */
#define SWC 42   /* a cell (fourteen across the 614 px of the panel, with the sections' gaps) */
static int sw_cell_x(int x0, int k) { return x0 + k * SWC + SWC / 2; }
static void sw_knobs(KApp *a, KPage pg, int i0, int n, int x0, int y, const char *const *L)
{
    int c;
    for (c = 0; c < n; c++) sk_control(a, pg, i0 + c, sw_cell_x(x0, c), y + 16, y + 16 + 15 + 2 + sk_kr, L[c], 1);
}
/* the LFO's wave on a screen: 41 points, its cycles 1 + 3 RATE / 127 from PHS, from x0 w px wide, about yc, amp px up and down */
static void lfo_screen16(const KVal *L, int x0, int w, int yc, int amp)
{
    const int k = L[1] < 0 ? 0 : L[1] > 4 ? 4 : L[1], sc = 127 + 3 * (L[0] < 0 ? 0 : L[0] > 127 ? 127 : L[0]), ph = 40 * (L[2] < 0 ? 0 : L[2] > 127 ? 127 : L[2]);
    int p16[2 * 41], q;
    for (q = 0; q <= 40; q++) {   /* (q / 40 (1 + 3 L0 / 127) + L2 / 127 cycles: whole 5080ths) */
        const int num = q * sc + ph, y = lfo_wave16(k, num / 5080, (num % 5080) * 65536 / 5080);
        p16[2 * q] = x0 * 16 + q * w * 16 / 40;
        p16[2 * q + 1] = (yc * 16 * 65536 - y * amp * 16) >> 16;
    }
    g_curve16(&cv, p16, 41, 17, SK_LCD_WAVE);
}
/* the Switch's two lower bands, every engine's that has them: ENV 1 (its curve) | where it goes | ENV 2; the LFO (its wave) |
 * where it goes. The controls are SOUND's items past the engine's (k_synth_all_sub: ENV 1's page paired with ENV 2, then the LFO) */
static void sw_mod_bands(KApp *a, const KEngine *en, int y, int rh)
{
    static const char *const LE[4] = {"Atk", "Dec", "Sus", "Rel"}, *const LD[3] = {"Pitch", "Filter", "Shape"};
    static const char *const L2[4] = {"Delay", "Hold", "Curve", "Amt"}, *const LL[4] = {"Rate", "Wave", "Phase", "Fade"};
    static const char *const LLD[4] = {"Pitch", "Filter", "Shape", "Amp"}, *const LV[1] = {"Level"};
    const int t = a->track, wx = whole.x; const KVal *E1 = a->params[t][K_PAGE_ENV1], *L = a->params[t][K_PAGE_LFO];
    int x = wx + 10;
    if (k_page_unused(a, K_PAGE_ENV1)) return;
    sk_base = en->nparams;   /* (ENV 1's: SOUND's items past the engine's, k_synth_all_sub) */
    sw_section(x, y, 6 * SWC, rh - 4, "ENV 1");
    env1_curve(E1, sw_screen(x + 6, y + SW_TOP, 2 * SWC - 12, rh - 8 - SW_TOP, 0, 0), 1);
    sw_knobs(a, K_PAGE_ENV1, 0, 4, x + 2 * SWC, y, LE);
    x += 6 * SWC + 6;
    sw_section(x, y, 3 * SWC, rh - 4, "ENV 1 TO"); sw_knobs(a, K_PAGE_ENV1, 4, 3, x, y, LD);
    x += 3 * SWC + 6;
    sk_base = en->nparams + 8;
    sw_section(x, y, 5 * SWC, rh - 4, "ENV 2"); sw_knobs(a, K_PAGE_ENV2, 0, 4, x, y, L2); sw_knobs(a, K_PAGE_ENV2, 4, 1, x + 4 * SWC, y, LV);
    if (k_page_unused(a, K_PAGE_LFO)) { sk_base = -1; return; }
    y += rh; x = wx + 10;
    sk_base = en->nparams + k_page_count(a, K_PAGE_ENV1);
    sw_section(x, y, 10 * SWC, rh - 4, "LFO");   /* (its screen wide: each row of the panel fourteen cells across) */
    {   const KRect r = sw_screen(x + 6, y + SW_TOP, 6 * SWC - 12, rh - 8 - SW_TOP, 0, 0);
        lfo_screen16(L, r.x + 5, r.w - 10, r.y + r.h / 2, r.h / 2 - 5); }
    sw_knobs(a, K_PAGE_LFO, 0, 4, x + 6 * SWC, y, LL);
    x += 10 * SWC + 6;
    sw_section(x, y, 4 * SWC, rh - 4, "LFO TO"); sw_knobs(a, K_PAGE_LFO, 4, 4, x, y, LLD);
    sk_base = -1;
}
/* a filter's response on a screen of the panel: its type (kt), cutoff (kc), resonance (kr), the engine's items */
static void sw_filter(const KEngine *en, const KVal *P, int kt, int kc, int kr, int x, int y, int w, int h)
{
    const int ft = k_param_clamp(&en->edit[kt], P[kt]);
    const KRect r = sw_screen(x, y, w, h, en->edit[kt].names ? en->edit[kt].names[ft] : "", 0);
    int p16[2 * 61];
    filter_pts16(ft, P[kc], P[kr], r.x + 6, r.w - 12, r.y + r.h - 4, r.h - 20, p16);
    g_curve16(&cv, p16, 61, 17, SK_LCD_WAVE);
}
/* a wave drawn on a screen of the panel: 121 points of f over the screen, f's sample (Q16) at q of 120 */
static void sw_wave(KRect r, int (*f)(const KApp *, int), const KApp *a)
{
    int p16[2 * 121], q;
    for (q = 0; q <= 120; q++) {
        int y = f(a, q);
        y = y > 65536 ? 65536 : y < -65536 ? -65536 : y;
        p16[2 * q] = (r.x + 8) * 16 + q * (r.w - 16) * 16 / 120;
        p16[2 * q + 1] = ((r.y + 14 + (r.h - 18) / 2) * 16 * 65536 - y * ((r.h - 22) / 2) * 16) >> 16;
    }
    g_curve16(&cv, p16, 121, 18, SK_LCD_WAVE);   /* (1.3 px) */
}
/* 2^(semis / 12), Q16; a phase in cycles (Q16) times it */
static uint32_t semis16(int semis) { return g_exp2((int32_t)((int64_t)semis * 16777216 / 12), 16); }
static uint32_t tmul16(uint32_t t, uint32_t r16) { return (uint32_t)((uint64_t)t * r16 >> 16); }
static uint32_t dcw16(int v) { return (uint32_t)(v < 0 ? 0 : v > 127 ? 127 : v) * 65536u / 127u; }
/* the panel's head and its sizes: the bar, then four bands; returns the band height */
static int sw_begin(KApp *a, const KEngine *en)
{
    wide_head(a, K_PAGE_SOUND, 0, 0, en, a->track);
    sk_cw = SWC; sk_kr = 9; sk_gap = 2;
    return (whole.h - 34) / 4;
}
static int sw_end(void) { sk_base = -1; sk_cw = 56; sk_kr = 10; sk_gap = 1; return 1; }
#define SW_X0 (whole.x + 10)
#define SW_Y0 (whole.y + 30)
#define SW_NEXT(x, cells) ((x) + (cells) * SWC + 6)
static int switch_view_kanalog(KApp *a)
{
    const int t = a->track;
    const KVal *P = a->params[t][K_PAGE_SOUND] + 4;
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LP[3] = {"Pitch", "Glide", "Detune"}, *const LS[4] = {"Sub", "Sub Oct", "Noise", "Color"};
    static const char *const LF[6] = {"Cutoff", "Reso", "Drive", "KeyTrk", "Type", "Env"}, *const LO[2] = {"Level", "Vel"};
    int y, x, rh;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;   /* PITCH | OSC 1 | OSC 2 | SUB NOISE */
    sw_section(x, y, 4 * SWC, rh - 4, "PITCH"); sw_knobs(a, K_PAGE_SOUND, 0, 3, x, y, LP); sk_switch_v(a, K_PAGE_SOUND, 3, sw_cell_x(x, 3), y + 14, "Sync");
    x = SW_NEXT(x, 4);
    sw_section(x, y, 3 * SWC, rh - 4, "OSC 1"); sk_wave_w(a, 4, x + 6, y + 16, y + 32, 2 * SWC - 10, "Wave", 1, 0, 0); sk_control(a, K_PAGE_SOUND, 5, sw_cell_x(x, 2), y + 16, y + 33 + sk_kr, "Level", 1);
    x = SW_NEXT(x, 3);
    sw_section(x, y, 3 * SWC, rh - 4, "OSC 2"); sk_wave_w(a, 6, x + 6, y + 16, y + 32, 2 * SWC - 10, "Wave", 1, 0, 0); sk_control(a, K_PAGE_SOUND, 7, sw_cell_x(x, 2), y + 16, y + 33 + sk_kr, "Level", 1);
    x = SW_NEXT(x, 3);
    sw_section(x, y, 4 * SWC, rh - 4, "SUB  NOISE"); sw_knobs(a, K_PAGE_SOUND, 8, 4, x, y, LS);
    y += rh; x = SW_X0;   /* FILTER (its response) | OUT */
    sw_section(x, y, 12 * SWC, rh - 4, "FILTER");
    sw_filter(en, P, 16, 12, 13, x + 6, y + SW_TOP, 6 * SWC - 12, rh - 8 - SW_TOP);
    sw_knobs(a, K_PAGE_SOUND, 12, 6, x + 6 * SWC, y, LF);
    x = SW_NEXT(x, 12);
    sw_section(x, y, 2 * SWC, rh - 4, "OUT"); sw_knobs(a, K_PAGE_SOUND, 18, 2, x, y, LO);
    sw_mod_bands(a, en, y + rh, rh);
    return sw_end();
}
/* PHASE: its two lines' bent waves mixed on the large screen; PITCH, LINE 1, LINE 2 above; then the rest and its output; its three
 * envelopes (DCO, DCW, DCA) and the LFO below */
static int sw_phase_mix(const KApp *a, int q)   /* (two cycles over the 120) */
{
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    const int a1 = P[7] < 0 ? 0 : P[7], a2 = P[11] < 0 ? 0 : P[11];
    const uint32_t t = (uint32_t)q * 65536u / 60u;
    if (!a1 && !a2) return 0;
    return (a1 * sk_wave_i(2, P[4], t, dcw16(P[6])) + a2 * sk_wave_i(2, P[8], tmul16(t, semis16(P[9])), dcw16(P[10]))) / (a1 + a2);
}
static int switch_view_kphase(KApp *a)
{
    const int t = a->track; const KVal *P = a->params[t][K_PAGE_SOUND] + 4, *E2 = a->params[t][K_PAGE_ENV2], *E1 = a->params[t][K_PAGE_ENV1];
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LP[4] = {"Pitch", "Glide", "Detune", "Line"}, *const L1[3] = {"DCO", "DCW", "DCA"};
    static const char *const LR[5] = {"Sub", "Fold", "Noise", "Drive", "KeyTrk"}, *const LO[2] = {"Level", "Vel"};
    static const char *const LE[4] = {"Atk", "Dec", "Sus", "Rel"}, *const LL[8] = {"Rate", "Wave", "Phase", "Fade", "Pitch", "Filter", "Shape", "Amp"};
    int y, x, rh, k;
    (void)P;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;   /* PITCH | LINE 1 | LINE 2 */
    sw_section(x, y, 4 * SWC, rh - 4, "PITCH"); sw_knobs(a, K_PAGE_SOUND, 0, 4, x, y, LP);
    for (k = 0; k < 2; k++) {
        x = SW_NEXT(x, k ? 5 : 4);
        sw_section(x, y, 5 * SWC, rh - 4, k ? "LINE 2" : "LINE 1");
        sk_wave_w(a, 4 + 4 * k, x + 6, y + 16, y + 32, 2 * SWC - 10, "Wave", 1, 2, a->params[t][K_PAGE_SOUND][4 + 6 + 4 * k]);
        sw_knobs(a, K_PAGE_SOUND, 5 + 4 * k, 3, x + 2 * SWC, y, L1);
    }
    y += rh; x = SW_X0;   /* the lines together | the rest | OUT */
    sw_section(x, y, 7 * SWC, rh - 4, "");
    sw_wave(sw_screen(x + 6, y + 6, 7 * SWC - 12, rh - 16, "LINE 1 + 2", 0), sw_phase_mix, a);
    x = SW_NEXT(x, 7);
    sw_section(x, y, 5 * SWC, rh - 4, "TONE"); sw_knobs(a, K_PAGE_SOUND, 12, 5, x, y, LR);
    x = SW_NEXT(x, 5);
    sw_section(x, y, 2 * SWC, rh - 4, "OUT"); sw_knobs(a, K_PAGE_SOUND, 17, 2, x, y, LO);
    /* the envelopes: DCO, DCW, DCA (ENV 1's page on PHASE: DCO 0..3, DCW 4..7, DCA 8..11), then the LFO */
    sk_base = en->nparams;
    for (k = 0; k < 3; k++) {
        const int by = y + rh + (k == 2 ? rh : 0), bx = k == 1 ? SW_NEXT(SW_X0, 7) : SW_X0, c = k == 2 ? 2 : 3;   /* (DCO, DCW: 7 cells; DCA 6, the LFO beside it) */
        const KVal *E = k == 0 ? E2 + 4 : k == 1 ? E1 : E2;   /* (where kreisel.c keeps them: DCO in ENV 2's 4..7, DCW ENV 1, DCA ENV 2) */
        sw_section(bx, by, (c + 4) * SWC, rh - 4, k == 0 ? "DCO  PITCH" : k == 1 ? "DCW  WAVE" : "DCA  LEVEL");
        env1_curve(E, sw_screen(bx + 6, by + SW_TOP, c * SWC - 12, rh - 8 - SW_TOP, 0, 0), 1);
        sw_knobs(a, K_PAGE_ENV1, 4 * k, 4, bx + c * SWC, by, LE);
    }
    sk_base = en->nparams + k_page_count(a, K_PAGE_ENV1);
    x = SW_NEXT(SW_X0, 6); y += 2 * rh;
    sw_section(x, y, 8 * SWC, rh - 4, "LFO"); sw_knobs(a, K_PAGE_LFO, 0, 8, x, y, LL);
    return sw_end();
}
/* TRIO: its three waves on their screens, PITCH and the levels; the filter (its response) and the output */
static int switch_view_ktrio(KApp *a)
{
    const int t = a->track; const KVal *P = a->params[t][K_PAGE_SOUND] + 4;
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LP[4] = {"Int 2", "Int 3", "Detune", "Mod"}, *const LV[3] = {"Level 1", "Level 2", "Level 3"};
    static const char *const LF[6] = {"Mode", "Cutoff", "Reso", "KeyTrk", "Drive", "Env"}, *const LO[2] = {"Level", "Vel"}, *const LW[1] = {"PW"};
    int y, x, rh, k;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;
    sw_section(x, y, 4 * SWC, rh - 4, "PITCH"); sw_knobs(a, K_PAGE_SOUND, 0, 4, x, y, LP);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 7 * SWC, rh - 4, "WAVES");
    for (k = 0; k < 3; k++) { char l[4] = {'W', (char)('1' + k), 0}; sk_wave_w(a, 4 + k, x + 6 + k * 2 * SWC, y + 16, y + 32, 2 * SWC - 10, l, 1, 1, 0); }
    sw_knobs(a, K_PAGE_SOUND, 7, 1, x + 6 * SWC, y, LW);
    x = SW_NEXT(x, 7);
    sw_section(x, y, 3 * SWC, rh - 4, "LEVELS"); sw_knobs(a, K_PAGE_SOUND, 8, 3, x, y, LV);
    y += rh; x = SW_X0;
    sw_section(x, y, 12 * SWC, rh - 4, "FILTER");
    sw_filter(en, P, 11, 12, 13, x + 6, y + SW_TOP, 6 * SWC - 12, rh - 8 - SW_TOP);
    sw_knobs(a, K_PAGE_SOUND, 11, 6, x + 6 * SWC, y, LF);
    x = SW_NEXT(x, 12);
    sw_section(x, y, 2 * SWC, rh - 4, "OUT"); sw_knobs(a, K_PAGE_SOUND, 17, 2, x, y, LO);
    sw_mod_bands(a, en, y + rh, rh);
    return sw_end();
}
/* FM2: OP2 into OP1, what comes out on the large screen; PITCH and INDEX; the output */
static int sw_fm_out(const KApp *a, int q)   /* sin(ph + ix m), m = sin(ph ratio + fb sin(ph ratio)), two cycles over the 120 */
{
    static const uint8_t R2[8] = {1, 2, 4, 6, 8, 12, 16, 32};   /* (twice the ratio) */
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    const int r2 = R2[P[2] < 0 ? 0 : P[2] > 7 ? 7 : P[2]], ix = P[4] < 0 ? 0 : P[4] > 127 ? 127 : P[4], fb = P[6] < 0 ? 0 : P[6] > 127 ? 127 : P[6];
    const int64_t RAD = 683565276;   /* a radian in turns of 2^32 */
    const uint32_t pr = (uint32_t)(((uint64_t)q * (uint64_t)r2 << 32) / 120u);   /* (ph ratio, turns of 2^32) */
    const int m = g_isin(pr + (uint32_t)((int64_t)fb * g_isin(pr) * RAD / (127 * 16384)));
    return 4 * g_isin((uint32_t)(((uint64_t)q << 32) / 60u) + (uint32_t)((int64_t)ix * 6 * m * RAD / (127 * 16384)));
}
static int sw_fm_mod(const KApp *a, int q)   /* OP 2: sin(ph ratio + fb sin(ph ratio)), at its ratio over the same two cycles */
{
    static const uint8_t R2[8] = {1, 2, 4, 6, 8, 12, 16, 32};
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    const int r2 = R2[P[2] < 0 ? 0 : P[2] > 7 ? 7 : P[2]], fb = P[6] < 0 ? 0 : P[6] > 127 ? 127 : P[6];
    const int64_t RAD = 683565276;
    const uint32_t pr = (uint32_t)(((uint64_t)q * (uint64_t)r2 << 32) / 120u);
    return 4 * g_isin(pr + (uint32_t)((int64_t)fb * g_isin(pr) * RAD / (127 * 16384)));
}
static int switch_view_kfm2(KApp *a)
{
    const int t = a->track;
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LP[4] = {"Pitch", "Tune", "Ratio", "Glide"}, *const LI[4] = {"Index", "Env", "Feed", "KeyTrk"};
    static const char *const LO[4] = {"Mix", "Drive", "Level", "Vel"};
    int y, x, rh;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;
    sw_section(x, y, 4 * SWC, rh - 4, "PITCH"); sw_knobs(a, K_PAGE_SOUND, 0, 4, x, y, LP);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 4 * SWC, rh - 4, "OP 2 > OP 1"); sw_knobs(a, K_PAGE_SOUND, 4, 4, x, y, LI);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 6 * SWC, 2 * rh - 4, "");
    sw_wave(sw_screen(x + 6, y + 6, 6 * SWC - 12, 2 * rh - 16, "OUT", 0), sw_fm_out, a);
    y += rh; x = SW_X0;
    sw_section(x, y, 4 * SWC, rh - 4, "OUTPUT"); sw_knobs(a, K_PAGE_SOUND, 8, 4, x, y, LO);
    x = SW_NEXT(x, 4);   /* OP 2 alone (with its feedback): what goes into OP 1 */
    sw_section(x, y, 4 * SWC, rh - 4, "");
    sw_wave(sw_screen(x + 6, y + 6, 4 * SWC - 12, rh - 16, "OP 2", 0), sw_fm_mod, a);
    sw_mod_bands(a, en, y + rh, rh);
    return sw_end();
}
/* LOFI: a console's voice: PULSE, MOTION, NOISE; its wave on a screen; STEP and the output */
static int sw_lofi_wave(const KApp *a, int q)   /* three cycles over the 120, in the steps CRUSH leaves */
{
    static const int D[4] = {8192, 16384, 32768, 49152};   /* (the duties, Q16) */
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4, w = P[0] < 0 ? 0 : P[0] > 3 ? 3 : P[0];
    const int ph = (3 * q % 120) * 65536 / 120, bits = 16 - 2 * (P[2] < 0 ? 0 : P[2] > 7 ? 7 : P[2]), steps = 1 << ((bits < 8 ? bits : 8) - 1);
    const int y = w == 3 ? 0 : w == 0 ? (ph < D[P[1] < 0 ? 0 : P[1] > 3 ? 3 : P[1]] ? 65536 : -65536) : w == 1 ? (ph < 32768 ? 4 * ph - 65536 : 196608 - 4 * ph) : 2 * ph - 65536;
    return ((y * steps + 32768) >> 16) * 65536 / steps;
}
static int switch_view_klofi(KApp *a)
{
    const int t = a->track;
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LPu[4] = {"Wave", "Duty", "Crush", "Rate"}, *const LM[4] = {"Sweep", "Vib", "Arp", "Speed"};
    static const char *const LN[4] = {"Noise", "Level", "Color", "Tone"}, *const LO[3] = {"Step", "Level", "Vel"};
    int y, x, rh;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;
    sw_section(x, y, 4 * SWC, rh - 4, "PULSE"); sw_knobs(a, K_PAGE_SOUND, 0, 4, x, y, LPu);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 4 * SWC, rh - 4, "MOTION"); sw_knobs(a, K_PAGE_SOUND, 4, 4, x, y, LM);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 6 * SWC, rh - 4, "NOISE"); sw_knobs(a, K_PAGE_SOUND, 8, 4, x, y, LN);
    y += rh; x = SW_X0;
    sw_section(x, y, 9 * SWC, rh - 4, "");
    {   KRect r = sw_screen(x + 6, y + 6, 9 * SWC - 12, rh - 16, "CHIP", 0);
        if (prof->input & K_IN_MOUSE) { r.y = (int16_t)(r.y + 14); r.h = (int16_t)(r.h - 14); }   /* (a desktop: the wave under its tag) */
        sw_wave(r, sw_lofi_wave, a); }
    x = SW_NEXT(x, 9);
    sw_section(x, y, 5 * SWC, rh - 4, "OUT"); sw_knobs(a, K_PAGE_SOUND, 12, 3, x, y, LO);
    sw_mod_bands(a, en, y + rh, rh);
    return sw_end();
}
/* ENSEMBLE: its five registers as faders over the strings' screen; the ensemble; the tone and the output */
static int switch_view_kensemble(KApp *a)
{
    const int t = a->track;
    const KEngine *en = k_engine_at(a->sound[t]);
    static const char *const LR[6] = {"Bass", "Cello", "Viola", "Violin", "Brass", "Tone"}, *const LE[4] = {"Ens", "Rate", "Depth", "Detune"};
    static const char *const LO[1] = {"Level"}, *const LT[5] = {"HPF", "Drive", "Noise", "Env", "Vel"};
    int y, x, rh;
    rh = sw_begin(a, en);
    y = SW_Y0; x = SW_X0;
    sw_section(x, y, 6 * SWC, rh - 4, "STRINGS"); sw_knobs(a, K_PAGE_SOUND, 0, 6, x, y, LR);
    x = SW_NEXT(x, 6);
    sw_section(x, y, 4 * SWC, rh - 4, "ENSEMBLE"); sw_knobs(a, K_PAGE_SOUND, 6, 4, x, y, LE);
    x = SW_NEXT(x, 4);
    sw_section(x, y, 4 * SWC - 12, rh - 4, "OUT"); sw_knobs(a, K_PAGE_SOUND, 10, 1, x, y, LO);
    y += rh; x = SW_X0;
    sw_section(x, y, 5 * SWC, rh - 4, "TONE"); sw_knobs(a, K_PAGE_SOUND, 11, 5, x, y, LT);
    sw_mod_bands(a, en, y + rh, rh);
    return sw_end();
}
/* ORGAN on the Switch: its nine drawbars, long, in their well; ROTARY, VIB / CHO, PERC stacked beside it, each named over its
 * switch; under them PERC's volume and decay, the click and the drive, the output, in three sections (no ENV, no LFO: an organ) */
static int switch_view_korgan(KApp *a)
{
    static const char *const ROT[3] = {"0", "S", "F"}, *const PRC[3] = {"0", "2", "3"};
    static const char *const LP[2] = {"P Vol", "P Dcy"}, *const LC[2] = {"Click", "Drive"}, *const LO[2] = {"Level", "Vel"};
    static const char *const *const LB[3] = {LP, LC, LO}, *const TB[3] = {"PERC", "CLICK  DRIVE", "OUT"};
    const int t = a->track, rh = (whole.h - 34) / 4, sp = 46, ww = 9 * sp + 20, y0 = SW_Y0, y1 = SW_Y0 + 3 * rh - 4;
    const int wh = y1 - y0, step = (wh - 64) / 8 < 7 ? 7 : (wh - 64) / 8 > 17 ? 17 : (wh - 64) / 8, bar = 3 + 8 * step + 30;
    const KEngine *en = k_engine_at(a->sound[t]);
    int k, rx, rw;
    wide_head(a, K_PAGE_SOUND, 0, 0, en, t);
    g_rrect_frame(&cv, SW_X0, y0, ww, wh, 6, 0x0B0806, KO_WELL);
    g_hline_a(&cv, SW_X0 + 3, y0 + 1, ww - 6, 0x000000, 120);
    ko_step = step;
    for (k = 0; k < 9; k++) ko_drawbar(a, 3 + k, SW_X0 + 10 + sp / 2 + k * sp, y0 + (wh - bar) / 2, 1);
    ko_step = 7;
    rx = SW_X0 + ww + 6; rw = whole.x + whole.w - 10 - rx;
    sk_cw = 56; sk_kr = 10; sk_gap = 2;
    {   const int pitch = 52, top = y0 + (wh - 3 * pitch) / 2 + 18, tw = rw - 24;   /* the three, close together, centred in the column */
        sw_section(rx, y0, rw, wh, "VOICE");
        sk_switch_at(a, K_PAGE_SOUND, 0, rx + 10, top - 14, rx + 12, top, tw, "Rotary", ROT, 1);
        ko_vibcho_at(a, 1, rx + 10, top + pitch - 14, rx + 12, top + pitch - 2, tw, 20);
        sk_switch_at(a, K_PAGE_SOUND, 2, rx + 10, top + 2 * pitch - 14, rx + 12, top + 2 * pitch, tw, "Perc", PRC, 1); }
    sk_cw = SWC + 6; sk_kr = 10; sk_gap = 2;
    {   const int y = SW_Y0 + 3 * rh, sw3 = (whole.w - 20 - 2 * 6) / 3;
        for (k = 0; k < 3; k++) {
            const int sx = SW_X0 + k * (sw3 + 6), c0 = sx + sw3 / 2 - 34;
            sw_section(sx, y, sw3, rh - 4, TB[k]);
            sk_control(a, K_PAGE_SOUND, 12 + 2 * k, c0, y + 16, y + 16 + 17 + sk_kr, LB[k][0], 1);
            sk_control(a, K_PAGE_SOUND, 13 + 2 * k, c0 + 68, y + 16, y + 16 + 17 + sk_kr, LB[k][1], 1);
        } }
    return sw_end();
}
/* DRUM on the Switch: the eight lanes as tabs (each its piece); the lane in hand's piece on a screen, its strike, its envelope;
 * the kit's four; and the pattern, every lane's sixteen steps (the lane in hand lit, the step playing marked). A tap on a tab or
 * on a lane of the pattern takes that lane */
static int switch_view_drum(KApp *a)
{
    static const char *const LS[6] = {"Level", "Pan", "Tune", "Tone", "Decay", "Click"}, *const LA[3] = {"Atk", "P Dep", "P Time"};
    static const char *const LK[4] = {"Tune", "Decay", "Crush", "Human"};
    const int t = a->track, rh = (whole.h - 34) / 4, lw = (whole.w - 20 - (K_LANES - 1) * 4) / K_LANES, tabh = 44;
    const KEngine *en = k_engine_at(a->sound[t]);
    int l, y, x, s;
    sw_begin(a, en);
    y = SW_Y0;
    for (l = 0; l < K_LANES; l++) {   /* the lanes */
        const int lx = SW_X0 + l * (lw + 4), on = l == a->cur_lane[t], pc = a->lane_p[t][l][0];
        const char *pn = en->edit[0].names && pc >= 0 && pc < 35 ? en->edit[0].names[pc] : "";
        g_rrect_frame(&cv, lx, y, lw, tabh, 6, on ? SK_ARC : k_mix(SK_PANEL, SK_OUT, 60), on ? k_mix(SK_PANEL, SK_ARC, 25) : k_mix(SK_PANEL, 0x000000, 10));
        g_text_box(&cv, lx, y + 4, lw, 16, GF_M, en->lanes[l] ? en->lanes[l] : "", on ? SK_INK : k_mix(SK_PANEL, SK_INK, 70), 0);
        g_text_box(&cv, lx, y + 24, lw, 14, GF_S, pn, on ? SK_INK : k_mix(SK_PANEL, SK_INK, 55), 0);
        sw_lane_tabs[l] = (KRect){lx, y, lw, tabh};
    }
    y += tabh + 6; x = SW_X0;   /* the lane in hand: its piece, its strike, its envelope */
    {   char b[24];
        const int pc = a->lane_p[t][a->cur_lane[t]][0];
        sw_section(x, y, 4 * SWC, rh - 4, "PIECE");
        snprintf(b, sizeof b, "%s", en->edit[0].names && pc >= 0 && pc < 35 ? en->edit[0].names[pc] : "");
        sw_screen(x + 6, y + SW_TOP, 2 * SWC - 4, rh - 8 - SW_TOP, 0, 0);
        g_text_box(&cv, x + 6, y + SW_TOP, 2 * SWC - 4, rh - 8 - SW_TOP, GF_M, b, SK_LCD_WAVE, 0);
        sk_control(a, K_PAGE_SOUND, 0, sw_cell_x(x, 3), y + 16, y + 16 + 17 + sk_kr, "Piece", 1);
    }
    x = SW_NEXT(x, 4);
    sw_section(x, y, 6 * SWC, rh - 4, "STRIKE"); sw_knobs(a, K_PAGE_SOUND, 1, 6, x, y, LS);
    x = SW_NEXT(x, 6);
    sw_section(x, y, whole.x + whole.w - 10 - x, rh - 4, "ENV");
    {   const int ew = whole.x + whole.w - 10 - x;
        for (s = 0; s < 3; s++) sk_control(a, K_PAGE_SOUND, 7 + s, x + ew * (2 * s + 1) / 6, y + 16, y + 16 + 15 + 2 + sk_kr, LA[s], 1); }
    y += rh; x = SW_X0;
    {   const int bh = whole.y + whole.h - 6 - y, kw = 4 * SWC, gx = x + kw + 6, gw = whole.x + whole.w - 10 - gx;
        const int ky = y + (bh - 2 * (rh - 8)) / 2;
        sw_section(x, y, kw, bh, "KIT");   /* the kit's four, two by two */
        for (s = 0; s < 4; s++) sk_control(a, K_PAGE_SOUND, 10 + s, x + kw * (2 * (s & 1) + 1) / 4, ky + (s >> 1) * (rh - 8), ky + (s >> 1) * (rh - 8) + 15 + 2 + sk_kr, LK[s], 1);
        sw_section(gx, y, gw, bh, "PATTERN");
        {   const int nx = gx + 34, cw = (gw - 34 - 8) / K_STEPS, rhh = (bh - 22) / K_LANES, gy = y + 16;
            for (l = 0; l < K_LANES; l++) {
                const int on = l == a->cur_lane[t], ry = gy + l * rhh;
                const uint32_t lit = on ? SK_ARC : k_mix(SK_PANEL, SK_ARC, 55), off = k_mix(SK_PANEL, 0x000000, on ? 30 : 20);
                g_text(&cv, gx + 8, ry + (rhh - g_text_h(GF_S)) / 2, GF_S, en->lanes[l] ? en->lanes[l] : "", on ? SK_INK : k_mix(SK_PANEL, SK_INK, 55));
                for (s = 0; s < K_STEPS; s++) {
                    const int hit = (a->lane_on[t][l] >> s) & 1, cx = nx + s * cw;
                    g_rrect(&cv, cx + 1, ry + 1, cw - 3, rhh - 3, 2, hit ? lit : off);
                }
                sw_lane_rows[l] = (KRect){gx, ry, gw, rhh};
            }
            for (s = 1; s < 4; s++) g_fill(&cv, nx + 4 * s * cw - 2, gy, 1, K_LANES * rhh, k_mix(SK_PANEL, SK_INK, 25));   /* the beats */
            if (a->playing) g_fill(&cv, nx + a->step % K_STEPS * cw + 1, gy - 4, cw - 3, 2, SK_INK);   /* the step playing: a mark over its column */
        }
    }
    return sw_end();
}
/* the Switch's synth (a large screen, no knobs: K_LAY_SYNTH_ALL): the engine on one panel, as a hardware synth's: the bar (the
 * engine, the preset), then its rows of four as sections, three across: SOUND's above, EDIT's below; each control the knob,
 * switch or selector its descriptor says, and each a target for the pad and the finger */
int switch_synth_view(KApp *a)
{
    const int t = a->track, wx = whole.x, wy = whole.y, mx = k_track_missing(a, t);
    const KEngine *en = k_engine_at(a->sound[t]);
    int r, c, rows, band;
    if (!(a->lay & K_LAY_SYNTH_ALL) || a->page != K_PAGE_SOUND) return 0;
    if (!mx) switch (en->uid) {   /* (the engines drawn for it; the rest, a plugin's: sections) */
    case K_UID('K', 'A', 'N', 'L'): return switch_view_kanalog(a);
    case K_UID('K', 'P', 'H', 'S'): return switch_view_kphase(a);
    case K_UID('K', 'T', 'R', 'I'): return switch_view_ktrio(a);
    case K_UID('K', 'F', 'M', '2'): return switch_view_kfm2(a);
    case K_UID('K', 'L', 'O', 'F'): return switch_view_klofi(a);
    case K_UID('K', 'E', 'N', 'S'): return switch_view_kensemble(a);
    case K_UID('K', 'O', 'G', 'N'): return switch_view_korgan(a);
    case K_UID('D', 'R', 'U', 'M'): return switch_view_drum(a);
    default: break;
    }
    wide_head(a, K_PAGE_SOUND, 0, mx, en, t);
    if (mx) return 1;
    rows = (en->nparams + 3) / 4;
    sk_cw = 46; sk_kr = 14; sk_gap = 3;
    for (band = 0; band < 2; band++) {   /* SOUND's rows, then EDIT's */
        const int r0 = band ? (en->page2 + 3) / 4 : 0, r1 = band ? rows : (en->page2 + 3) / 4, n = r1 - r0;
        const int by = wy + 34 + band * ((whole.h - 40) / 2), bh = (whole.h - 40) / 2 - 6;
        const int gap = 8, sw = n > 0 ? (whole.w - 20 - (n < 3 ? 2 : n - 1) * gap) / (n < 3 ? 3 : n) : 0;
        for (r = r0; r < r1; r++) {
            const int sx = wx + 10 + (r - r0) * (sw + gap);
            const char *title = band == 0 && r < 3 ? en->page_title[r] : "";
            char tb[16];
            g_rrect_frame(&cv, sx, by, sw, bh, 6, k_mix(SK_PANEL, SK_OUT, 60), k_mix(SK_PANEL, 0x000000, 10));
            if (!title || !title[0]) { snprintf(tb, sizeof tb, band ? "EDIT %d" : "%d", r - r0 + 1); title = tb; }
            g_text(&cv, sx + 8, by + 3, GF_S, title, k_mix(SK_PANEL, SK_INK, 70));
            for (c = 0; c < 4; c++) {
                const int i = r * 4 + c;
                if (i >= en->nparams || (band == 0 && i >= en->page2)) break;
                {   const int stack = 15 + 4 + 2 * sk_kr + sk_gap + 15, ly = by + 18 + (bh - 18 - stack) / 2;   /* (label, knob, value: centred) */
                    sk_control(a, K_PAGE_SOUND, i, sx + sw * (2 * c + 1) / 8, ly, ly + 19 + sk_kr, ui_item_name(a, K_PAGE_SOUND, i), 1); }
            }
        }
    }
    sk_cw = 56; sk_kr = 10; sk_gap = 1;
    return 1;
}
/* ORGAN on a 320 x 180 screen: the bars in their well at the left, ROTARY, VIB / CHO, PERC one under another at the right, each
 * label at the left of its control */
int synth_view_korgan_wide(KApp *a)
{
    static const char *const ROT[3] = {"0", "S", "F"}, *const PRC[3] = {"0", "2", "3"};
    const int row = a->win / 4, wx = whole.x, wy = whole.y, y0 = wy + 30, y1 = whole.y + whole.h - 4, sp = 22;
    const int ww = 8 * sp + 16 + 8, rx = wx + 7 + ww + 6, rw = whole.x + whole.w - 7 - rx;   /* the well's width; the right column */
    int k;
    wide_head(a, K_PAGE_SOUND, row, 0, k_engine_at(a->sound[a->track]), a->track);
    g_rrect_frame(&cv, wx + 7, y0, ww, y1 - y0, 5, 0x0B0806, KO_WELL);
    g_hline_a(&cv, wx + 10, y0 + 1, ww - 6, 0x000000, 120);
    for (k = 0; k < 9; k++) ko_drawbar(a, 3 + k, wx + 7 + 4 + 8 + k * sp, y0 + 3 + (y1 - y0 - 90) / 2, 1);
    {   /* three rows: a switch, the box, a switch; the controls right-aligned to the panel's edge */
        const int tw = 52, t1 = y0 + 8, t2 = y0 + 46, t3 = y0 + 76;   /* (a switch: its track 12, its names under it to +29) */
        sk_switch_at(a, K_PAGE_SOUND, 0, rx, t1 - 2, rx + rw - tw, t1, tw, "Rotary", ROT, 1);
        ko_vibcho_at(a, 1, rx, t2 + 2, rx + rw - tw, t2, tw, 20);
        sk_switch_at(a, K_PAGE_SOUND, 2, rx, t3 - 2, rx + rw - tw, t3, tw, "Perc", PRC, 1);
    }
    return 1;
}
#else   /* (the FM-1, 240 x 240: neither the Switch's panels nor the 320 x 180 ORGAN) */
int switch_synth_view(KApp *a) { (void)a; return 0; }
#endif
#ifndef KREISEL_BITMAP_FONT   /* (a 320 x 180 screen only: not in the FM-1's build) */
/* a wide screen (320 x 180): SOUND, EDIT and LFO as the engine's panel, from its descriptor (every engine, a plugin too, in its skin).
 * A bar over it (SOUND: the engine, "< NAME >"; EDIT and LFO: the page), then two rows of four at a time, the row in hand bright
 * and the next one dim under it (the last row: the one before over it), small knobs, their labels and values apart; the row's
 * number at the left. */
/* one oscillator's wave (a scope each): RETRO OSC 1, OSC 2, SUB (a square an octave down); PHASE LINE 1, LINE 2;
 * TRIO W1..W3 at their intervals. n: how many the engine has (0: none) */
static int osc_count(const KEngine *en)
{
    return en->uid == K_UID('K', 'A', 'N', 'L') || en->uid == K_UID('K', 'T', 'R', 'I') ? 3 : en->uid == K_UID('K', 'P', 'H', 'S') ? 2 : 0;
}
static int osc_one(const KApp *a, int o, uint32_t t)   /* oscillator o's sample (Q16) at t cycles (Q16) */
{
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4;
    const uint32_t uid = k_engine_at(a->sound[a->track])->uid;
    if (uid == K_UID('K', 'P', 'H', 'S'))
        return o ? sk_wave_i(2, P[8], tmul16(t, semis16(P[9])), dcw16(P[10])) : sk_wave_i(2, P[4], t, dcw16(P[6]));
    if (uid == K_UID('K', 'T', 'R', 'I')) return sk_wave_i(1, P[4 + o], o ? tmul16(t, semis16(P[o - 1])) : t, 0);
    return o == 2 ? sk_wave_i(0, 1, t >> 1, 0) : sk_wave_i(0, P[4 + 2 * o], t, 0);   /* (SUB: SQR, an octave down) */
}
static void osc_scopes(const KApp *a, const KEngine *en, int x, int y, int w, int h)   /* the scopes one under the other, each wide */
{
    static const char *const RET[3] = {"Osc 1", "Osc 2", "Sub"}, *const PHS[2] = {"Line 1", "Line 2"}, *const TRI[3] = {"Osc 1", "Osc 2", "Osc 3"};
    const int n = osc_count(en), gap = 3, bh = (h - (n - 1) * gap) / n;
    const char *const *L = en->uid == K_UID('K', 'P', 'H', 'S') ? PHS : en->uid == K_UID('K', 'T', 'R', 'I') ? TRI : RET;
    int o, q;
    for (o = 0; o < n; o++) {
        const int by = y + o * (bh + gap), tagw = g_text_w(GF_S, L[o]) + 10, wx0 = x + tagw, ww = w - tagw - 4;
        int p16[2 * 49];
        g_rrect_frame(&cv, x, by, w, bh, 3, sk.lcd_edge, sk.lcd);
        g_text(&cv, x + 5, by + (bh - g_text_h(GF_S)) / 2, GF_S, L[o], k_mix(sk.lcd, SK_LCD_WAVE, 70));   /* (its name at the left, the wave beside it) */
        for (q = 0; q <= 48; q++) {   /* (two cycles) */
            int v = osc_one(a, o, (uint32_t)q * 65536u / 24u);
            v = v > 65536 ? 65536 : v < -65536 ? -65536 : v;
            p16[2 * q] = wx0 * 16 + q * ww * 16 / 48;
            p16[2 * q + 1] = ((by + bh / 2) * 16 * 65536 - v * (bh / 2 - 4) * 16) >> 16;
        }
        g_curve16(&cv, p16, 49, 17, SK_LCD_WAVE);
    }
}
int wide_panel_view(KApp *a)
{
    const int wx = whole.x, wy = whole.y, row = a->win / 4, t = a->track, mx = k_track_missing(a, t);
    const KPage pg = a->page;
    const KEngine *en = k_engine_at(a->sound[t]);
    const int n = pg == K_PAGE_LFO || pg == K_PAGE_ENV1 ? k_page_count(a, pg) : pg == K_PAGE_EDIT ? (mx ? 0 : en->nparams - en->page2) : (mx ? 0 : en->page2);
    const int pitch = 56, vis = whole.h - 39 >= 3 * pitch - 8 ? 3 : 2;   /* (the rows in sight: three where the screen is tall, the H700's) */
    const int rows = (n + 3) / 4, r0 = row + vis > rows ? (rows - vis < 0 ? 0 : rows - vis) : row;
    int k, c, pk;
    if (cv.w < 300 || cv.w > 400 || cv.h > 240 || (pg != K_PAGE_SOUND && pg != K_PAGE_EDIT && pg != K_PAGE_LFO && !(pg == K_PAGE_ENV1 && drum_track(a)))) return 0;
    if ((pg == K_PAGE_SOUND || pg == K_PAGE_EDIT) && en->uid == K_UID('K', 'V', 'E', 'C'))
        return 0; /* both VECTOR pages share the larger, labelled host XY preview */
    if (pg == K_PAGE_SOUND && en->uid == K_UID('K', 'O', 'G', 'N') && !mx)
        return 0; /* ORGAN's SOUND: its bars and switches (synth_view_korgan_wide) */
    if ((pg == K_PAGE_SOUND || pg == K_PAGE_EDIT) && !mx && sk_geom()) {   /* the built-in engines: the FM-1's own views, laid to this screen */
        static const uint32_t OWN[7] = {K_UID('K', 'A', 'N', 'L'), K_UID('K', 'L', 'O', 'F'), K_UID('K', 'T', 'R', 'I'), K_UID('K', 'P', 'H', 'S'),
                                        K_UID('K', 'O', 'G', 'N'), K_UID('K', 'F', 'M', '2'), K_UID('D', 'R', 'U', 'M')};
        for (k = 0; k < 7; k++) if (en->uid == OWN[k]) return 0;
    }
    wide_head(a, pg, row, mx, en, t);
    {   /* the FM-1's picture of the page, at the left, where the engine has one ("not ported"): EDIT the filter's
         * response, SOUND FM2's, PHASE's or LOFI's wave, LFO its wave; the knobs closer at the right of it */
        const uint32_t uid = en->uid;
        int kt = -1, kc = -1, kr = -1, q;
        pk = 0;
        if (!mx && pg == K_PAGE_EDIT && drum_track(a)) pk = 7;   /* DRUM: the kit's eight lanes, each its piece */
        else if (!mx && pg == K_PAGE_EDIT) {
            for (q = 0; q < en->nparams; q++) {
                const char *l = en->edit[q].label;
                if (!strcmp(l, "CUT")) kc = q; else if (!strcmp(l, "RES")) kr = q; else if (!strcmp(l, "TYPE") || !strcmp(l, "MODE")) kt = q;
            }
            pk = kt >= 0 && kc >= 0 && kr >= 0;
        } else if (!mx && pg == K_PAGE_SOUND)
            pk = uid == K_UID('K', 'F', 'M', '2') ? 2 : uid == K_UID('K', 'P', 'H', 'S') ? 3 : uid == K_UID('K', 'L', 'O', 'F') ? 4 :
                 uid == K_UID('K', 'A', 'N', 'L') || uid == K_UID('K', 'T', 'R', 'I') ? 6 : 0;
        else if (pg == K_PAGE_LFO && !drum_track(a)) pk = 5;
        if (pk) {
            const int rh2 = whole.h - 39 < 2 * pitch - 8 ? whole.h - 39 : 2 * pitch - 8, pw = pk == 4 ? 88 : 116, ph = 74, px = wx + 10, py = wy + 31 + (rh2 - ph) / 2;   /* (wide, not
                                                 * tall: a wave reads across; in the middle of the two rows) */
            const KVal *P = a->params[t][K_PAGE_SOUND] + 4;
            if (pk == 1) sw_filter(en, P, kt, kc, kr, px, py, pw, ph);
            else if (pk == 7) {   /* (under the kit's one row of knobs, the whole width: four lanes a line) */
                const KRect r = sw_screen(wx + 10, wy + 86, whole.w - 20, whole.h - 92 < 44 ? whole.h - 92 : 44, 0, 0);
                for (q = 0; q < K_LANES; q++) {
                    char b[16];
                    snprintf(b, sizeof b, "%d %s", q + 1, en->edit[0].names[k_param_clamp(&en->edit[0], a->lane_p[t][q][0])]);
                    g_text(&cv, r.x + 8 + (q & 3) * (r.w - 16) / 4, r.y + 4 + (q / 4) * ((r.h - 8) / 2), GF_S, b,
                           q == a->cur_lane[t] ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 70));
                }
            }
            else if (pk == 5) {
                const KVal *L = a->params[t][K_PAGE_LFO];
                const KRect r = sw_screen(px, py, pw, ph, "LFO", 0);
                lfo_screen16(L, r.x + 5, r.w - 10, r.y + 14 + (r.h - 18) / 2, (r.h - 22) / 2);
            } else
            {
                if ((pk == 3 || pk == 6) && osc_count(en)) osc_scopes(a, en, px, wy + 31, pw, rh2);   /* (an oscillator each) */
                else if (pk == 4) {   /* LOFI: the FM-1's console scope, red blocks on black */
                    g_rrect(&cv, px, py, pw, ph, 3, 0x6A6A6A); g_rrect(&cv, px + 1, py + 1, pw - 2, ph - 2, 2, 0x000000);
                    g_pix8(&cv, px + 5, py + 5, "CHIP", lofi_dim(NES_WHITE, 1), 1);
                    lofi_scope(P, px + 6, py + 18, pw - 12, ph - 24, 0xF83800);
                }
                else sw_wave(sw_screen(px, py, pw, ph, pk == 2 ? "OUT" : "CHIP", 0), pk == 2 ? sw_fm_out : sw_lofi_wave, a);
            }
        }
    }
    if (pk == 7) pk = 0;   /* (the kit's: the knobs as they were) */
    sk_cw = pk ? 42 : 62; sk_kr = cv.h < 180 ? 7 : 8; sk_gap = cv.h < 180 ? 3 : 4;
    for (k = 0; k < vis && r0 + k < rows; k++) {
        const int sh = cv.h < 180;   /* (a shorter screen, the Zero's 170: smaller knobs, nearer their labels, the rows closer) */
        const int r = r0 + k, ly = wy + 32 - sh + k * (pitch - 4 * sh), ky = ly + 25 - sh, lit = r == row || pg == K_PAGE_SOUND || pg == K_PAGE_EDIT;
        const int kx0 = pk == 4 ? wx + 104 : pk ? wx + 134 : wx + 26, kw = pk == 4 ? whole.w - 110 : pk ? whole.w - 142 : 0;
        char num[4];
        snprintf(num, sizeof num, "%d", r + 1);
        if (!pk) g_text(&cv, wx + 12, ky - g_text_h(GF_S) / 2, GF_S, num, lit ? SK_ARC : k_mix(SK_PANEL, SK_INK, 35));
        if (k) g_fill(&cv, kx0, ly - 3, wx + whole.w - 10 - kx0, 1, SK_OUT);
        for (c = 0; c < 4; c++) {
            const int i = r * 4 + c, cx = pk ? kx0 + (2 * c + 1) * kw / 8 : wx + 62 + c * 72;
            if (i >= n) break;
            if (pk == 4) {   /* LOFI: the FM-1's console screen, its rows' colours (PULSE red, MOTION yellow, NOISE cyan) */
                static const char *const LL[12] = {"WAVE", "DUTY", "CRSH", "SRATE", "SWP", "VIB", "ARP", "RATE", "NOISE", "LEVEL", "COLOR", "TONE"};
                static const uint32_t LC[3] = {0xF83800, 0xF8B800, 0x3CBCFC};
                lofi_item_small(a, pg, i, cx, ly - 2, i < 12 ? LL[i] : ui_item_name(a, pg, i), LC[r % 3], lit);
            } else sk_control(a, pg, i, cx, ly, ky, ui_item_name(a, pg, i), lit);
        }
    }
    sk_cw = 56; sk_kr = 10; sk_gap = 1;
    return 1;
}
#else
int wide_panel_view(KApp *a) { (void)a; return 0; }   /* (the FM-1: never a wide screen) */
#endif
