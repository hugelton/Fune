/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM-1's engine skins (their colours, knobs, pills, little screens, waves, the engine's bar) and the views of
 * ANALOG, LOFI and ORGAN's parts drawn with them. */
#include "draw_int.h"
/* ANALOG's own view (Kreisel's, KANL), on the 240 x 240 screen: as drawn in Kreisel.sketch, "SOUND - Analog". The preset over a
 * panel in its wooden frame; in it the engine's bar (K1: "< ANALOG >"), then PITCH and the two oscillators' waves (K2 .. K4, the
 * waves in little screens), then DETUNE, the two levels and NOISE (K1 .. K4 of the second row) ("SOUND - Analog (3 rows)"). The row the knobs hold is bright,
 * the others faint; the third row SYNC. ANALOG's filter is on EDIT (VCF, VCA). */
KSkin sk;
void sk_skin(const KApp *a)
{
    sk_app = a;
    static const KSkin ANALOG = {0x141414, 0x000000, 0x1E1E1E, 0x4A4A4A, 0x5C5C5C, 0xE6E6E6, 0xF5F5F7, 0xF5F5F7, 0x0C0C0C, 0x000000, 0x4A4A4A, 0xFF9100, 1, 0};
    static const KSkin TRIO = {0xECEAE4, 0xBDBDB8, 0xA3A6AB, 0x7E8186, 0xCFCFCA, 0xC02828, 0xC02828, 0x2A2A2A, 0x5F6367, 0xC9CFD1, 0x9AA0A3, 0x2F3437, 0, 1};
    static const KSkin PHASE = {0x252A35, 0x0E1116, 0x3A3D45, 0x55585F, 0x787A80, 0x4588ED, 0xF5F5F7, 0xF5F5F7, 0x22262F, 0x000000, 0x55585F, 0xF2D94A, 0, 0};
    static const KSkin LOFI = {0x000000, 0xFCFCFC, 0x2A2A2A, 0x6A6A6A, 0x6A6A6A, 0xF83800, 0xFCFCFC, 0xFCFCFC, 0x000000, 0x000000, 0xFCFCFC, 0xFCFCFC, 0, 0};
    static const KSkin ORGAN = {0x6A4127, 0x24150B, 0x3B2416, 0x8A6040, 0x9C7452, 0xF2E3C2, 0xF2E3C2, 0xF2E3C2, 0x2E1B0F, 0x1A0F08, 0x8A6040, 0xFFB347, 2, 0};
    static const KSkin FM2 = {0x252A35, 0x0E1116, 0x3A3D45, 0x55585F, 0x787A80, 0x69CCB5, 0xF5F5F7, 0xF5F5F7, 0x22262F, 0x000000, 0x55585F, 0x6BE1C1, 0, 0};
    static const KSkin ENSEMBLE = {0xD4CCB8, 0x6A5C49, 0xE1D9C9, 0x9C8F75, 0xB5A98F, 0x566544, 0x304136, 0x304136, 0xA39B83, 0xD8DCBD, 0x9C8F75, 0x566544, 0, 0};
    static const KSkin DRUM = {0x141414, 0x000000, 0x1E1E1E, 0x4A4A4A, 0x5C5C5C, 0xE6E6E6, 0xF5F5F7, 0xF5F5F7, 0x0C0C0C, 0x000000, 0x4A4A4A, 0xFF9100, 0, 0};   /* ANALOG's, no wood */
    static const KSkin OTHER = {0x252A35, 0x0E1116, 0x3A3D45, 0x55585F, 0x787A80, 0x4588ED, 0xF5F5F7, 0xF5F5F7, 0x22262F, 0x000000, 0x55585F, 0xFF9100, 0, 0};
    uint32_t uid = k_track_missing(a, a->track) ? 0u : k_engine_uid(a->sound[a->track]);   /* (a missing plugin: the plain skin) */
    sk = uid == K_UID('K', 'A', 'N', 'L') ? ANALOG : uid == K_UID('K', 'T', 'R', 'I') ? TRIO : uid == K_UID('K', 'P', 'H', 'S') ? PHASE : uid == K_UID('K', 'L', 'O', 'F') ? LOFI : uid == K_UID('K', 'O', 'G', 'N') ? ORGAN : uid == K_UID('K', 'E', 'N', 'S') ? ENSEMBLE : uid == K_UID('K', 'F', 'M', '2') ? FM2 : uid == K_UID('D', 'R', 'U', 'M') ? DRUM : OTHER;
}
#define SK_FACE sk.face
#define SK_EDGE sk.edge
#define SK_RING sk.ring
#define SK_WOOD_HI 0x7A5235
int sk_kr = 10;   /* a knob's radius: 10 on 240, smaller where two pages share a screen */
/* the engines' views' geometry (draw_int.h): the FM-1's numbers as they were hand-laid */
int sg_x0 = 33, sg_cw = 56, sg_y0 = 49, sg_rp = 52, sg_kd = 27, sg_wd = 16, sg_wh = 34, sg_ww = 48, sg_bar_y = 17, sg_bar_h = 30, sg_mid = 16,
    sg_lane_h = 26, sg_fm1 = 1;
int sk_gap = 1;   /* from the knob's foot to its value */
uint32_t ui_knob_stat[3];   /* knobs drawn, on flat ground, copied (a platform's diagnostics) */
static void sk_knob(int cx, int cy, int pct, int bip, int lit, int hot)
{
    const int r = sk_kr, x0 = cx - r, y0 = cy - r, w = 2 * r + 1, h = 2 * r + 3;   /* (odd: centred on cx + .5, as its ring and mark; even, it sat
                                                                                   * half a pixel up and left of its gauge) */
    const uint32_t edge = lit ? SK_EDGE : k_mix(SK_PANEL, SK_EDGE, 50), face = lit ? SK_FACE : k_mix(SK_PANEL, SK_FACE, 50);
    /* its disc and shadow: on a flat ground they are always the same pixels, so a few are kept and copied (a knob's costliest part
     * on the FM-1); on wood or anything else under it they are drawn */
    static struct { uint32_t edge, face; gpix_t bg; int r; gpix_t px[23 * 25]; } kc[4];
    static int knext;
    gpix_t bg = 0;
    int flat = cv.s <= 1 && x0 >= 0 && y0 >= 0 && x0 + w <= cv.w && y0 + h <= cv.h && w * h <= 23 * 25, k, j, i;   /* (a scaled canvas: drawn) */
    if (flat) {
        bg = cv.px[y0 * cv.w + x0];
        for (j = 0; j < h && flat; j++) for (i = 0; i < w; i++) if (cv.px[(y0 + j) * cv.w + x0 + i] != bg) { flat = 0; break; }
    }
#ifdef G_RECORD
    flat = 0;   /* (the shapes are wanted: tools/ui_sketch, nothing copied) */
#endif
    ui_knob_stat[0]++; ui_knob_stat[1] += flat;
    for (k = 0; flat && k < 4; k++)
        if (kc[k].r == r && kc[k].edge == edge && kc[k].face == face && kc[k].bg == bg) {
            for (j = 0; j < h; j++) memcpy(&cv.px[(y0 + j) * cv.w + x0], &kc[k].px[j * w], (size_t)w * sizeof(gpix_t));
            ui_knob_stat[2]++;
            break;
        }
    if (!flat || k == 4) {
        if (cv.s > 1) g_shadow(&cv, x0, y0, w, w, r, 0, 1, 2, 0x000000, 40);   /* (a dense screen: a soft shadow, not a disc moved down) */
        else g_rrect_a_under(&cv, x0, y0 + 2, w, w, r, 0x000000, 85, x0, y0, w, w, r);   /* its shadow (under the knob, not blended where it hides it) */
        g_rrect_frame(&cv, x0, y0, w, w, r, edge, face);
        if (flat) {   /* kept for the next on this ground */
            k = knext; knext = (knext + 1) & 3;
            kc[k].r = r; kc[k].edge = edge; kc[k].face = face; kc[k].bg = bg;
            for (j = 0; j < h; j++) memcpy(&kc[k].px[j * w], &cv.px[(y0 + j) * cv.w + x0], (size_t)w * sizeof(gpix_t));
        }
    }
    if (ui_mark) ui_mark(25);
    {   const uint32_t groove = k_mix(k_mix(SK_PANEL, 0x000000, 10), SK_INK, lit ? 16 : 8);   /* (a desktop: the gauge's track a groove in its section) */
        const uint32_t ring = (prof->input & K_IN_MOUSE) ? groove : lit ? SK_RING : k_mix(SK_PANEL, SK_RING, 40);
        g_knob(&cv, cx, cy, r + 2, pct, bip, hot ? SK_ARC : lit ? SK_ARC : k_mix(SK_PANEL, SK_ARC, 35), ring, lit ? sk.mark : k_mix(SK_PANEL, sk.mark, 40)); }
}
static int sk_pct(const KParamDesc *d, int v)
{
    if (d->fmt == KF_ENUM) { int nn = 0; while (d->names && d->names[nn]) nn++; return nn > 1 ? (k_param_clamp(d, v) - d->min) * 100 / (nn - 1) : 0; }
    return d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
}
/* the descriptor of item i of a synth page (SOUND, EDIT: the engine's; the host's pages have none: 0 .. 100) */
static const KParamDesc *sk_desc(const KApp *a, KPage pg, int i)
{
    const KEngine *en = k_engine_at(a->sound[a->track]);
    int k = pg == K_PAGE_SOUND ? i : pg == K_PAGE_EDIT ? en->page2 + i : -1;
    if (pg == K_PAGE_LFO) return k_page_desc(K_PAGE_LFO) + (i >= 0 && i <= 8 ? i : 0);   /* (8: a drum track's LANE) */
    return k >= 0 && k < en->nparams ? &en->edit[k] : 0;
}
/* a control of the panel: its label, a knob, its value under it. Its kind follows its descriptor: a switch for on / off, places
 * marked round the knob for a few names, a knob from the middle for a value either way (a destination, a semitone) */
