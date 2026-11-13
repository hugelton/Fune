/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SYNTH's vanilla view (from the engine's data alone), a plugin's own layout and widgets, the vector pad. */
#include "draw_int.h"

/* SYNTH, the vanilla view: built from the engine's data alone (its name, its rows' titles, its parameters' labels, formats and
 * ranges), so any engine or plugin has one; the engine's code draws nothing (an engine may be given a view of its own here, by
 * its id: SYNTH_VIEWS; none yet). Flat, after Logic for iPad: a section's title at the left over a hairline; a control is its
 * label, its value (in the accent) and a flat knob (a ring, the value an arc), or a switch for on / off. The engine's block on
 * top: ENGINE (KNOB 1 on that row) and the engine's name large; a row titled "" goes with it. The row the four knobs hold is
 * bright, the others faint; the control in hand lit. */
static void flat_knob(int cx, int cy, int r, int pct, int bip, uint32_t arc, uint32_t ring)
{
    g_knob(&cv, cx, cy, r, pct, bip, arc, ring, arc);
}
static void synth_control(KApp *a, KPage pg, int i, KRect r, int lit, int hot, int compact)
{
    char val[16], vu[24], lab[16];
    const KEngine *en = k_engine_at(a->sound[a->track]);
    const char *unit, *name = ui_item_name(a, pg, i);
    const KParamDesc *d = &en->edit[pg == K_PAGE_EDIT ? en->page2 + i : i];
    const uint32_t faint = k_mix(th.bg, th.text, 35);
    int lh = g_text_h(GF_S), v = k_param_get(a, pg, i), y = r.y, rad, room;
    if (!name[0]) return;
    snprintf(lab, sizeof lab, "%s", card_label(name, vu, sizeof vu));
    ui_item_value(a, pg, i, val, sizeof val, &unit);
    snprintf(vu, sizeof vu, "%s%s", val, d->fmt == KF_ENUM ? "" : unit);
    if (compact) { /* 320x180 VECTOR: keep a complete knob in each short row */
        g_text_box(&cv, r.x + 2, y, r.w / 2 - 2, lh + 1, GF_S, lab,
                   hot ? th.text : lit ? th.mid : faint, 0);
        g_text_box(&cv, r.x + r.w / 2, y, r.w - r.w / 2, lh + 1, GF_S, vu,
                   hot ? th.accent : lit ? k_mix(th.text, th.accent, 70) : faint, 0);
        y += lh + 3;
    } else {
        g_text_box(&cv, r.x, y, r.w, lh + 1, GF_S, lab, hot ? th.text : lit ? th.mid : faint, 0);
        y += lh;
        g_text_box(&cv, r.x, y, r.w, lh + 1, GF_S, vu, hot ? th.accent : lit ? k_mix(th.text, th.accent, 70) : faint, 0);
        y += lh + 2;
    }
    room = r.y + r.h - y;
    if (d->fmt == KF_ONOFF) {   /* a switch: a pill, its knob right and lit when on */
        int sw = 22, sh = 11, sx = r.x + (r.w - sw) / 2, sy = y + (room - sh) / 2;
        g_rrect(&cv, sx, sy, sw, sh, sh / 2, v ? (lit ? th.accent : faint) : k_mix(th.bg, th.text, 14));
        g_rrect(&cv, v ? sx + sw - sh + 1 : sx + 1, sy + 1, sh - 2, sh - 2, (sh - 2) / 2, lit ? th.text : faint);
        return;
    }
    rad = (room < r.w - 8 ? room : r.w - 8) / 2 - 1;
    if (rad > 16) rad = 16;
    if (rad < 5) return;   /* no room for a knob: the value says it */
    {
        int pct, bip = d->min < 0 && d->max > 0;
        if (d->fmt == KF_ENUM) { int nn = 0; while (d->names && d->names[nn]) nn++; pct = nn > 1 ? (k_param_clamp(d, v) - d->min) * 100 / (nn - 1) : 0; }
        else pct = d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
        flat_knob(r.x + r.w / 2, y + rad + 1, rad, pct, bip, hot ? th.accent : lit ? th.accent : faint, lit ? k_mix(th.bg, th.text, 26) : k_mix(th.bg, th.text, 12));
    }
}
/* A plugin's own layout (kreisel_plugin_ui.h): its widgets on its grid, drawn by the host in the theme. The knobs still take the
 * page's parameters four at a time in edit[]'s order (the row in hand lit), a touch takes a widget's parameter; the plugin
 * decides only where each sits and how it looks. A picture is the plugin's drawing, clipped to its cell, a budget of calls. */
