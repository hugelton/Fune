/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The parts: faces, panels, knobs, slots, tabs, wells, chips; and the row of cards (a parameter's label, gauge and
 * value, opening when touched). */
#include "draw_int.h"

/* --------------------------------------------------------------- parts --- */
/* a lit face: lighter at the top, darker at the bottom, a hair of light on the top edge (what keeps a part from being flat) */
void face(int x, int y, int w, int h, int r, uint32_t col, int light, int dark)
{
    g_rrect_grad(&cv, x, y, w, h, r, k_mix(col, 0xFFFFFF, light), k_mix(col, 0x000000, dark));
}
void panel_box(KRect r)   /* a panel: raised, lit from above, a soft shadow under it */
{
    g_panel(&cv, r.x, r.y, r.w, r.h, 8, th.surf, 40);
}
/* an inactive section: a recess */
void ghost_box(int x, int y, int w, int h) { g_sunken(&cv, x, y, w, h, 8, th.bg); }
/* a knob with a dark metal face inside its ring */
void knob(int cx, int cy, int r, int v, int bip, uint32_t arc, uint32_t ring, uint32_t mark)
{
    g_knob_face(&cv, cx, cy, r, k_mix(th.bg, th.text, 22));
    g_knob(&cv, cx, cy, r, v, bip, arc, ring, mark);
}
/* a fader slot: a groove, dark at its core */
void slot(int cx, int y, int h)
{
    g_rrect(&cv, cx - 1, y, 3, h, 1, k_mix(th.bg, th.text, 16));
    g_rrect(&cv, cx, y + 1, 1, h - 1, 0, k_mix(th.bg, 0x000000, 80));
}
/* a tab: round at the top, square at the bottom */
static void tab_shape(int x, int y, int w, int h, uint32_t col)
{
    g_rrect(&cv, x, y, w, h, 6, col);
    g_fill(&cv, x, y + h / 2, w, h - h / 2, col);
}
static void tab_shape_a(int x, int y, int w, int h, uint32_t col, int alpha)
{
    g_rrect_a(&cv, x, y, w, h / 2 + 3, 5, col, alpha);
    g_rrect_a(&cv, x, y + h / 2, w, h - h / 2, 0, col, alpha);
}
/* a well: a recess for what is shown (a graph, a grid): darker than the ground, shaded along its top edge, lit along its bottom one */
void well(KRect r) { g_sunken(&cv, r.x, r.y, r.w, r.h, 8, th.bg); }
/* a curve with a glow: a broad faint stroke (3.2 px) under the line (1.2 px), its points in 1/16 px */
void curve_glow16(const int *p16, int n, uint32_t col)
{
    g_curve16(&cv, p16, n, 33, k_mix(th.bg, col, 20));
    g_curve16(&cv, p16, n, 17, col);
}
static void chip(const char *s, KRect r, int selected, GFont f)
{
    if (selected) g_colored(&cv, r.x, r.y, r.w, r.h, 5, th.sel); else g_panel(&cv, r.x, r.y, r.w, r.h, 5, th.raise, 0);
    g_text_box(&cv, r.x, r.y, r.w, r.h, f, s, selected ? th.ink : th.text, 0);
}
/* a card is open while its parameter was touched or turned within the hold (several knobs can be turned together) */
int touched_recent(const KApp *a, int i) { return i >= 0 && i < K_ITEM_SLOTS && (a->touched >> i & 1u) && now - a->touch_ms[i] < feel.hold_ms; }
/* the pages whose picture does not need the whole room (the envelopes, the LFO) keep their cards open, the values showing */
int cards_open(const KApp *a) { return a && (a->page == K_PAGE_ENV1 || a->page == K_PAGE_ENV2 || a->page == K_PAGE_LFO || a->page == K_PAGE_ARP); }   /* (ARP: always open) */
int card_wanted(const KApp *a, int i) { return touched_recent(a, i) || (cards_open(a) && i < k_param_count(a->page)) ||
    (a->page == K_PAGE_MIXER && (a->lay & K_LAY_MIX_KEYS) && a->focus_count && a->focus[a->focused].kind == K_NODE_PARAM && a->focus[a->focused].index == i); }   /* (the Zero: the control the arrows move, lit) */