int sk_base = -1;   /* the Switch's SOUND: a control of ENV / LFO drawn on it is the SOUND item sk_base + i (k_synth_all_sub) */
int sk_side = 0;   /* a control's value beside its knob, not under it (a short screen: the Zero's); 0 on the FM-1 */
int sk_cw = 56;   /* a control's width (its label, its value, where it is hit): 56 on 240, narrower where two pages share a screen */
/* the FM-1's row in hand (SELECT picks it, the four knobs turn it): its labels in colour, the rest as they are (not on the Switch,
 * where every control is a target) */
static int sk_in_row(const KApp *a, int ti) { const int r0 = (a->win / 4) * 4; return !(a->lay & K_LAY_SYNTH_ALL) && ti >= r0 && ti < r0 + 4; }
/* the row in hand's colour: the track's own (its colour on HOME and the mixer), with ink dark or light on it as it is light or dark */
static int sk_light(void) { return ((sk.panel >> 16 & 255) + (sk.panel >> 8 & 255) + (sk.panel & 255)) > 3 * 160; }   /* (TRIO's pale panel) */
static uint32_t sk_row_ink(void) { return sk_light() ? 0x2A2A2A : th.text; }   /* the row in hand: the app's mark, white; on a pale skin dark
                                                 * (white on white was 1.08:1: the a11y audit 2026-11-12) */
uint32_t sk_row_text(void) { return sk_light() ? 0xF5F5F7 : 0x101010; }   /* the ink on the mark */
/* the pill under a label of the row in hand: at px, py, pw wide (its label tw wide, in a box of bw) */
void sk_pill(int px, int py, int pw, int tw, int bw, const char *label)
{
    REC("pill", I_(px, py, pw, 13, tw, bw), I_(sk_row_ink()), label);
    g_rrect(&cv, px, py, pw, 13, 4, sk_row_ink());
}
/* a control's label: the row in hand's on a pill of the track's colour (a row told apart on any skin, a grey one too) */
static void sk_label(const KApp *a, int ti, int x, int y, int w, const char *label, uint32_t ink)
{
    if (sk_in_row(a, ti) && label && label[0]) {
        const int lw = g_text_w(GF_S, label);
        int tw = lw + 8;
        if (tw > w - 4) tw = w - 4 > lw + 4 ? w - 4 : lw + 4;   /* (close controls, their boxes overlapping: 2 px a side) */
        sk_pill(x + (w - tw) / 2, y + 1, tw, lw, w, label);
        g_text_box(&cv, x, y, w, 15, GF_S, label, sk_row_text(), 0);
    } else g_text_box(&cv, x, y, w, 15, GF_S, label, ink, 0);
}
void sk_control(KApp *a, KPage pg, int i, int cx, int ly, int ky, const char *label, int lit)
{
    lit = lit && !mod_unused(a, pg, i);   /* a destination its engine does not read: dim */
    const KParamDesc *d = sk_desc(a, pg, i);
    const char *unit;
    char val[16], vu[24];
    const int ti = sk_base >= 0 ? sk_base + i : i;   /* (the item the focus and the touch know it by) */
    int hot = lit && (hot_param(a, ti) || touched_recent(a, ti)), v = k_param_get(a, pg, i), pct, bip, places = 0, q;
    uint32_t ink = hot ? SK_ARC : lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52);
    const uint32_t lab = !hot && lit && (prof->input & K_IN_MOUSE) ? k_mix(SK_PANEL, SK_INK, 68) : ink;   /* (a desktop: the label a step quieter than its value) */
    ui_item_value(a, pg, i, val, sizeof val, &unit);
    snprintf(vu, sizeof vu, "%s%s", val, d && d->fmt == KF_ENUM ? "" : unit);
    if (d && d->fmt == KF_ENUM && strlen(vu) > 2 && !strchr(vu, '&') && !strchr(vu, '%')) { int k2; for (k2 = 1; vu[k2]; k2++) if (vu[k2] >= 'A' && vu[k2] <= 'Z') vu[k2] = (char)(vu[k2] + 32); }   /* "Tri", "Notch"; LP stays */
    if (d && d->fmt == KF_ONOFF) {   /* a switch: a pill, its knob right and lit when on */
        int on = k_switch_on(d, v);
        sk_label(a, ti, cx - sk_cw / 2, ly, sk_cw, label, lab);
        if (sk_side) {   /* the pill at the left, its name beside it */
            const int px = cx - sk_cw / 2 + 4;
            g_rrect(&cv, px, ky - 6, 24, 12, 6, on ? (lit ? SK_ARC : k_mix(SK_PANEL, SK_ARC, 40)) : k_mix(SK_PANEL, 0x000000, 45));
            g_rrect(&cv, on ? px + 13 : px + 1, ky - 5, 10, 10, 5, lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52));
            g_text_box(&cv, px + 28, ky - 7, cx + sk_cw / 2 - px - 28, 15, GF_S, k_switch_name(d, v), ink, -1);
            k_set_bounds(a, K_NODE_PARAM, ti, (KRect){cx - sk_cw / 2, ly, sk_cw, ky + 8 - ly});
            return;
        }
        g_rrect(&cv, cx - 12, ky - 6, 24, 12, 6, on ? (lit ? SK_ARC : k_mix(SK_PANEL, SK_ARC, 40)) : k_mix(SK_PANEL, 0x000000, 45));
        g_rrect(&cv, on ? cx + 1 : cx - 11, ky - 5, 10, 10, 5, lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52));
        g_text_box(&cv, cx - sk_cw / 2, ky + sk_kr + sk_gap, sk_cw, 15, GF_S, k_switch_name(d, v), ink, 0);
        k_set_bounds(a, K_NODE_PARAM, ti, (KRect){cx - sk_cw / 2, ly, sk_cw, ky + sk_kr + 16 - ly});
        return;
    }
    if (d) { pct = sk_pct(d, v); bip = (d->min < 0 && d->max > 0) || d->fmt == KF_LINK_FENV; if (d->fmt == KF_ENUM) { while (d->names && d->names[places]) places++; if (places > 6 || places < 2) places = 0; } }
    else { pct = v < 0 ? 0 : v > 100 ? 100 : v; { int ii = i; KPage pp = k_item_page(a, pg, &ii); bip = !phase_env_ui(a) && ((pp == K_PAGE_ENV1 && ii >= 4) || (pp == K_PAGE_ENV2 && ii == 3) || (pp == K_PAGE_ENV2 && ii == 4)); } }
    if (ui_mark) ui_mark(20);
    sk_label(a, ti, cx - sk_cw / 2, ly, sk_cw, label, lab);
    if (ui_mark) ui_mark(21);
    if (sk_side) {   /* the knob at the left of the column, its value at its right */
        const int kx = cx - sk_cw / 2 + 4 + sk_kr, vx = kx + sk_kr + 5;
        sk_knob(kx, ky, pct, bip, lit, hot);
        for (q = 0; q < places; q++) {
            const uint32_t ph = (uint32_t)((135 * (places - 1) + 270 * q) * 46603 / (places - 1)) << 8;
            const int rr = sk_kr + 3, dx = (g_isin(ph + 0x40000000u) * rr + 8192) >> 14, dy = (g_isin(ph) * rr + 8192) >> 14;
            g_fill(&cv, kx + dx, ky + dy, 1, 1, lit ? SK_RING : k_mix(SK_PANEL, SK_RING, 40));
        }
        g_text_box(&cv, vx, ky - 7, cx + sk_cw / 2 - vx, 15, GF_S, vu, ink, -1);
        k_set_bounds(a, K_NODE_PARAM, ti, (KRect){cx - sk_cw / 2, ly, sk_cw, ky + sk_kr + 2 - ly});
        return;
    }
    sk_knob(cx, ky, pct, bip, lit, hot);
    if (ui_mark) ui_mark(22);
    for (q = 0; q < places; q++) {   /* a rotary switch's places */
        const uint32_t ph = (uint32_t)((135 * (places - 1) + 270 * q) * 46603 / (places - 1)) << 8;   /* 135 + 270 q / (places - 1) degrees (2^32 a turn) */
        const int rr = sk_kr + 6, dx = (g_isin(ph + 0x40000000u) * rr + 8192) >> 14, dy = (g_isin(ph) * rr + 8192) >> 14;   /* (rounded) */
        g_fill(&cv, cx + dx, ky + dy, 2, 2, lit ? SK_RING : k_mix(SK_PANEL, SK_RING, 40));
    }
    if (ui_mark) ui_mark(23);
    g_text_box(&cv, cx - sk_cw / 2, ky + sk_kr + sk_gap, sk_cw, 15, GF_S, vu, ink, 0);
        if (ui_mark) ui_mark(24);
    k_set_bounds(a, K_NODE_PARAM, ti, (KRect){cx - sk_cw / 2, ly, sk_cw, ky + sk_kr + 16 - ly});
}
/* ---- the pictures' curves, in integers on every device (the FM-1 has no FPU: one formula for all). Each picture computes its points
 * in 1/16 px; values in Q16 (1.0 = 65536). */