int plug_scope_on;   /* a scope or a picture shown: draw again soon (ui_draw) */
static int param_pct(const KParamDesc *d, int v)
{
    if (d->fmt == KF_ENUM) { int nn = 0; while (d->names && d->names[nn]) nn++; return nn > 1 ? (k_param_clamp(d, v) - d->min) * 100 / (nn - 1) : 0; }
    return d->max > d->min ? (v - d->min) * 100 / (d->max - d->min) : 0;
}
static uint32_t plug_col(int c)
{
    switch (c) {
    case K_COL_BG: return th.bg;
    case K_COL_FAINT: return k_mix(th.bg, th.text, 14);
    case K_COL_LINE: return th.line;
    case K_COL_MID: return th.mid;
    case K_COL_ACCENT: return th.accent;
    case K_COL_ALERT: return th.rec;
    default: return th.text;
    }
}
typedef struct { KDraw g; int x, y, left; } PlugPic;   /* the frame's place on the canvas, the calls left */
static int pic_spend(const KDraw *g) { PlugPic *q = (PlugPic *)g->host; return q && q->left > 0 ? (q->left--, 1) : 0; }
static void pic_fill(const KDraw *g, int x, int y, int w, int h, int col)
{
    const PlugPic *q = (const PlugPic *)g->host;
    if (!pic_spend(g)) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > g->w) w = g->w - x;
    if (y + h > g->h) h = g->h - y;
    if (w > 0 && h > 0) g_fill(&cv, q->x + x, q->y + y, w, h, plug_col(col));
}
static void pic_line(const KDraw *g, int x0, int y0, int x1, int y1, int col)
{
    const PlugPic *q = (const PlugPic *)g->host;
    if (!pic_spend(g)) return;
    if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 >= g->w || x1 >= g->w || y0 >= g->h || y1 >= g->h) return;   /* (a line that leaves the frame: not drawn) */
    g_line(&cv, q->x + x0, q->y + y0, q->x + x1, q->y + y1, 1, plug_col(col));
}
static int pic_text(const KDraw *g, int x, int y, const char *s, int col)
{
    const PlugPic *q = (const PlugPic *)g->host;
    const int w = s ? g_text_w(GF_S, s) : 0;
    if (!pic_spend(g) || !s || x < 0 || y < 0 || x + w > g->w || y + g->text_h > g->h) return x;   /* (whole or not at all) */
    return g_text(&cv, q->x + x, q->y + y, GF_S, s, plug_col(col)) - q->x;
}
static int pic_text_w(const KDraw *g, const char *s) { (void)g; return s ? g_text_w(GF_S, s) : 0; }
static int pic_scope(const KDraw *g, int8_t *dst, int n) { (void)g; return k_audio_scope(dst, n); }

static void plug_texts(KApp *a, KPage pg, int i, const KWidget *w, const KParamDesc *d, const char *lab, KRect r, int lit, int hot, int *top, int *bot)
{
    const int lh = g_text_h(GF_S);
    const uint32_t faint = k_mix(th.bg, th.text, 35);
    char val[16], vu[24];
    const char *unit;
    *top = r.y; *bot = r.y + r.h;
    if (!(w->style & K_ST_NOLABEL)) { g_text_box(&cv, r.x, r.y, r.w, lh + 1, GF_S, lab, hot ? th.text : lit ? th.mid : faint, 0); *top += lh + 1; }
    if (!(w->style & K_ST_NOVALUE) && i >= 0) {
        ui_item_value(a, pg, i, val, sizeof val, &unit);
        snprintf(vu, sizeof vu, "%s%s", val, d->fmt == KF_ENUM ? "" : unit);
        g_text_box(&cv, r.x, *bot - lh - 1, r.w, lh + 1, GF_S, vu, hot ? th.accent : lit ? k_mix(th.text, th.accent, 70) : faint, 0);
        *bot -= lh + 2;
    }
}
/* An effect's layout (K_LPAGE_FX) is drawn for one insert: plug_fx its slot (-1: an engine's layout), plug_fx_lit whether that
 * insert is in hand (its widgets lit as the knobs' window reaches them). A widget's parameter is the FX page's item that holds it. */