int hot_param(const KApp *a, int i)
{
    if (!k_profile_cursor(prof)) return touched_recent(a, i);   /* no cursor: the ones just turned */
    return a->focus_count && a->focus[a->focused].kind == K_NODE_PARAM && a->focus[a->focused].index == i;
}

/* a card's label in mixed case: the tables keep Felucca's capitals (WAVE, CUT, DRV ...), the card shows them as Wave, Cut, Drv; a few stay
 * as they are (BPM, FX1, FX2). Lower case is narrower, so what fits in capitals fits here. */
const char *card_label(const char *s, char *out, size_t n)
{
    static const char *const KEEP[] = {"BPM", "FX1", "FX2", "B1", "B2", "RTN", "FB"};
    size_t i, w = 0;
    for (i = 0; i < sizeof KEEP / sizeof KEEP[0]; i++)
        if (!strcmp(s, KEEP[i])) return s;
    for (i = 0; s[i] && w + 1 < n; i++, w++) {
        char c = s[i], prev = i ? s[i - 1] : ' ';
        out[w] = (c >= 'A' && c <= 'Z' && prev != ' ' && prev != '-' && prev != '/') ? (char)(c + 32) : c;
    }
    out[w] = 0;
    return out;
}
/* a parameter card. At rest: its label and a thin gauge. Touched or turned: it grows upward over the picture and shows
 * its value and unit too, for a moment. i: the parameter; pos: its place in the window (the knob that turns it, counting
 * from the left); rest: the card at rest (the focus rectangle stays there while the drawn card moves). */
static int card_dim;   /* the card is of a row that is not the one the knobs hold */
/* the item is a destination of the host's modulation the track's engine does not read (KEngine.nomod: ENV 1's FLT and SHP, the
 * LFO's Filter and Shape): it still turns, and is shown dim */
int mod_unused(const KApp *a, KPage pg, int i)
{
    const unsigned nm = k_track_missing(a, a->track) ? 0u : k_engine_at(a->sound[a->track])->nomod;   /* (missing: the sine reads all) */
    pg = k_item_page(a, pg, &i);
    if (!nm || (pg != K_PAGE_ENV1 && pg != K_PAGE_LFO) || (pg == K_PAGE_ENV1 && phase_env_ui(a))) return 0;   /* (PHASE's ENV items: its envelopes) */
    return (i == 5 && (nm & K_NOMOD_FILTER)) || (i == 6 && (nm & K_NOMOD_SHAPE));
}
/* a card's value as where it stands in its range, 0..100 */
static int card_pct(const KApp *a, int i, int v)
{
    if (k_page_desc(a->page)) {   /* where it stands in its range */
        const KParamDesc *d = &k_page_desc(a->page)[i];
        v = d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
    } else if (K_IS_HOME(a->page) && i >= K_HOME_COLOR0) v = v * 100 / (K_PALETTE - 1);   /* the colour: its place in the palette */
    else if (K_IS_HOME(a->page) && i < 4) {   /* a track's macro: where it stands in its range */
        const KParamDesc *d = &k_engine_at(a->sound[a->track])->edit[k_engine_macro(a->sound[a->track], i)];
        v = d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
    }   /* the colour: its place in the palette */
    else if (a->page == K_PAGE_SOUND && i >= 0 && i < k_engine_at(a->sound[a->track])->nparams) {   /* where it stands in its own range */
        const KParamDesc *d = &k_engine_at(a->sound[a->track])->edit[i];
        v = d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
    }
    v = v < 0 ? 0 : v > 100 ? 100 : v;   /* (the gauge stays in its track whatever the value) */
    return v;
}
/* a device with no knobs (no cards there): the value as a control of the page, a small knob, its name and value on a
 * line, no tab; the same place, the same target */