/* fract(sinf(n 12.9898) 43758.5453), Q16, n 0..40: the sample-and-hold steps (as the float pictures had them) */
static const uint16_t SH_HASH[41] = {
    0, 61696, 5632, 38656, 24064, 22016, 30208, 64736, 11456, 28160, 50176, 53504, 21248, 1920, 10112, 29312, 13568, 53504, 46336, 14080, 15872,
    43520, 31232, 61952, 35072, 57856, 32512, 11520, 46336, 26944, 65280, 0, 41728, 37632, 57600, 27904, 32384, 4096, 25088, 43648, 65024};
static int sh_hash16(int n) { return SH_HASH[n < 0 ? 0 : n % 41]; }
/* the LFO's wave (as Felucca 2.0's LFO page draws it): kind 0 SIN 1 TRI 2 SAW 3 SQR 4 S&H (a value a cycle) at phase f (Q16) of
 * cycle cyc; Q16 */
int lfo_wave16(int kind, int cyc, int f)
{
    return kind == 0 ? 4 * g_isin((uint32_t)f << 16) : kind == 1 ? 65536 - 4 * abs(f - 32768) : kind == 2 ? 2 * f - 65536 :
           kind == 3 ? (f < 32768 ? 65536 : -65536) : 2 * sh_hash16(cyc) - 65536;
}
void sk_line16(const int *p16, int n, int lit) { g_curve16(&cv, p16, n, 17, lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62)); }   /* (1.2 px) */
/* a wave's sample for a little screen, t in cycles (Q16): ANALOG's, TRIO's six, PHASE's eight bent by a DCW (dcw 0..65536); Q16 */
int sk_wave_i(int kind, int w, uint32_t t, uint32_t dcw)
{
    const int p = (int)(t & 0xFFFFu);
    if (kind == 1) {   /* TRIO: TRI SAW PLS NOI S&T P&S */
        switch (w) {
        case 0: return p < 32768 ? 4 * p - 65536 : 196608 - 4 * p;
        case 1: return 2 * p - 65536;
        case 2: return p < 32768 ? 58982 : -58982;
        case 3: return sh_hash16((int)(t >> 12)) * 9 / 5 - 58982;   /* (1.8 h - .9) */
        case 4: { const int a = (int)((uint32_t)p * 65535u >> 16), tri = (int)((uint32_t)(p < 32768 ? p : 65536 - p) * 131070u >> 16); return 2 * (a & tri) - 65536; }
        default: return p >= 32768 ? 2 * p - 65536 : -65536;
        }
    }
    if (kind == 2) {   /* PHASE: a cosine through the bent phase */
        const uint32_t d = 32768u - (dcw * 31719u >> 16), up = (uint32_t)p;   /* .5 - .484 dcw */
        uint32_t q;
        #define PDWI(x) ((x) < d ? (x) * 32768u / d : 32768u + ((x) - d) * 32768u / (65536u - d))
        switch (w) {
        case 1: { const uint32_t h = up < 32768u ? 2u * up : 2u * up - 65536u; q = (PDWI(h) >> 1) + (up < 32768u ? 0u : 32768u); } break;
        case 2: q = PDWI(up); if (q > 32768u) q = 32768u; break;
        case 3: { const uint32_t h = (2u * up) & 0xFFFFu; q = PDWI(h); } break;
        case 4: q = up < 32768u ? PDWI(up) : PDWI(up) >> 1; break;
        case 5: case 6: case 7: {
            const uint32_t win = w == 5 ? 65536u - up : w == 6 ? (up < 32768u ? 2u * up : 131072u - 2u * up) : (up < 16384u ? 4u * up : up > 49152u ? (65536u - up) * 4u : 65536u);
            const int c = g_isin(up * (65536u + 7u * dcw) + 0x40000000u);   /* cos of ph (1 + 7 dcw) turns (the product: Q32 turns, wrapping a turn) */
            return (int)((uint32_t)(16384 - c) * win >> 14) - 65536;
        }
        default: q = PDWI(up); break;
        }
        #undef PDWI
        return -4 * g_isin((q << 16) + 0x40000000u);
    }
    switch (w) {   /* ANALOG (SAW SQR TRI SIN P12 P25 P37; 10 SYNC at ratio 1, 11 SUB) */
    case 0: case 10: return 2 * p - 65536;
    case 1: return p < 32768 ? 65536 : -65536;
    case 2: return 65536 - 4 * abs(p - 32768);
    case 3: return 4 * g_isin((uint32_t)p << 16);
    case 4: case 5: case 6: return p < 8192 * (w - 3) ? 65536 : -65536;
    default: return (p < 32768 ? 39322 : -39322) + ((t >> 16) & 1u ? -26214 : 26214);   /* (SUB: its square and the octave's, .6 +- .4) */
    }
}
static int32_t lin24(int v, int32_t sl20, int div)   /* v sl / div in Q24 (sl in Q20, v sl20 < 2^31), rounded */
{
    const int32_t m = v * sl20;
    return (m / div) * 16 + ((m % div) * 16 + div / 2) / div;
}
int lrdiv(int n, int d) { const int q = n / d, r = n % d; return 2 * r > d || (2 * r == d && (q & 1)) ? q + 1 : q; }   /* n / d as lrintf rounds it (n >= 0, d > 0) */
/* a wave on a little screen: its label at the left over it, its name at the right; kind 0 ANALOG, 1 TRIO, 2 PHASE (dcw its DCW, 0..127) */
void sk_wave_w(KApp *a, int i, int x, int ly, int y, int w, const char *label, int lit, int kind, int dcw)
{
    const char *unit;
    char val[16];
    int q, hot = lit && (hot_param(a, i) || touched_recent(a, i)), v = k_param_get(a, K_PAGE_SOUND, i), h = sg_wh;   /* (34 on the FM-1) */
    uint32_t ink = hot ? SK_ARC : lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52);
    ui_item_value(a, K_PAGE_SOUND, i, val, sizeof val, &unit);
    if (val[0] && val[1] != '&') { int k2; for (k2 = 1; val[k2]; k2++) if (val[k2] >= 'A' && val[k2] <= 'Z') val[k2] = (char)(val[k2] + 32); }   /* "Saw" (S&T stays) */
    g_text(&cv, x + 1, ly, GF_S, label, ink);
    g_text(&cv, x + w - 1 - g_text_w(GF_S, val), ly, GF_S, val, ink);
    g_rrect(&cv, x, y, w, h, 3, hot ? SK_ARC : sk.lcd_edge);   /* the little screen */
    g_rrect(&cv, x + 1, y + 1, w - 2, h - 2, 2, sk.lcd);
    {   /* (no FPU: the same points in 1/16 px from integers) */
        int p16[2 * 49];
        const uint32_t d16 = (uint32_t)(dcw < 0 ? 0 : dcw > 127 ? 127 : dcw) * 65536u / 127u;
        for (q = 0; q <= 48; q++) {   /* one and a half of its wave: t = q 1.5 / 48 = q / 32 */
            int yy = sk_wave_i(kind, v, (uint32_t)q * 2048u, d16);
            yy = yy > 65536 ? 65536 : yy < -65536 ? -65536 : yy;
            p16[2 * q] = (x + 7) * 16 + q * (w - 14) * 16 / 48;
            p16[2 * q + 1] = ((y + h / 2) * 16 * 65536 - yy * (h / 2 - 7) * 16) >> 16;
        }
        sk_line16(p16, 49, lit);
    }
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){x, ly, w, y + h - ly});
}
static void sk_wave(KApp *a, int i, int x, int ly, int y, const char *label, int lit)   /* (ANALOG's: 75 wide) */
{
    sk_wave_w(a, i, x, ly, y, 75, label, lit, 0, 0);
}
/* the frame every synth page shares on 240 x 240: the preset over a panel in its wooden frame */
int sk_geom(void)
{
    if (cv.w == 240 && cv.h == 240) {   /* the FM-1: as hand-laid (header C, sk_c: the bar 17 down and 30 tall) */
        sg_x0 = 33; sg_cw = 56; sg_y0 = 49; sg_rp = 52; sg_kd = 27; sg_wd = 16; sg_wh = 34; sg_ww = 48; sg_mid = 16; sg_lane_h = 26;
        sg_bar_y = sk_c ? 17 : 27; sg_bar_h = sk_c ? 30 : 20; sg_fm1 = 1;
        sk_cw = 56; sk_kr = 10; sk_gap = 1; ko_step = 7; sk_side = 0;
        return 1;
    }
#ifndef KREISEL_BITMAP_FONT
    if (cv.w >= 300 && cv.w <= 400 && cv.h >= 160 && cv.h <= 240 && !(prof->input & K_IN_MOUSE)) {   /* a 320-wide screen (the Zero's, the
                                                 * H700's): four columns across, three rows in the height under the bar */
        const int tall = whole.h >= 190;
        sg_fm1 = 0;
        sg_bar_y = 6; sg_bar_h = tall ? 26 : 20;
        sg_cw = (whole.w - 8) / 4; sg_x0 = 4 + sg_cw / 2; sg_ww = sg_cw - 10;
        sg_y0 = sg_bar_y + sg_bar_h + 5;
        sg_rp = (whole.h - sg_y0 - 3) / 3;
        sk_kr = tall ? 9 : 7; sk_gap = tall ? 1 : 0; sk_cw = sg_cw; sk_side = !tall;   /* (short: label, then knob and value side by side) */
        sg_kd = 17 + sk_kr;   /* (the knob clear of the pill over it: ui_pill_check) */
        sg_wd = tall ? 16 : 13; sg_wh = sg_rp - sg_wd - 3; sg_mid = sg_rp / 4; sg_lane_h = sg_rp - sg_wd - 4;
        ko_step = (whole.y + whole.h - 4 - (SK_LY(1) + 6) - 30) / 8;   /* (the bar, its cap and its footage in the well) */
        if (ko_step < 3) ko_step = 3;
        if (ko_step > 7) ko_step = 7;
        return 1;
    }
#endif
    return 0;
}
KRect sk_frame(const KApp *a)
{
    sk_skin(a);
    const int wx = whole.x, wy = whole.y;
    KRect pn = {wx, wy + sg_bar_y - 4, whole.w, whole.h - (sg_bar_y - 4)};   /* (the FM-1's: 13 down with header C, 23 without) */
    g_fill_around(&cv, pic.x, pic.y, pic.w, pic.h, pn.x, pn.y + 8, pn.w, pn.h - 16, th.bg);   /* (not under the panel: it covers itself) */
    if (!sk_c && sg_fm1) preset_line(a, wx + 6, wy, wx + whole.w - 6);
    g_rrect_frame(&cv, pn.x, pn.y, pn.w, pn.h, 8, SK_OUT, SK_PANEL);
    if (sk.wood == 2) {   /* all wood: a grain of streaks along the panel, lighter and darker by a hash of the line */
        int yy;
        for (yy = pn.y + 2; yy < pn.y + pn.h - 2; yy++) {
            uint32_t h = (uint32_t)yy * 2654435761u;
            h ^= h >> 13;
            if ((h & 3u) == 0u) g_hline_a(&cv, pn.x + 2, yy, pn.w - 4, (h & 16u) ? 0x9A6A44 : 0x3A2212, 40 + (int)((h >> 5) & 31u));
        }
    } else if (sk.wood) {   /* wooden cheeks */
        g_rrect(&cv, pn.x + 1, pn.y + 1, 5, pn.h - 2, 3, SK_WOOD); g_fill(&cv, pn.x + 2, pn.y + 2, 1, pn.h - 4, SK_WOOD_HI);
        g_rrect(&cv, pn.x + pn.w - 6, pn.y + 1, 5, pn.h - 2, 3, SK_WOOD); g_fill(&cv, pn.x + pn.w - 5, pn.y + 2, 1, pn.h - 4, SK_WOOD_HI);
    }
    return pn;
}
/* the little screen over the knobs of EDIT, ENV and LFO (where SOUND has its engine and its first row): black, the line in amber */
/* a heading in words, not all capitals (the a11y audit 2026-11-12): "SUB  NOISE" -> "Sub noise", "ENV 1 TO" ->
 * "Env 1 to"; the acronyms stay (LFO DCO DCW DCA FX OP1 OP2 ARP) */