int plug_fx = -1, plug_fx_lit;
static const KParamDesc FX_MIX_DESC = {"MIX", KF_PCT, 0, 100, 50, 0, 0};
static int fx_item_of(const KApp *a, int slot, int param)   /* the FX page's item for an insert's parameter (K_WP_MIX: its MIX), or -1 */
{
    const int n = k_page_count(a, K_PAGE_FX);
    int item;
    for (item = 0; item < n; item++) {
        int s2 = -1, p2 = -3;
        k_fx_item(a, item, &s2, &p2);
        if (s2 == slot && p2 == param) return item;
    }
    return -1;
}
static int fx_item_lit(const KApp *a, int item)   /* the knobs (or the cursor's row) hold it */
{
    const int vis = k_params_visible(a);
    return item >= 0 && plug_fx_lit && (vis <= 0 || (item >= a->win && item < a->win + vis));
}
static void plug_widget(KApp *a, const KWidget *w, KRect cell, int mode)   /* mode 0 SOUND, 1 EDIT, 2 both on SOUND (K_LAY_SYNTH_ALL); plug_fx >= 0: an insert's */
{
    const int edit = mode == 1, all = mode == 2, fx = plug_fx >= 0;
    const int ftype = fx ? a->params[a->track][K_PAGE_FX][plug_fx * 4] : 0;
    const KEngine *en = fx ? k_effect_at(ftype)->desc : k_engine_at(a->sound[a->track]);
    const int flat = w->style & K_ST_FLAT, row = a->win / 4;
    const uint32_t faint = k_mix(th.bg, th.text, 35), trough = k_mix(th.bg, th.text, flat ? 10 : 14);
    const KPage pg = fx ? K_PAGE_FX : edit ? K_PAGE_EDIT : K_PAGE_SOUND;
    KRect r = {cell.x + 2, cell.y + 2, cell.w - 4, cell.h - 4};
    int i = -1, lit = 0, hot = 0, v = 0, top, bot, ri = -1, pct = 0;
    KPage rpg = pg;   /* where its value is read (both pages on SOUND: EDIT's from EDIT) */
    const KParamDesc *d = 0;
    const char *lab = w->text;
    char lb[24];
    if (r.w < 4 || r.h < 4) return;
    if (fx && w->kind != K_W_LABEL && w->kind != K_W_SCOPE && w->kind != K_W_PICTURE && w->kind < K_W_COUNT) {   /* an insert's: its item */
        d = w->param == K_WP_MIX ? &FX_MIX_DESC : &en->edit[w->param];
        i = ri = fx_item_of(a, plug_fx, w->param);
        if (i >= 0) {
            lit = fx_item_lit(a, i) || (w->kind == K_W_ENV && fx_item_lit(a, fx_item_of(a, plug_fx, w->param + 3)));
            hot = lit && (hot_param(a, i) || touched_recent(a, i));
            v = k_param_get(a, K_PAGE_FX, i);
            pct = v < 0 ? 0 : v > 100 ? 100 : v;   /* (an insert's values are kept 0 .. 100) */
            k_set_bounds(a, K_NODE_PARAM, i, cell);
        }
        if (!lab) lab = card_label(d->label, lb, sizeof lb);
    } else if (!fx && w->kind != K_W_LABEL && w->kind != K_W_SCOPE && w->kind != K_W_PICTURE && w->kind < K_W_COUNT) {
        const int on_page = all || (edit ? w->param >= en->page2 : w->param < en->page2);
        d = &en->edit[w->param];
        if (on_page) {
            i = edit ? w->param - en->page2 : w->param;
            lit = all || i / 4 == row || (w->kind == K_W_ENV && (i + 3) / 4 == row);
            hot = lit && (hot_param(a, i) || touched_recent(a, i));
            rpg = all && w->param >= en->page2 ? K_PAGE_EDIT : pg;
            ri = all && w->param >= en->page2 ? w->param - en->page2 : i;
            v = k_param_get(a, rpg, ri);
            pct = param_pct(d, v);
            k_set_bounds(a, K_NODE_PARAM, i, cell);
        }
        if (!lab) lab = card_label(d->label, lb, sizeof lb);
    }
    if (w->style & K_ST_FRAME) {
        if (flat) { g_fill(&cv, cell.x + 1, cell.y + 1, cell.w - 2, 1, th.line); g_fill(&cv, cell.x + 1, cell.y + cell.h - 2, cell.w - 2, 1, th.line);
                    g_fill(&cv, cell.x + 1, cell.y + 1, 1, cell.h - 2, th.line); g_fill(&cv, cell.x + cell.w - 2, cell.y + 1, 1, cell.h - 2, th.line); }
        else g_rrect_frame(&cv, cell.x + 1, cell.y + 1, cell.w - 2, cell.h - 2, 3, th.line, th.bg);
        r = (KRect){r.x + 2, r.y + 2, r.w - 4, r.h - 4};
    }
    switch (w->kind) {
    case K_W_LABEL:
        g_text_box(&cv, r.x, r.y, r.w, r.h, w->style & K_ST_LARGE ? GF_M : GF_S, lab ? lab : "", w->style & K_ST_ACCENT ? th.accent : th.mid, -1);
        break;
    case K_W_VALUE: {
        char val[16], vu[24];
        const char *unit;
        const int lh = g_text_h(GF_S), big = w->style & K_ST_LARGE ? GF_L : GF_M;
        if (!(w->style & K_ST_NOLABEL)) { g_text_box(&cv, r.x, r.y, r.w, lh + 1, GF_S, lab, hot ? th.text : lit ? th.mid : faint, 0); r.y += lh + 1; r.h -= lh + 1; }
        if (ri < 0) break;
        ui_item_value(a, rpg, ri, val, sizeof val, &unit);
        snprintf(vu, sizeof vu, "%s%s", val, d->fmt == KF_ENUM ? "" : unit);
        g_text_box(&cv, r.x, r.y, r.w, r.h, g_text_h((GFont)big) <= r.h ? (GFont)big : GF_S, vu, hot ? th.accent : lit ? th.text : faint, 0);
        break; }
    case K_W_KNOB: {
        int rad;
        plug_texts(a, rpg, ri, w, d, lab, r, lit, hot, &top, &bot);
        rad = ((bot - top) < r.w ? (bot - top) : r.w) / 2 - 1;
        if (rad > 20) rad = 20;
        if (rad >= 5)
            flat_knob(r.x + r.w / 2, top + (bot - top) / 2, rad, pct, d->min < 0 && d->max > 0, lit ? th.accent : faint,
                      flat ? th.bg : lit ? k_mix(th.bg, th.text, 26) : k_mix(th.bg, th.text, 12));
        break; }
    case K_W_SWITCH: {
        int sw, sh, sx, sy;
        const int on = d->fmt == KF_ONOFF ? k_switch_on(d, v) : v != d->min;
        plug_texts(a, rpg, ri, w, d, lab, r, lit, hot, &top, &bot);
        sw = r.w < 22 ? r.w : 22; sh = 11; sx = r.x + (r.w - sw) / 2; sy = top + (bot - top - sh) / 2;
        if (bot - top < sh) break;
        if (flat) {
            g_fill(&cv, sx, sy, sw, sh, on ? (lit ? th.accent : faint) : trough);
            g_fill(&cv, on ? sx + sw - sh + 2 : sx + 2, sy + 2, sh - 4, sh - 4, lit ? th.bg : k_mix(th.bg, th.text, 30));
        } else {
            g_rrect(&cv, sx, sy, sw, sh, sh / 2, on ? (lit ? th.accent : faint) : trough);
            g_rrect(&cv, on ? sx + sw - sh + 1 : sx + 1, sy + 1, sh - 2, sh - 2, (sh - 2) / 2, lit ? th.text : faint);
        }
        break; }
    case K_W_SLIDER: {
        const int bip = d->min < 0 && d->max > 0;
        const uint32_t fillc = lit ? th.accent : faint;
        plug_texts(a, rpg, ri, w, d, lab, r, lit, hot, &top, &bot);
        if (bot - top < 6) break;
        if (r.h > r.w) {   /* upright */
            const int bw = flat ? (r.w > 22 ? 22 : r.w - 2) : 4, bx = r.x + (r.w - bw) / 2, h = bot - top - 2, y0 = top + 1;
            const int at = y0 + h - pct * h / 100, mid = y0 + h / 2;
            if (flat) g_fill(&cv, bx, y0, bw, h, trough); else g_rrect(&cv, bx, y0, bw, h, 2, trough);
            if (bip) g_fill(&cv, bx, at < mid ? at : mid, bw, at < mid ? mid - at : at - mid + 1, fillc);
            else g_fill(&cv, bx, at, bw, y0 + h - at, fillc);
            if (!flat) g_rrect(&cv, r.x + r.w / 2 - 8, at - 3, 16, 6, 2, lit ? th.text : faint);
        } else {           /* lying */
            const int bh = flat ? ((bot - top) > 14 ? 14 : bot - top - 2) : 4, by = top + (bot - top - bh) / 2, wd = r.w - 2, x0 = r.x + 1;
            const int at = x0 + pct * wd / 100, mid = x0 + wd / 2;
            if (flat) g_fill(&cv, x0, by, wd, bh, trough); else g_rrect(&cv, x0, by, wd, bh, 2, trough);
            if (bip) g_fill(&cv, at < mid ? at : mid, by, at < mid ? mid - at : at - mid + 1, bh, fillc);
            else g_fill(&cv, x0, by, at - x0, bh, fillc);
            if (!flat) g_rrect(&cv, at - 3, r.y + (r.h - 14) / 2 + (top - r.y) / 2, 6, 14, 2, lit ? th.text : faint);
        }
        break; }
    case K_W_ENV: {
        int k, q[4], px[5], py[5];
        const uint32_t c = lit ? th.accent : faint;
        const int seg = r.w / 4, base = r.y + r.h - 2, hi = r.y + 2;
        if (r.h < 12) break;
        for (k = 0; k < 4; k++) {
            const KParamDesc *e = &en->edit[w->param + k];
            const int pk = w->param + k, ed = pk >= en->page2;
            if (fx) { const int it = fx_item_of(a, plug_fx, pk), fv = it >= 0 ? k_param_get(a, K_PAGE_FX, it) : 50; q[k] = fv < 0 ? 0 : fv > 100 ? 100 : fv; }
            else q[k] = (all || edit == ed) ? param_pct(e, k_param_get(a, ed ? K_PAGE_EDIT : K_PAGE_SOUND, ed ? pk - en->page2 : pk)) : param_pct(e, e->def);
        }
        px[0] = r.x; py[0] = base;
        px[1] = px[0] + 2 + q[0] * (seg - 2) / 100; py[1] = hi;
        px[2] = px[1] + 2 + q[1] * (seg - 2) / 100; py[2] = base - q[2] * (base - hi) / 100;
        px[3] = r.x + r.w - seg; py[3] = py[2];
        px[4] = px[3] + 2 + q[3] * (seg - 3) / 100; py[4] = base;
        if (px[3] < px[2]) px[3] = px[2];
        if (!flat) g_fill(&cv, r.x, base + 1, r.w, 1, k_mix(th.bg, th.text, 12));
        for (k = 0; k < 4; k++) g_line(&cv, px[k], py[k], px[k + 1], py[k + 1], flat ? 1 : 2, c);
        break; }
    case K_W_SCOPE: {
        int8_t sm[64];
        int n = k_audio_scope(sm, r.w / 2 < 64 ? r.w / 2 : 64), k, mid = r.y + r.h / 2, amp = r.h / 2 - 1, pk = 8;
        for (k = 0; k < n; k++) pk = sm[k] > pk ? sm[k] : -sm[k] > pk ? -sm[k] : pk;   /* a quiet sound shown as large (up to 16 x) */
        amp = amp * 120 / (pk < 8 ? 8 : pk);
        const uint32_t c = th.accent;
        plug_scope_on = 1;
        if (!flat) g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 3, k_mix(th.bg, th.text, 12), k_mix(th.bg, th.text, 4));
        g_fill(&cv, r.x + 1, mid, r.w - 2, 1, k_mix(th.bg, th.text, flat ? 10 : 16));
        for (k = 1; k < n; k++)
            g_line(&cv, r.x + 1 + (k - 1) * (r.w - 3) / (n - 1), mid - sm[k - 1] * amp / 128,
                   r.x + 1 + k * (r.w - 3) / (n - 1), mid - sm[k] * amp / 128, 1, c);
        break; }
    case K_W_PICTURE: {
        PlugPic q;
        int16_t vals[K_ENGINE_PARAMS];
        int k, lo = 0;
        for (k = 0; k < en->nparams && k < K_ENGINE_PARAMS; k++) {
            const int on_sound = k < en->page2, it = fx ? fx_item_of(a, plug_fx, k) : -1;
            const int pv = fx ? (it >= 0 ? k_param_get(a, K_PAGE_FX, it) : 50) : k_param_get(a, on_sound ? K_PAGE_SOUND : K_PAGE_EDIT, on_sound ? k : k - en->page2);
            vals[k] = (int16_t)pv;
            if (fx ? fx_item_lit(a, it) : all || (edit ? !on_sound && (k - en->page2) / 4 == row : on_sound && k / 4 == row)) lo = 1;
        }
        memset(&q, 0, sizeof q);
        q.g.size = sizeof(KDraw); q.g.version = 1; q.g.w = (int16_t)r.w; q.g.h = (int16_t)r.h;
        q.g.screen = (uint8_t)k_screen_kind(cv.w, cv.h); q.g.lit = (uint8_t)lo; q.g.text_h = (int16_t)g_text_h(GF_S);
        q.g.fill = pic_fill; q.g.line = pic_line; q.g.text = pic_text; q.g.text_w = pic_text_w; q.g.scope = pic_scope; q.g.host = &q;
        q.x = r.x; q.y = r.y; q.left = 400;
        plug_scope_on = 1;
        if (fx) k_effect_draw(ftype, &q.g, w->param, vals, en->nparams);
        else k_plugin_draw(a->sound[a->track], &q.g, w->param, vals, en->nparams);
        break; }
    default: break;   /* (a widget this host does not know: its place stays empty) */
    }
}
void plug_layout(KApp *a, const KLayout *L, KRect area, int mode)
{
    int k;
    for (k = 0; k < L->n; k++) {
        const KWidget *w = &L->w[k];
        const int x0 = area.x + area.w * w->x / L->cols, x1 = area.x + area.w * (w->x + w->w) / L->cols;
        const int y0 = area.y + area.h * w->y / L->rows, y1 = area.y + area.h * (w->y + w->h) / L->rows;
        plug_widget(a, w, (KRect){x0, y0, x1 - x0, y1 - y0}, mode);
    }
}
int plugin_layout_view(KApp *a)
{
    const int t = a->track, edit = a->page == K_PAGE_EDIT, scr = k_screen_kind(cv.w, cv.h);
    const KLayout *L, *E;
    const KEngine *en;
    KRect p = pic, area;
    int drum = 0;
    char lane[16];
    if (k_track_missing(a, t)) return 0;
    if (!edit && k_is_drum(a, t) && (L = k_plugin_layout(a->sound[t], K_LPAGE_DRUM, scr)) != 0) drum = 1;   /* a drum's: its lane page */
    else L = k_plugin_layout(a->sound[t], edit ? K_LPAGE_EDIT : K_LPAGE_SOUND, scr);
    if (!L) return 0;
    en = k_engine_at(a->sound[t]);
    if (drum) snprintf(lane, sizeof lane, "Lane %d  %s", a->cur_lane[t] + 1, lane_label(a, t, a->cur_lane[t]));   /* (the lane the keys play) */
    if ((a->lay & K_LAY_SYNTH_ALL) && !edit) {   /* a big screen's SOUND holds EDIT's too: the two tables one over the other */
        const int lh = drum ? g_text_h(GF_S) + 2 : 0, top = whole.y + 32 + lh, h = whole.y + whole.h - 4 - top;
        E = k_plugin_layout(a->sound[t], K_LPAGE_EDIT, scr);
        wide_head(a, K_PAGE_SOUND, 0, 0, en, t);
        if (drum) g_text(&cv, whole.x + 10, top - lh, GF_S, lane, th.accent);
        plug_layout(a, L, (KRect){whole.x + 8, top, whole.w - 16, E ? h / 2 : h}, 2);
        if (E) {
            g_fill(&cv, whole.x + 10, top + h / 2, whole.w - 20, 1, k_mix(th.bg, th.text, 12));
            plug_layout(a, E, (KRect){whole.x + 8, top + h / 2 + 2, whole.w - 16, h - h / 2 - 2}, 2);
        }
        return 1;
    }
    g_fill(&cv, p.x, p.y, p.w, p.h, th.bg);
    preset_line(a, p.x + 6, p.y, p.x + p.w - 6);
    if (!edit) {
        char eb[24];
        const int ew = (snprintf(eb, sizeof eb, "<  %s  >", en->name), g_text_w(GF_S, eb));
        g_text(&cv, p.x + p.w - 6 - ew, p.y + 3, GF_S, eb, th.accent);
        k_set_bounds(a, K_NODE_SOUND, 0, (KRect){p.x + p.w - 8 - ew, p.y, ew + 4, g_text_h(GF_M)});
    }
    area = (KRect){p.x + 2, p.y + g_text_h(GF_M) + 4, p.w - 4, 0};
    if (drum) { g_text(&cv, area.x + 4, area.y, GF_S, lane, th.accent); area.y += g_text_h(GF_S) + 2; }
    area.h = p.y + p.h - area.y;
    plug_layout(a, L, area, edit);
    return 1;
}
/* VECTOR's display is host-owned. Besides X/Y, show the *assigned*
 * four wavetable names: position without corner identity was too ambiguous.
 * This is a visualisation only; the knobs, focus and engine logic are unchanged. */