static void inline_ctl(KApp *a, int i, KRect r, int hot)
{
    char b[12], cased[24], vu[20];
    const char *unit, *name = card_label(ui_item_name(a, a->page, i), cased, sizeof cased);
    const int ky = r.y + r.h / 2, kr = r.h / 2 - 3 < 9 ? r.h / 2 - 3 : 9, trk = ui_param_track(a->page, i);
    const uint32_t ink = card_dim ? th.dim : hot ? th.text : th.mid;
    const int narrow = r.w < 120;   /* (a narrow one, the Zero's 77: no knob, a thin bar under the name and value) */
    int x = r.x + 4, vw;
    if (a->page != K_PAGE_FX && narrow) {
        const int pct = card_pct(a, i, k_param_get(a, a->page, i)), bw = r.w, by = r.y + r.h - 5;   /* (the bars as wide as the row: the picture's width) */
        g_rrect(&cv, r.x, by, bw, 2, 1, k_mix(th.bg, th.text, 20));
        if (pct > 0) g_rrect(&cv, r.x, by, bw * pct / 100 < 2 ? 2 : bw * pct / 100, 2, 1, hot ? th.accent : card_dim ? th.dim : trk ? track_hue(trk - 1) : th.sel);
    } else if (a->page != K_PAGE_FX) {
        knob(x + kr, ky, kr, card_pct(a, i, k_param_get(a, a->page, i)), 0, hot ? th.accent : card_dim ? th.dim : trk ? track_hue(trk - 1) : th.sel,
             k_mix(th.bg, th.text, 30), th.text);
        x += 2 * kr + 5;
    }
    if (trk) { char nb[2] = {(char)('0' + trk), 0}; g_colored(&cv, x, ky - 6, 12, 13, 4, track_hue(trk - 1)); g_text_box(&cv, x, ky - 7, 12, 15, GF_S, nb, th.ink, 0); x += 15; }
    ui_item_value(a, a->page, i, b, sizeof b, &unit);
    snprintf(vu, sizeof vu, "%s%s", b, a->page == K_PAGE_FX ? "" : unit);
    vw = a->page == K_PAGE_FX ? 0 : g_text_w(GF_S, vu);
    if (narrow && vw && unit[0] && g_text_w(GF_S, name) > r.x + r.w - 4 - vw - 4 - x) { snprintf(vu, sizeof vu, "%s", b); vw = g_text_w(GF_S, vu); }   /* (the name cut: the number alone, BPM 120) */
    {   const int ty = (narrow ? ky - 2 : ky) - g_text_h(GF_S) / 2;
        text_fit(x, ty, GF_S, name, r.x + r.w - 4 - vw - 4 - x, ink);
        if (vw) g_text(&cv, r.x + r.w - 4 - vw, ty, GF_S, vu, card_dim ? th.dim : hot ? th.accent : th.text); }
}
static void card(KApp *a, int i, int pos, KRect rest)
{
    int v = k_param_get(a, a->page, i), hot = hot_param(a, i), vx, cx, gw, gy;
    uint32_t gcol = a->page == K_PAGE_FX ? fx_accent(fx_type(a, i)) : a->page == K_PAGE_MIXER ? th.text : K_IS_HOME(a->page) ? (i < 4 ? th.text : track_hue(K_HOME_TRACK(i))) : th.sel;   /* (HOME's LEVEL and PAN rows: a card a track, in its colour) */   /* the gauge of a track's card: in the track's colour */
    const uint32_t open = card_open[i];
    int grow = (int)(((CARD_FULL - CARD_H) * open + 32768u) >> 16), pad = (int)((6u * open + 32768u) >> 16), alpha = (int)((255u * open + 32768u) >> 16);
    KRect r = {rest.x, rest.y - grow, rest.w, rest.h + grow};
    char b[8];
    (void)pos;
    cx = r.x + pad + 4; gw = r.w - 2 * pad - 8;   /* 4 px in from each side: the gauge stays inside the tab */
    gy = rest.y + rest.h - 6 - (int)(((CARD_FULL - CARD_H + 1) * open + 32768u) >> 16);   /* the gauge rises with the box: the value goes under it */
    k_set_bounds(a, K_NODE_PARAM, i, rest);
    if (!ui_item_name(a, a->page, i)[0]) return;   /* a blank card: room for what is to come */
    if (!prof->knobs) { inline_ctl(a, i, rest, hot); return; }
    {   /* each card is a tab of its own: round at the top, square below, plain grey; the one in use lit, with a bar on top */
        uint32_t grey = k_mix(th.bg, th.text, 11), top = k_mix(grey, 0xFFFFFF, 7), bot = k_mix(grey, 0x000000, 7);
        int hh = (rest.h - 1) / 2;
        g_drop(&cv, rest.x, rest.y, rest.w, rest.h, 0, 3, 45);
        tab_shape(rest.x, rest.y, rest.w, rest.h, k_mix(grey, 0x000000, 60));
        g_rrect_grad(&cv, rest.x + 1, rest.y + 1, rest.w - 2, rest.h - 1, 5, top, bot);   /* the square foot: the same gradient, carried on */
        g_vgrad(&cv, rest.x + 1, rest.y + 1 + hh, rest.w - 2, rest.h - 1 - hh, k_mix(top, bot, hh * 100 / (rest.h - 2)), bot);
        g_hline_a(&cv, rest.x + 5, rest.y + 1, rest.w - 10, 0xFFFFFF, 34);
    }
    if (alpha > 0) {   /* the box appears only for the card in use */
        uint32_t grey = k_mix(th.bg, th.text, 11);
        tab_shape_a(r.x, r.y, r.w, r.h, k_mix(grey, 0x000000, 60), alpha);   /* round at the top, square below, as the tab it rises from */
        tab_shape_a(r.x + 1, r.y + 1, r.w - 2, r.h - 1, grey, alpha);   /* the grey it has closed (flat): only size and place animate */
        g_hline_a(&cv, r.x + 6, r.y + 1, r.w - 12, 0xFFFFFF, 34 * alpha / 255);
    }
    {   /* the label, centred; for a track's item (the mixer) the track's number first, in a blue cushion */
        char cased[24];
        const char *name = card_label(ui_item_name(a, a->page, i), cased, sizeof cased);
        int trk = ui_param_track(a->page, i), ico = ui_item_icon(a, a->page, i);
        uint32_t ink = card_dim ? th.dim : a->page == K_PAGE_FX ? gcol : th.mid;
        if (trk) {
            int nw = g_text_w(GF_S, name), total = 12 + 3 + nw, lx = r.x + (r.w - total) / 2;
            char nb[2] = {(char)('0' + trk), 0};
            g_colored(&cv, lx, r.y + 2, 12, 13, 4, track_hue(trk - 1));
            g_text_box(&cv, lx, r.y + 1, 12, 15, GF_S, nb, th.ink, 0);
            g_text(&cv, lx + 15, r.y + 1, GF_S, name, ink);
        } else if (ico >= 0) {   /* an icon before the label, the pair centred */
            int nw = g_text_w(GF_S, name), total = 12 + 3 + nw, lx = r.x + (r.w - total) / 2;
            g_icon2(&cv, lx, r.y + 2, 12, (GIcon)ico, k_mix(th.bg, th.text, 48), ink);
            g_text(&cv, lx + 15, r.y + 1, GF_S, name, ink);
        } else {
            g_text_box(&cv, r.x + 2, r.y + 1, r.w - 4, 15, GF_S, name, ink, 0);
        }
    }
    if (a->page == K_PAGE_FX) {
        /* FX parameters live as rotary controls in the picture above. Cards
         * are labels and transient readouts, not a second set of bar gauges. */
        if (open > .5f) {
            const char *unit;
            int tw;
            ui_item_value(a, a->page, i, b, sizeof b, &unit);
            if (b[0]) {
                tw = g_text_w(GF_M, b) + (unit[0] ? 2 + g_text_w(GF_S, unit) : 0);
                vx = g_text(&cv, cx + (gw - tw) / 2, gy + 5, GF_M, b, th.text);
                if (unit[0]) g_text(&cv, vx + 2, gy + 9, GF_S, unit, th.mid);
            }
        }
        return;
    }
    v = card_pct(a, i, v);

    g_rrect(&cv, cx, gy, gw, 3, 1, k_mix(th.bg, 0x000000, 55));   /* the trough: sunk */
    if (v > 0) g_colored(&cv, cx, gy, gw * v / 100 < 3 ? 3 : gw * v / 100, 3, 1, hot ? (gcol == th.sel ? th.accent : k_mix(gcol, 0xFFFFFF, 25)) : card_dim ? th.dim : gcol);
    if (open > .5f) {   /* the value and unit under the gauge, once the card is mostly open; centred, as the label is */
        int tw;
        const char *unit;
        ui_item_value(a, a->page, i, b, sizeof b, &unit);
        tw = g_text_w(GF_M, b) + (unit[0] ? 2 + g_text_w(GF_S, unit) : 0);
        vx = g_text(&cv, cx + (gw - tw) / 2, gy + 5, GF_M, b, th.text);
        if (unit[0]) g_text(&cv, vx + 2, gy + 9, GF_S, unit, th.mid);
    }
}
void cards(KApp *a)
{
    int i, g = 2, vis = k_params_visible(a), cnt = k_param_count(a->page), total = cards_area.w - (vis - 1) * g, rows = 1, r;
    if (cnt > vis && !k_page_rowed(a->page)) {   /* more parameters than cards: the thumb of the window on that line (a rowed page shows its row in its picture) */
        g_fill(&cv, cards_area.x + cards_area.w * a->win / cnt, cards_area.y - 3, cards_area.w * vis / cnt, 1, th.mid);
    }
    for (r = 0; r < rows; r++) {   /* the inserts show both slots, the one the knobs hold bright */
        card_dim = rows > 1 && r != a->win / 4;
        for (i = 0; i < vis; i++) {   /* the remainder is spread, so the row is as wide as the picture above it */
            int x0 = cards_area.x + total * i / vis + i * g, x1 = cards_area.x + total * (i + 1) / vis + i * g;
            if (a->page == K_PAGE_SOUND && a->win == 0 && vis >= 2) {   /* the engine row: the engine card is two cards wide (the rest of the row is blank) */
                if (i == 1) continue;
                if (i == 0) x1 = cards_area.x + total * 2 / vis + g;
            }
            {   const int it = rows > 1 ? r * 4 + i : a->win + i, d0 = card_dim;
                card_dim = d0 || mod_unused(a, a->page, it);
                card(a, it, i, (KRect){x0, cards_area.y + r * (CARD_H + 2), x1 - x0, CARD_H});
                card_dim = d0; }
        }
    }
    card_dim = 0;
}
/* Moves each card toward open (the parameter last touched, for a moment) or shut. Returns 1 while one is moving or
 * the hold is running out, so the platform keeps drawing. */
int cards_step(const KApp *a)
{
    int i, busy = 0;
    const uint32_t dt = last_now && now >= last_now ? (now - last_now < 10000u ? now - last_now : 10000u) : 0u;
    for (i = 0; i < K_ITEM_SLOTS; i++) {
        int want = card_wanted(a, i);
        const uint32_t ms = want ? feel.open_ms : feel.close_ms, target = want ? 65536u : 0u, step = ms ? dt * 65536u / ms : 65536u;
        if (card_open[i] < target) card_open[i] = target - card_open[i] > step ? card_open[i] + step : target;
        else if (card_open[i] > target) card_open[i] = card_open[i] - target > step ? card_open[i] - step : target;
        busy |= touched_recent(a, i) || card_open[i] != target;
    }
    last_now = now;
    return busy;
}