const char *wcase(const char *s)
{
    static char buf[2][32];
    static int at;
    static const char *const KEEP[] = {"LFO", "DCO", "DCW", "DCA", "FX", "FX1", "FX2", "OP1", "OP2", "ARP", "AMP", "MOD", 0};
    char *o = buf[at ^= 1];
    int n = 0, first = 1;
    if (!s) return s;
    while (*s && n < 31) {
        if (*s == ' ') { if (n && o[n - 1] != ' ') o[n++] = ' '; s++; continue; }
        {   const char *w = s; int len = 0, k, keep = 0;
            while (w[len] && w[len] != ' ') len++;
            for (k = 0; KEEP[k]; k++) if ((int)strlen(KEEP[k]) == len && !strncmp(KEEP[k], w, (size_t)len)) keep = 1;
            if (len <= 2) keep = 1;   /* (LP, BP, HP) */
            for (k = 0; k < k_engine_count(); k++) { const char *e = k_engine_at(k)->name; if ((int)strlen(e) == len && !strncmp(e, w, (size_t)len)) keep = 1; }   /* (an engine's name: the owner's call) */
            for (k = 0; k < len && n < 31; k++) {
                const char c = w[k];
                o[n++] = keep || (first && !k) ? c : (c >= 'A' && c <= 'Z' ? (char)(c + 32) : c);
            }
            if (!keep && first && len && w[0] >= 'a' && w[0] <= 'z') o[n - len] = (char)(w[0] - 32);
            first = 0; s += len; }
    }
    o[n] = 0;
    return o;
}
KRect sk_screen(const char *tag, const char *right, int lit)
{
    KRect r = {whole.x + 11, whole.y + sg_bar_y, whole.w - 23, SK_LY(1) - 2 - (whole.y + sg_bar_y)};   /* (the FM-1's: 82 or 72 tall) */
    const uint32_t amber = lit ? SK_LCD_WAVE : k_mix(sk.lcd, SK_LCD_WAVE, 62);
    g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 3, sk.lcd_edge, sk.lcd);
    if (tag) g_text(&cv, r.x + 6, r.y + 3, GF_S, wcase(tag), amber);
    if (right && !strpbrk(right, "0123456789")) right = wcase(right);   /* (words; a value keeps its units) */
    {   int at = 0, n = sk_c && sk_app ? k_page_tabs(sk_app, &at) : 0, k, rx = r.x + r.w - 6;
        if (n > 1) {   /* the page's tabs (1 2: the turns its page key gives) in the screen's corner, on a cushion of their own */
            const int tw = n * 17 - 2, tx = r.x + r.w - 4 - tw, ty = r.y + 3;
            g_rrect(&cv, tx - 3, ty - 2, tw + 6, 18, 5, k_mix(sk.lcd, 0xFFFFFF, 14));
            for (k = 0; k < n && k < 8; k++) {
                char b[4];
                const int on = k == at, bx = tx + k * 17;
                snprintf(b, sizeof b, "%d", k + 1);
                g_rrect(&cv, bx, ty, 15, 14, 3, on ? amber : k_mix(sk.lcd, 0xFFFFFF, 26));
                g_text_box(&cv, bx, ty, 15, 14, GF_S, b, on ? sk.lcd : k_mix(sk.lcd, 0xFFFFFF, 70), 0);
            }
            rx = tx - 9;
        }
        if (right) g_text(&cv, rx - g_text_w(GF_S, right), r.y + 3, GF_S, right, amber);
    }
    return r;
}
/* a page's two rows of knobs, at the heights of SOUND's second and third */
void sk_rows(KApp *a, KPage pg, const char *const *L, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (L[i] && L[i][0]) sk_control(a, pg, i, SK_CX(i & 3), SK_LY(i < 4 ? 1 : 2), SK_KY(i < 4 ? 1 : 2), L[i], 1);   /* (all at full: the rows not in hand are veiled after, sk_row_frame) */
}
/* the engine's bar along the top of SOUND (K1): "< NAME >"; the skin's bar colour, and TRIO's pastel rainbow running off it */
void sk_engine_bar(KApp *a, int row)
{
    const int bx = whole.x + 11, by = whole.y + sg_bar_y, bw = whole.w - 23, bh = sg_bar_h, on = 1, hot = 0;   /* (the engine: ALGORITHM; no knob of the rows) */
    const GFont nf = bh >= 24 ? GF_M : GF_S;   /* (header C: the name larger, the preset's line gone) */
    const uint32_t light = 0xF5F5F7;   /* (every skin's bar is dark) */
    const uint32_t ink = hot ? SK_ARC : on ? light : k_mix(sk.bar, light, 45);
    const char *nm = k_engine_at(a->sound[a->track])->name;
    int tw = g_text_w(nf, nm), cx = bx + bw / 2, q;
    g_rrect_frame(&cv, bx, by, bw, bh, 3, hot ? SK_ARC : k_mix(sk.bar, 0x979797, 30), sk.bar);
    g_hline_a(&cv, bx + 2, by + 1, bw - 4, 0xFFFFFF, 18);
    g_text(&cv, cx - tw / 2, by + (bh - g_text_h(nf)) / 2, nf, nm, ink);
    for (q = 0; q < 10; q++) {   /* the arrows: a solid triangle each side, 6 wide and 10 tall, 12 from the name */
        int span = 6 - (q < 5 ? 5 - q : q - 4), lx = cx - tw / 2 - (bh >= 24 ? 16 : 12), rx = cx + tw / 2 + (bh >= 24 ? 16 : 12);
        if (span < 1) span = 1;
        g_fill(&cv, lx - span, by + (bh - 10) / 2 + q, span, 1, ink);
        g_fill(&cv, rx, by + (bh - 10) / 2 + q, span, 1, ink);
    }
    k_set_bounds(a, K_NODE_SOUND, 0, (KRect){bx, by, bw, bh});
    (void)row;
}
/* a switch standing up, for on / off (ANALOG's SYNC): its label over it, the thumb up when on, the state under it */
void sk_switch_v(KApp *a, KPage pg, int i, int cx, int ly, const char *label)
{
    const int on = k_param_get(a, pg, i) != 0, hot = hot_param(a, i) || touched_recent(a, i), ty = ly + 16, th = 20;   /* (all of it over the next row's labels) */
    const uint32_t ink = hot ? SK_ARC : SK_INK;
    sk_label(a, i, cx - sk_cw / 2, ly, sk_cw, label, ink);
    if (sk_side) {   /* a short screen: lying down, its name beside it (as sk_control's switches) */
        const int px = cx - sk_cw / 2 + 4, ky = ly + sg_kd;
        g_rrect(&cv, px, ky - 6, 24, 12, 6, on ? SK_ARC : k_mix(SK_PANEL, 0x000000, 45));
        g_rrect(&cv, on ? px + 13 : px + 1, ky - 5, 10, 10, 5, SK_INK);
        g_text_box(&cv, px + 28, ky - 7, cx + sk_cw / 2 - px - 28, 15, GF_S, on ? "On" : "Off", ink, -1);
        k_set_bounds(a, K_NODE_PARAM, i, (KRect){cx - sk_cw / 2, ly, sk_cw, ky + 8 - ly});
        return;
    }
    g_rrect(&cv, cx - 6, ty, 12, th, 6, on ? SK_ARC : k_mix(SK_PANEL, 0x000000, 45));
    g_rrect_a(&cv, cx - 5, (on ? ty + 1 : ty + th - 11) + 1, 10, 10, 5, 0x000000, 90);
    g_rrect(&cv, cx - 5, on ? ty + 1 : ty + th - 11, 10, 10, 5, SK_INK);
    g_text_box(&cv, cx - sk_cw / 2, ty + th + 2, sk_cw, 15, GF_S, on ? "On" : "Off", ink, 0);
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){cx - sk_cw / 2, ly, sk_cw, ty + th + 16 - ly});
}
/* ANALOG's SOUND (240 x 240): the engine's bar, then its three rows as the knobs take them: PITCH GLIDE DETUNE and SYNC standing up |
 * the two oscillators, each its wave on a little screen and its level | SUB, its octave, NOISE and its colour */