static void vector_xy_pad(const KApp *a, KRect r)
{
    const KEngine *en = k_engine_at(a->sound[a->track]);
    const KVal *p = a->params[a->track][K_PAGE_SOUND] + 4;
    int side = r.h - 9, x, y, ix, iy, span, dx, dy, vx, vy, legend;
    static const char *const tags[4] = {"A", "B", "C", "D"};
    if (side > 78) side = 78;
    if (side > r.w / 2 - 12) side = r.w / 2 - 12;
    if (side < 25) return;
    x = r.x + r.w - side - 18;
    y = r.y + (r.h - side) / 2;
    legend = x - r.x - 7;
    vx = p[0] < 0 ? 0 : p[0] > 127 ? 127 : p[0];
    vy = p[1] < 0 ? 0 : p[1] > 127 ? 127 : p[1];
    ix = x + 5; iy = y + 5; span = side - 10;
    dx = ix + vx * (span - 1) / 127;
    dy = iy + vy * (span - 1) / 127;
    g_text(&cv, r.x + 5, r.y + 2, GF_S, a->page == K_PAGE_EDIT ? "CORNER WAVES" : "VECTOR WT", th.mid);
    for (int k = 0; k < 4; k++) {
        int wt = p[6 + k];
        char b[26];
        const KParamDesc *desc = &en->edit[6 + k];
        const char *name = desc->names && wt >= desc->min && wt <= desc->max ? desc->names[wt - desc->min] : "?";
        int cw = (legend - 10) / 2, tx = r.x + 5 + (k & 1) * (cw + 5);
        int ty = r.y + (r.h < 58 ? 17 : (r.h - 30) / 2) + (k / 2) * 15;
        snprintf(b, sizeof b, "%s %s", tags[k], name);
        g_text_box(&cv, tx, ty, cw, 13, GF_S, b, th.text, -1);
    }
    g_rrect_frame(&cv, x, y, side, side, 2, th.line, k_mix(th.bg, th.text, 6));
    g_line(&cv, ix, iy + span / 2, ix + span - 1, iy + span / 2, 1, th.line);
    g_line(&cv, ix + span / 2, iy, ix + span / 2, iy + span - 1, 1, th.line);
    g_fill(&cv, dx - 3, dy - 3, 7, 7, k_mix(th.bg, th.accent, 40));
    g_fill(&cv, dx - 1, dy - 1, 3, 3, th.accent);
    g_text(&cv, x - 9, y - 1, GF_S, "A", th.mid);
    g_text(&cv, x + side + 1, y - 1, GF_S, "B", th.mid);
    g_text(&cv, x - 9, y + side - 11, GF_S, "C", th.mid);
    g_text(&cv, x + side + 1, y + side - 11, GF_S, "D", th.mid);
}
/* the preset's line over a synth page: its name (no icon), then the page's tabs (1 2 ..: the turns its page key gives, the one in
 * hand lit) where the page has more than one */