int synth_view_kanalog(KApp *a)
{
    const int row = a->win / 4;
    static const char *const L1[3] = {"Pitch", "Glide", "Detune"}, *const L3[4] = {"Sub", "Sub Oct", "Noise", "Color"};
    int c;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;   /* (another screen: the vanilla view) */
    sk_frame(a);
    sk_engine_bar(a, row);
    for (c = 0; c < 3; c++) sk_control(a, K_PAGE_SOUND, c, SK_CX(c), SK_LY(0), SK_KY(0), L1[c], 1);
    sk_switch_v(a, K_PAGE_SOUND, 3, SK_CX(3), SK_LY(0), "Sync");
    sk_wave_w(a, 4, SK_WX(0), SK_LY(1), SK_LY(1) + sg_wd, SK_WW, "W1", 1, 0, 0);   /* (a wave in a knob's column: 48 wide) */
    sk_control(a, K_PAGE_SOUND, 5, SK_CX(1), SK_LY(1), SK_KY(1), "Level 1", 1);
    sk_wave_w(a, 6, SK_WX(2), SK_LY(1), SK_LY(1) + sg_wd, SK_WW, "W2", 1, 0, 0);
    sk_control(a, K_PAGE_SOUND, 7, SK_CX(3), SK_LY(1), SK_KY(1), "Level 2", 1);
    for (c = 0; c < 4; c++) sk_control(a, K_PAGE_SOUND, 8 + c, SK_CX(c), SK_LY(2), SK_KY(2), L3[c], 1);
    return 1;
}
/* LOFI's own view: a game console's screen. Black, the pixel font (8 x 8: Press Start 2P's, pixfont8.c), the same places as the
 * other engines' panels: the engine's bar, then three rows at the knobs' heights, a colour a row (PULSE red, MOTION yellow, NOISE
 * cyan); a parameter its name, its value and a gauge of eight blocks. The row the knobs hold bright, the others dim; the one in
 * hand pointed at. Beside the first row (K1's column, under the bar) a little scope of the chip's wave. EDIT: the scope large,
 * then OUTPUT (SRATE STEP LEVEL VEL). */
uint32_t lofi_dim(uint32_t c, int lit) { return lit ? c : k_mix(0x000000, c, 40); }
static void lofi_frame(KApp *a)
{
    const int wx = whole.x, wy = whole.y, row = a->win / 4; (void)row;   /* (the FM-1 build has no use for it) */
    sk_skin(a);
    const int top = sg_bar_y - 4;   /* (the FM-1's: 13 or 23) */
    g_fill_around(&cv, pic.x, pic.y, pic.w, pic.h, wx, wy + top + 8, whole.w, whole.h - top - 16, th.bg);
    if (!sk_c && sg_fm1) preset_line(a, wx + 6, wy, wx + whole.w - 6);
    g_rrect_frame(&cv, wx, wy + top, whole.w, whole.h - top, 8, 0x3A3A3A, 0x000000);
    if (a->page == K_PAGE_SOUND) {   /* the engine's bar: the place and size of every engine's (sk_engine_bar) */
        const int bx = wx + 11, by = wy + top + 4, bw = whole.w - 23, bh = sg_bar_h, hot = 0;
        const char *nm = k_engine_at(a->sound[a->track])->name;
        const uint32_t ink = hot ? 0xF8D878 : lofi_dim(NES_WHITE, 1);
        int tw = g_pix8_w(nm, 1), cx = bx + bw / 2;
        g_rrect_frame(&cv, bx, by, bw, bh, 3, lofi_dim(NES_WHITE, 1), 0x000000);
        g_pix8(&cv, cx - tw / 2, by + (bh - 8) / 2, nm, ink, 1);
        g_pix8(&cv, cx - tw / 2 - 20, by + (bh - 8) / 2, "<", ink, 1);
        g_pix8(&cv, cx + tw / 2 + 12, by + (bh - 8) / 2, ">", ink, 1);
        k_set_bounds(a, K_NODE_SOUND, 0, (KRect){bx, by, bw, bh});
    }
}
static void lofi_item(KApp *a, KPage pg, int i, int cx, int y, const char *label, uint32_t col, int lit)
{
    const KParamDesc *d = sk_desc(a, pg, i);
    const char *unit;
    char val[16], vu[24];
    int hot = lit && (hot_param(a, i) || touched_recent(a, i)), v = k_param_get(a, pg, i), pct, n, q;
    const uint32_t lc = lofi_dim(col, lit), vc = hot ? 0xF8D878 : lofi_dim(NES_WHITE, lit);
    ui_item_value(a, pg, i, val, sizeof val, &unit);
    snprintf(vu, sizeof vu, "%s%s", val, d && (d->fmt == KF_ENUM || d->fmt == KF_ONOFF) ? "" : unit);
    if (d && d->fmt == KF_ONOFF) snprintf(vu, sizeof vu, "%s", d->max > 1 ? k_switch_name(d, v) : v ? "ON" : "OFF");
    pct = d ? sk_pct(d, v) : v;
    n = (pct * 8 + 50) / 100;
    if (sk_in_row(a, i)) {   /* the row in hand: the label on a pill of the track's colour */
        const int lw = g_pix8_w(label, 1);
        sk_pill(cx - lw / 2 - 4, y, lw + 8, lw, 56, label);
        g_pix8(&cv, cx - lw / 2, y + 2, label, sk_row_text(), 1);
    } else g_pix8(&cv, cx - g_pix8_w(label, 1) / 2, y + 2, label, lc, 1);
    if (hot) g_pix8(&cv, cx - g_pix8_w(vu, 1) / 2 - 10, y + 16, ">", vc, 1);   /* the cursor */
    g_pix8(&cv, cx - g_pix8_w(vu, 1) / 2, y + 16, vu, vc, 1);
    for (q = 0; q < 8; q++) g_fill(&cv, cx - 23 + q * 6, y + 31, 4, 5, q < n ? lc : 0x262626);
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){cx - 28, y, 56, 40});
}
/* lofi_item narrower (a column of ~46 px: the Zero's, beside the scope): the name, the value, a gauge of eight 4 px blocks */
void lofi_item_small(KApp *a, KPage pg, int i, int cx, int y, const char *label, uint32_t col, int lit)
{
    const KParamDesc *d = sk_desc(a, pg, i);
    const char *unit;
    char val[16], vu[24];
    int hot = lit && (hot_param(a, i) || touched_recent(a, i)), v = k_param_get(a, pg, i), n, q;
    const uint32_t lc = lofi_dim(col, lit), vc = hot ? 0xF8D878 : lofi_dim(NES_WHITE, lit);
    ui_item_value(a, pg, i, val, sizeof val, &unit);
    snprintf(vu, sizeof vu, "%s%s", val, d && (d->fmt == KF_ENUM || d->fmt == KF_ONOFF) ? "" : unit);
    if (d && d->fmt == KF_ONOFF) snprintf(vu, sizeof vu, "%s", d->max > 1 ? k_switch_name(d, v) : v ? "ON" : "OFF");
    for (q = 0; vu[q]; q++) if (vu[q] >= 'a' && vu[q] <= 'z') vu[q] = (char)(vu[q] - 32);   /* (the chip's font: capitals) */
    n = ((d ? sk_pct(d, v) : v) * 8 + 50) / 100;
    if (sk_in_row(a, i)) {
        const int lw = g_pix8_w(label, 1);
        sk_pill(cx - lw / 2 - 3, y, lw + 6, lw, 46, label);
        g_pix8(&cv, cx - lw / 2, y + 2, label, sk_row_text(), 1);
    } else g_pix8(&cv, cx - g_pix8_w(label, 1) / 2, y + 2, label, lc, 1);
    g_pix8(&cv, cx - g_pix8_w(vu, 1) / 2, y + 15, vu, vc, 1);
    for (q = 0; q < 8; q++) g_fill(&cv, cx - 20 + q * 5, y + 27, 4, 4, q < n ? lc : 0x262626);
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){cx - 23, y, 46, 34});
}
/* the chip's wave (two cycles) in blocks of two pixels: its steps and its bits */
void lofi_scope(const KVal *P, int x0, int y0, int w, int h, uint32_t col)
{
    static const uint32_t DUTY[4] = {0x20000000u, 0x40000000u, 0x80000000u, 0xC0000000u};
    const int bits = 16 - P[2] * 2 - (P[2] >= 7 ? 1 : 0);
    int q, prev = -1;
    for (q = 0; q < w; q += 2) {
        uint32_t ph = (uint32_t)(k_umul((uint32_t)q * 2u, 0xFFFFFFFFu) / (uint32_t)w);
        int32_t s, y;
        switch (P[0]) {
        case 1: { int32_t st = (int32_t)(ph >> 27); s = (st < 16 ? st : 31 - st) * 2184 - 16380; } break;
        case 2: s = (int32_t)(ph >> 28) * 2184 - 16380; break;
        case 3: s = 0; break;   /* OFF: a flat line */
        default: s = ph < DUTY[P[1] & 3] ? 14000 : -14000; break;
        }
        if (bits < 16) s = (s >> (16 - bits)) << (16 - bits);
        y = (y0 + h / 2 - s * (h / 2 - 2) / 16384) & ~1;
        if (prev >= 0 && prev != y) g_fill(&cv, x0 + q, prev < y ? prev : y, 2, (prev < y ? y - prev : prev - y) + 2, col);
        g_fill(&cv, x0 + q, y, 2, 2, col);
        prev = y;
    }
}
int lofi_view_sel(KApp *a)
{
    static const char *const L[12] = {"WAVE", "DUTY", "CRSH", "SRATE", "SWP", "VIB", "ARP", "RATE", "NOISE", "LEVEL", "COLOR", "TONE"};   /* (a row a knob row) */
    static const uint32_t C[3] = {0xF83800, 0xF8B800, 0x3CBCFC};
    const int row = a->win / 4, wx = whole.x; (void)row;   /* (the FM-1 build has no use for it) */
    int rr, c;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    lofi_frame(a);
    for (rr = 0; rr < 3; rr++) {
        const int y = SK_LY(rr) + 1;   /* (the FM-1's: 50 + 52 a row) */
        if (rr) for (c = wx + 12; c < wx + whole.w - 12; c += 4) g_fill(&cv, c, y - 3, 2, 1, 0x2A2A2A);   /* a dotted rule between rows */
        for (c = 0; c < 4; c++) {
            int i = rr * 4 + c;
            if (sg_fm1) lofi_item(a, K_PAGE_SOUND, i, SK_CX(c), y, L[i], C[rr], 1);
            else lofi_item_small(a, K_PAGE_SOUND, i, SK_CX(c), y, L[i], C[rr], 1);
        }
    }
    return 1;
}
int lofi_view_edit(KApp *a)
{
    static const char *const L[3] = {"STEP", "LEVEL", "VEL"};
    const int wx = whole.x, wy = whole.y, row = a->win / 4; (void)row;   /* (the FM-1 build has no use for it) */
    int c;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    lofi_frame(a);
    {   /* the screen of every EDIT (sk_screen): on the FM-1 at 17 or 27, 82 or 72 tall; its scope from 46, 44 tall */
        const int sy = wy + sg_bar_y, sh = SK_LY(1) - 2 - sy, oy = sg_fm1 ? 46 : 21;
        g_rrect(&cv, wx + 11, sy, whole.w - 23, sh, 3, 0x6A6A6A);
        g_rrect(&cv, wx + 12, sy + 1, whole.w - 25, sh - 2, 2, 0x000000);
        g_pix8(&cv, wx + 17, sg_fm1 ? wy + 32 : sy + 6, "SCOPE", lofi_dim(NES_WHITE, 1), 1);
        lofi_scope(a->params[a->track][K_PAGE_SOUND] + 4, wx + 18, sg_fm1 ? wy + 46 : sy + oy - 4, whole.w - 36,
                   sg_fm1 ? 44 : sh - oy, 0xF83800);
    }
    for (c = 0; c < 3; c++) {
        if (sg_fm1) lofi_item(a, K_PAGE_EDIT, c, SK_CX(c), wy + 104, L[c], NES_WHITE, 1);
        else lofi_item_small(a, K_PAGE_EDIT, c, SK_CX(c), SK_LY(1) + 3, L[c], NES_WHITE, 1);
    }
    return 1;
}
/* a filter's response, 40 Hz .. 16 kHz on a log axis, 61 points: type ft (LP BP HP NOTCH), cutoff c and resonance r (0..127), from x0
 * w px wide, its 0 at yb and 2.2 hh px over it. In integers: x = f / fc from logs (Q24), folded to v = min(x, 1 / x) (Q14), so that
 * (1 - x^2)^2 + x^2 / qq^2 = x^4 ((1 - v^2)^2 + v^2 / qq^2) above fc stays in range */
void filter_pts16(int ft, int c, int r, int x0, int w, int yb, int hh, int *p16)
{
    const int32_t lfc = 82323963 + lin24(c < 0 ? 0 : c > 127 ? 127 : c, 9498938, 127);   /* log2 fc: 30 (16000 / 30)^(c / 127) */
    const int iq = 254 * 16384 / (127 + 16 * (r < 0 ? 0 : r));   /* 1 / qq, Q14 (qq = .5 + 8 r / 127) */
    int q;
    for (q = 0; q <= 60; q++) {
        const int32_t lx = 89287137 + lin24(q, 9063740, 60) - lfc;   /* log2 x: 40 400^(q / 60) / fc */
        const int hi = lx > 0, v = (int)g_exp2(hi ? -lx : lx, 14), v2 = v * v >> 14, a = 16384 - v2, b = v * iq >> 14;
        const int dd = (int)g_isqrt((uint32_t)((a * a >> 14) + (b * b >> 14)) << 14);
        const int num = ft == 0 ? (hi ? v2 : 16384) : ft == 1 ? b : ft == 2 ? (hi ? 16384 : v2) : a;
        int m = dd ? num * 16384 / dd * 10 + num * 16384 % dd * 10 / dd : 360448;   /* m, Q14 times 10 */
        if (m > 360448) m = 360448;   /* 2.2 (16384 22) */
        p16[2 * q] = x0 * 16 + q * w * 16 / 60;
        p16[2 * q + 1] = yb * 16 - (m * hh * 16 + 360447) / 360448;   /* (m / 2.2 of the height) */
    }
}
/* EDIT (VCF, VCA): the filter's response on the screen (its type at the left, the cutoff and resonance at the right), then the
 * engine's EDIT rows (ANALOG: CUT RES DRV KTR | TYPE ENV LEVEL VEL; TRIO: MODE CUT RES KTR | DRV ENV LEVEL VEL). kt, kc, kr: the
 * engine's indexes of the type, the cutoff and the resonance; a NOTCH type draws its notch */