void preset_line(const KApp *a, int x, int y, int xr)
{
    const int lh = g_text_h(GF_M);
    int at, n = k_page_tabs(a, &at), k, tx;
    g_text(&cv, x, y, GF_M, trk_preset(a, a->track), th.text);
    tx = xr - n * 17 + 2;   /* the tabs at the right end */
    for (k = 0; k < n && k < 8; k++) {
        char b[4];
        const int on = k == at, bx = tx + k * 17;
        snprintf(b, sizeof b, "%d", k + 1);
        if (on) g_colored(&cv, bx, y + lh / 2 - 7, 15, 14, 3, th.text);
        else g_panel(&cv, bx, y + lh / 2 - 7, 15, 14, 3, k_mix(th.bg, th.text, 14), 0);
        g_text_box(&cv, bx, y + lh / 2 - 7, 15, 14, GF_S, b, on ? th.ink : th.mid, 0);
    }
}
void pic_synth_vanilla(KApp *a)
{
    const int t = a->track, k = a->sound[t], row = a->win / 4, lh = g_text_h(GF_S), mx = k_track_missing(a, t), edit = a->page == K_PAGE_EDIT; (void)row; (void)lh;
    const KEngine *en = k_engine_at(k);
    const int vector = !mx && en->uid == K_UID('K', 'V', 'E', 'C');
    const int nitems = edit ? (mx ? 0 : en->nparams - en->page2) : (mx ? 0 : en->page2), nrows = (nitems + 3) / 4;   /* (the engine: ALGORITHM, named over them) */
    int r, c, y, rh;
    KRect p = pic;
    g_fill(&cv, p.x, p.y, p.w, p.h, th.bg);
    preset_line(a, p.x + 6, p.y, p.x + p.w - 6);   /* the preset over SOUND and EDIT (as the panels), the page's tabs beside it */
    if (!edit) {   /* the engine at the right of it: its node (a D-pad's, a touch's) */
        char eb[24];
        const int ew = (snprintf(eb, sizeof eb, "<  %s  >", mx ? gone_word(a, a->track) : en->name), g_text_w(GF_S, eb));
        g_text(&cv, p.x + p.w - 6 - ew, p.y + 3, GF_S, eb, mx ? th.rec : th.accent);
        k_set_bounds(a, K_NODE_SOUND, 0, (KRect){p.x + p.w - 8 - ew, p.y, ew + 4, g_text_h(GF_M)});
    }
    y = p.y + g_text_h(GF_M) + 4;
    if (vector) {
        /* SOUND needs room for two live knob rows. EDIT has one row:
         * spend its otherwise empty area on a larger corner assignment map. */
        const int banner = edit ? (p.h >= 165 ? 95 : 66) : (p.h >= 165 ? 70 : 48);
        KRect vr = {p.x + 5, y, p.w - 10, banner};
        vector_xy_pad(a, vr);
        y += banner;
    }
    rh = (p.y + p.h - y) / (nrows ? nrows : 1);
    for (r = 0; r < nrows; r++) {
        int on = r == row;
        if (vector && on && rh >= 23)
            g_rrect_frame(&cv, p.x + 2, y + 1, p.w - 4, rh - 2, 3,
                          k_mix(th.bg, th.accent, 75), k_mix(th.bg, th.accent, 5));
        else if (r) g_fill(&cv, p.x + 2, y, p.w - 4, 1, k_mix(th.bg, th.text, 12));
        for (c = 0; c < 4; c++) {
            int i = r * 4 + c, x0 = p.x + p.w * c / 4, x1 = p.x + p.w * (c + 1) / 4, hot = on && (hot_param(a, i) || touched_recent(a, i));
            KRect cr = {x0, y + 2, x1 - x0, rh - 2};
            if (i >= nitems) break;
            k_set_bounds(a, K_NODE_PARAM, i, cr);
            synth_control(a, edit ? K_PAGE_EDIT : K_PAGE_SOUND, i, cr,
                                 on || vector, hot, vector && cv.h <= 200);
            /* Instrument pages keep both knob rows legible; the compact
             * handheld control saves one text line so no ring is clipped. */
        }
        y += rh;
    }
}