static int edit_view_filter(KApp *a, const char *const *L, int kt, int kc, int kr, int div)
{
    const KEngine *en = k_engine_at(a->sound[a->track]);
    const KVal *P = a->params[a->track][K_PAGE_SOUND] + 4, row = a->win / 4; (void)row;   /* (the FM-1 build has no use for it) */
    char c1[16], c2[16], rd[32];
    const char *u1, *u2;
    KRect r;
    int ft;
    if (!sk_geom() || k_track_missing(a, a->track)) return 0;
    sk_frame(a);
    ui_item_value(a, K_PAGE_EDIT, kc - en->page2, c1, sizeof c1, &u1);
    ui_item_value(a, K_PAGE_EDIT, kr - en->page2, c2, sizeof c2, &u2);
    snprintf(rd, sizeof rd, "%s%s  %s%s", c1, u1, c2, u2);
    ft = k_param_clamp(&en->edit[kt], P[kt]);
    r = sk_screen(en->edit[kt].names[ft], rd, 1);
    {   int p16[2 * 61];
        filter_pts16(ft, P[kc], P[kr], r.x + 8, r.w - 16, r.y + r.h - 8, r.h - 26, p16);
        sk_line16(p16, 61, 1); }
    sk_rows(a, K_PAGE_EDIT, L, 8);
    g_fill(&cv, whole.x + (sg_fm1 ? div : sg_cw * 2 + 4), SK_LY(2) + 2, 1, sg_rp, SK_OUT);   /* VCF | VCA (the FM-1's at 155, 52 tall) */
    return 1;
}
int edit_view_kanalog(KApp *a)
{
    static const char *const L[8] = {"Cutoff", "Reso", "Drive", "KeyTrk", "Type", "Env", "Level", "Vel"};
    return edit_view_filter(a, L, 16, 12, 13, 117);   /* (TYPE, CUT, RES: the engine's 16, 12, 13) */
}
int edit_view_ktrio(KApp *a)
{
    static const char *const L[8] = {"Mode", "Cutoff", "Reso", "KeyTrk", "Drive", "Env", "Level", "Vel"};
    return edit_view_filter(a, L, 11, 12, 13, 117);
}
/* ORGAN's SOUND (Kreisel.sketch's console, after the tonewheel organs): all wood; the engine's bar, then the nine drawbars,
 * pulled down as the real ones (0 in, 8 out), their caps brown (the sub bars), white (the octaves) and black (the fifths and the
 * third). The first row: ENGINE (the bar), PERC and ROTR (sliding switches) and VIB; then the bars, four a row
 * (16' 5 1/3' 8' 4' | 2 2/3' 2' 1 3/5' 1 1/3' | 1'): the row in hand bright, the others dim. */
static const char *const KO_FOOT[9] = {"16", "5\x01", "8", "4", "2\x02", "2", "1\x03", "1\x01", "1"};   /* (\1 1/3 \2 2/3 \3 3/5: drawn small) */
static void ko_tiny(int x, int y, char ch, uint32_t ink)   /* a 3 x 5 digit: 1 2 3 5 */
{
    static const uint16_t G[4] = {0x2C97, 0x73E7, 0x73CF, 0x79CF};   /* rows of 3 bits, top first: 1 2 3 5 */
    const int g = ch == '1' ? 0 : ch == '2' ? 1 : ch == '3' ? 2 : 3;
    int r, c;
    for (r = 0; r < 5; r++)
        for (c = 0; c < 3; c++)
            if ((G[g] >> (14 - r * 3 - c)) & 1u) g_fill(&cv, x + c, y + r, 1, 1, ink);
}
static void ko_foot(int cx, int y, const char *f, uint32_t ink)   /* a footage: "16", "5" and 1/3 small beside it */
{
    static const char NUM[4] = {0, '1', '2', '3'}, DEN[4] = {0, '3', '3', '5'};
    const int fr = (unsigned char)f[1] < 4u ? f[1] : 0;
    char b[3] = {f[0], fr ? 0 : f[1], 0};
    const int w = g_pix5_w(b, 1) + (fr ? 5 : 0), x = cx - w / 2;
    g_pix5(&cv, x, y + 2, b, ink, 1);
    if (fr) {   /* its numerator over a bar over its denominator */
        ko_tiny(x + w - 3, y - 1, NUM[fr], ink);
        g_fill(&cv, x + w - 4, y + 5, 5, 1, ink);
        ko_tiny(x + w - 3, y + 7, DEN[fr], ink);
    }
}
int ko_step = 7;   /* a drawbar's step, px (the Switch's: longer, switch_view_korgan) */
void ko_drawbar(KApp *a, int i, int cx, int y0, int lit)   /* (dark shafts, short) */
{
    static const uint32_t CAP[9] = {0x6B3A1E, 0x6B3A1E, 0xF0EBE0, 0xF0EBE0, 0x1C1C1C, 0xF0EBE0, 0x1C1C1C, 0x1C1C1C, 0xF0EBE0};
    const int v = k_param_get(a, K_PAGE_SOUND, i), k = i - 3, step = ko_step, hot = lit && (hot_param(a, i) || touched_recent(a, i));
    const int len = (v < 0 ? 0 : v > 8 ? 8 : v) * step, cap = CAP[k] == 0xF0EBE0 ? 0x1C1C1C : 0xF0EBE0;
    const uint32_t dim = KO_WELL, ink = hot ? 0xFFB347 : sk_in_row(a, i) ? sk_row_ink() : lit ? 0xF2E3C2 : k_mix(dim, 0xF2E3C2, 45);   /* (the row in hand: its footages in colour) */
    int q;
    char d[2] = {(char)('0' + (v < 0 ? 0 : v > 8 ? 8 : v)), 0};
    g_rrect(&cv, cx - 9, y0, 18, 5, 2, 0x050403);   /* the slot in the panel */
    if (len > 0) {   /* the shaft pulled out, as wide as the cap: dark, outlined, a tick a step */
        const uint32_t edge = lit ? 0xD8CBB0 : k_mix(dim, 0xD8CBB0, 50);
        g_fill(&cv, cx - 8, y0 + 3, 16, len, 0x0C0A08);
        g_fill(&cv, cx - 8, y0 + 3, 1, len, edge);
        g_fill(&cv, cx + 7, y0 + 3, 1, len, edge);
        for (q = 1; q <= v; q++) g_fill(&cv, cx - 4, y0 + 3 + q * step - 2, 8, 1, edge);
    }
    {   /* the cap: stepped, a narrow top over a full body, its value on it */
        const int cy = y0 + 3 + len;
        const uint32_t c = lit ? CAP[k] : k_mix(dim, CAP[k], 55);
        g_rrect_a(&cv, cx - 8, cy + 3, 16, 13, 3, 0x000000, 90);   /* (cap and shaft 16 px) */
        g_fill(&cv, cx - 5, cy, 10, 3, hot ? 0xFFB347 : c);
        g_rrect_frame(&cv, cx - 8, cy + 2, 16, 12, 3, hot ? 0xFFB347 : c, c);
        g_hline_a(&cv, cx - 5, cy + 3, 10, 0xFFFFFF, 45);
        g_pix5(&cv, cx - 2, cy + 4, d, lit ? (uint32_t)cap : k_mix(c, (uint32_t)cap, 60), 1);
    }
    ko_foot(cx, y0 + 3 + 8 * step + 17, KO_FOOT[k], ink);   /* the footage under the bar */
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){cx - 10, y0, 20, 3 + 8 * step + 30});
}
/* a sliding switch across, for a few names (ORGAN's PERC): its label at the left, a track with a place a name and the thumb on
 * the one set, the names under their places (the one set bright) */
/* the label at (lx, ly), the track at (x, ty), w wide: above it (sk_switch_h) or at its left (the 320 x 180 ORGAN) */
void sk_switch_at(KApp *a, KPage pg, int i, int lx, int ly, int x, int ty, int w, const char *label, const char *const *shown, int lit)
{
    const KParamDesc *d = sk_desc(a, pg, i);
    const int hot = lit && (hot_param(a, i) || touched_recent(a, i)), th = 12;
    int n = 0, v, q;
    while (d && d->names && d->names[n]) n++;
    if (n < 2) return;
    v = k_param_clamp(d, k_param_get(a, pg, i)) - d->min;
    if (sk_in_row(a, i)) { const int tw = g_text_w(GF_S, label); sk_pill(lx - 2, ly + 1, tw + 8, tw, tw + 8, label); g_text(&cv, lx + 2, ly, GF_S, label, sk_row_text()); }
    else g_text(&cv, lx + 2, ly, GF_S, label, hot ? SK_ARC : lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52));
    g_rrect(&cv, x, ty, w, th, 6, k_mix(SK_PANEL, 0x000000, 45));
    {   /* a 12 px thumb that travels the track: its places sp apart, 2 px in from the ends; the names centred under them */
        const int tw = 12, sp = (w - 4 - tw) / (n - 1), c0 = x + 2 + tw / 2;
        g_rrect_a(&cv, c0 + v * sp - tw / 2, ty + 2, tw, th - 2, 5, 0x000000, 90);
        g_rrect(&cv, c0 + v * sp - tw / 2, ty + 1, tw, th - 2, 5, hot ? SK_ARC : lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 45));
        for (q = 0; q < n; q++) {   /* the names, "Off" "2nd" */
            char nm[12];
            int k2;
            snprintf(nm, sizeof nm, "%s", shown ? shown[q] : d->names[q]);
            for (k2 = 1; !shown && nm[k2]; k2++) if (nm[k2] >= 'A' && nm[k2] <= 'Z') nm[k2] = (char)(nm[k2] + 32);
            g_text_box(&cv, c0 + q * sp - 20, ty + th + 2, 40, 15, GF_S, nm,
                       q == v ? (hot ? SK_ARC : lit ? SK_INK : k_mix(SK_PANEL, SK_INK, 52)) : k_mix(SK_PANEL, SK_INK, lit ? 55 : 30), 0);
        }
    }
    {
        const int bx = lx < x ? lx : x, by = ly < ty ? ly : ty;
        k_set_bounds(a, K_NODE_PARAM, i, (KRect){bx, by, x + w - bx, ty + th + 17 - by});
    }
}
/* a sliding switch across, for a few names (ORGAN's ROTR, PERC): its label above at the left, the track, the names under it */
void sk_switch_h(KApp *a, KPage pg, int i, int x, int ly, int w, const char *label, const char *const *shown, int lit)   /* shown: the names under the places (0: the descriptor's) */
{
    sk_switch_at(a, pg, i, x, ly, x, ly + 20, w, label, shown, lit);
}
/* ORGAN's VIB / CHO: its name in a box (V1-V3 vibrato, C1-C3 chorus); the label
 * centred over the box (lx < 0) or at (lx, ly), at its left */
void ko_vibcho_at(KApp *a, int i, int lx, int ly, int x, int ty, int w, int th)
{
    static const char *const N[7] = {"OFF", "V1", "V2", "V3", "C1", "C2", "C3"};
    const int v = k_param_clamp(sk_desc(a, K_PAGE_SOUND, i), k_param_get(a, K_PAGE_SOUND, i)), hot = hot_param(a, i) || touched_recent(a, i);
    const uint32_t lab = hot ? SK_ARC : SK_INK;
    if (lx < 0) sk_label(a, i, x, ly, w, "Vib/Cho", lab);
    else if (sk_in_row(a, i)) { const char *nm = lx > x ? "Vib/Cho" : "Vi/Ch"; sk_pill(lx - 2, ly + 1, g_text_w(GF_S, nm) + 8, g_text_w(GF_S, nm), g_text_w(GF_S, nm) + 8, nm); g_text(&cv, lx + 2, ly, GF_S, nm, sk_row_text()); }
    else g_text(&cv, lx + 2, ly, GF_S, lx > x ? "Vib/Cho" : "Vi/Ch", lab);   /* (at its left, on 320 x 180: short) */
    g_rrect_frame(&cv, x, ty, w, th, 4, k_mix(SK_PANEL, 0x000000, 70), 0x120C08);
    g_text_box(&cv, x, ty, w, th, GF_M, N[v < 0 ? 0 : v > 6 ? 6 : v], hot ? SK_ARC : v ? 0xF2E3C2 : k_mix(0x120C08, 0xF2E3C2, 55), 0);
    {
        const int bx = lx < 0 || lx > x ? x : lx, by = ly < ty ? ly : ty;
        const int bottom = lx < 0 ? ly + 20 + 12 + 17 : ty + th + 2;   /* (over its box: as tall as a switch, whose names are under it) */
        k_set_bounds(a, K_NODE_PARAM, i, (KRect){bx, by, x + w - bx, bottom - by});
    }
}
void ko_vibcho(KApp *a, int i, int x, int ly, int lit)
{
    (void)lit;
    ko_vibcho_at(a, i, -1, ly, x, ly + 18, 52, 22);
}
/* the wide screen's panel and its bar (wide_panel_view, and the 320 x 180 ORGAN): the engine "< NAME >" (SOUND, K1) or the
 * page, the preset at its right */
void wide_head(KApp *a, KPage pg, int row, int mx, const KEngine *en, int t)
{
    const int wx = whole.x, wy = whole.y;
    sk_skin(a);
    g_fill_around(&cv, pic.x, pic.y, pic.w, pic.h, wx, wy + 8, whole.w, whole.h - 16, th.bg);
    g_rrect_frame(&cv, wx, wy, whole.w, whole.h, 8, SK_OUT, SK_PANEL);
    if (sk.wood == 1) {   /* the cheeks */
        g_rrect(&cv, wx + 1, wy + 1, 5, whole.h - 2, 3, SK_WOOD);
        g_rrect(&cv, wx + whole.w - 6, wy + 1, 5, whole.h - 2, 3, SK_WOOD);
    }
    {   /* the bar: the engine (SOUND, K1 on its first row) or the page, the preset at its right */
        const int bx = wx + 10, by = wy + 6, bw = whole.w - 20, bh = 20, on = pg == K_PAGE_SOUND || pg == K_PAGE_EDIT;
        const int hot = 0;
        char b[32];
        const uint32_t ink = hot ? SK_ARC : on ? 0xF5F5F7 : k_mix(sk.bar, 0xF5F5F7, 60);
        const int ew = (bw - 4) / 2, px = bx + ew + 4, pw = bw - ew - 4;   /* two boxes, half and half: the engine (or the page) and the preset */
        if (prof->input & K_IN_MOUSE) {   /* a desktop: the preset is in the header; the engine a raised box, its < > buttons joined to it */
            const int cb = 26, nw = 220, gx0 = wx + (whole.w - (nw + 2 * cb + 4)) / 2, face = (int)k_mix(sk.bar, 0xFFFFFF, 20);
            const char *name = pg == K_PAGE_SOUND ? (mx ? gone_word(a, t) : en->name) : pg == K_PAGE_ENV1 ? "ENV" : pg == K_PAGE_LFO ? "LFO" : "EDIT";
            (void)ew; (void)px; (void)pw;
            if (pg == K_PAGE_SOUND) {
                g_raised(&cv, &th, gx0, by - 1, cb, bh + 2, 4, (uint32_t)face, 3);
                g_raised(&cv, &th, gx0 + cb + 2 + nw + 2, by - 1, cb, bh + 2, 4, (uint32_t)face, 3);
                g_icon(&cv, gx0 + (cb - 14) / 2, by + (bh - 14) / 2, 14, GI_CARET_L, 0xF5F5F7);   /* (Fukiai's carets) */
                g_icon(&cv, gx0 + cb + 2 + nw + 2 + (cb - 14) / 2, by + (bh - 14) / 2, 14, GI_CARET_R, 0xF5F5F7);
            }
            g_raised(&cv, &th, gx0 + cb + 2, by - 1, nw, bh + 2, 4, k_mix(sk.bar, 0xFFFFFF, 12), 3);   /* (raised, as the buttons beside it) */
            g_text_box(&cv, gx0 + cb + 2, by - 1, nw, bh + 2, GF_M, name, ink, 0);
            if (pg == K_PAGE_SOUND) k_set_bounds(a, K_NODE_SOUND, 0, (KRect){gx0, by - 1, nw + 2 * cb + 4, bh + 2});
            return;
        }
        g_rrect_frame(&cv, bx, by, ew, bh, 3, hot ? SK_ARC : k_mix(sk.bar, 0x979797, 30), sk.bar);
        g_rrect_frame(&cv, px, by, pw, bh, 3, k_mix(sk.bar, 0x979797, 30), sk.bar);
        if (pg == K_PAGE_SOUND) snprintf(b, sizeof b, "<  %s  >", mx ? gone_word(a, t) : en->name);
        else if (pg == K_PAGE_ENV1) snprintf(b, sizeof b, "ENV  LANE %d", a->cur_lane[t] + 1);
        else snprintf(b, sizeof b, "%s", pg == K_PAGE_LFO ? "LFO" : "EDIT");
        g_text_box(&cv, bx, by, ew, bh, GF_S, b, ink, 0);
        {   const int iw = preset_icon(a, t, px + 6, by + bh / 2, 12, k_mix(sk.bar, 0xF5F5F7, 60));
            g_text_box(&cv, px + 6 + iw, by, pw - 12 - iw, bh, GF_S, trk_preset(a, t), k_mix(sk.bar, 0xF5F5F7, 60), -1); }
        if (pg == K_PAGE_SOUND) k_set_bounds(a, K_NODE_SOUND, 0, (KRect){bx, by, ew, bh});
    }
}
