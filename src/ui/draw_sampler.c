/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler's SOUND page (Linux and macOS, K_WITH_SAMPLES; never the FM-1's build: this file is not in its groups). A look of its
 * own, after the owner's ask for something bold (2026-11-14): flat blocks of colour on black, the waveform as large as the screen
 * allows, few words. From the top: a bar (the engine's tag, the file, its length), the waveform (START, END and the loop marked
 * in their controls' colours, the voices' playheads; while recording, what comes in, in red, with the input's level), then the
 * controls as tiles, a colour a column: the row in hand full and large, the other row small. It is laid out from the screen's
 * size: the Cardputer Zero's 320 x 170 (the keys alone), the H700's 320 x 240, a desktop's 640 x 360 (the mouse: each tile a
 * target, and there EDIT's row too, as that screen joins the pages). */
#include "draw_int.h"
#if K_WITH_SAMPLES && !defined(KREISEL_BITMAP_FONT)
#include "sample/k_rec.h"
#include "sample/k_smp.h"
#include "sample/k_browse.h"

#define SM_BG 0x0B0B0B
#define SM_WELL 0x171716
#define SM_DIM 0x4C4B47
#define SM_PAPER 0xEEEBE3
#define SM_INK 0x111111
#define SM_RED 0xFF3B2F
static const uint32_t SM_COL[4] = {0xFF6A1F, 0xFFC93C, 0x2FC6B4, 0xEEEBE3};   /* the four columns: vermilion, sun, teal, paper */

KRect sampler_rec_btn, sampler_file_btn;
KRect sampler_rows[SAMPLER_ROWS];
int sampler_row_at[SAMPLER_ROWS], sampler_rows_n;
int sampler_live;   /* (the UI keeps drawing while the input is open: its level moves) */

/* the min and max of each column, kept while the sample and the width stay */
static struct { const int16_t *f; uint32_t n; int w, ch; int16_t lo[1280], hi[1280]; } pk;
static void peaks(const int16_t *f, uint32_t n, int ch, int w)
{
    int x;
    if (w > 1280) w = 1280;
    if (pk.f == f && pk.n == n && pk.w == w && pk.ch == ch) return;
    pk.f = f; pk.n = n; pk.w = w; pk.ch = ch;
    for (x = 0; x < w; x++) {
        const uint32_t a = (uint32_t)((uint64_t)n * (uint32_t)x / (uint32_t)w), b0 = (uint32_t)((uint64_t)n * (uint32_t)(x + 1) / (uint32_t)w);
        const uint32_t b = b0 > a ? b0 : a + 1, st = (b - a) > 512 ? (b - a) / 512 : 1;   /* (a long file: 512 looks a column) */
        int lo = 0, hi = 0;
        uint32_t i;
        for (i = a; i < b && i < n; i += st) {
            const int v = ch == 2 ? (f[2 * i] + f[2 * i + 1]) / 2 : f[i];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        pk.lo[x] = (int16_t)lo; pk.hi[x] = (int16_t)hi;
    }
}

/* column k's bar about the middle line, the same either way: the largest swing of it and its neighbours (a silhouette, not the
 * stripes a sound's cycles make when a column holds less than one of them), amp px up and down */
static void wave_col(int x, int k, int yc, int amp, uint32_t col)
{
    int q, a = 0, y0, y1;
    for (q = k - 1; q <= k + 1; q++) if (q >= 0 && q < pk.w) { const int m2 = -pk.lo[q] > pk.hi[q] ? -pk.lo[q] : pk.hi[q]; if (m2 > a) a = m2; }
    y0 = yc - (a * amp >> 15); y1 = yc + (a * amp >> 15) + 1;
    if (y1 <= y0) y1 = y0 + 1;
    g_fill(&cv, x, y0, 1, y1 - y0, col);
}

static void secs(char *b, size_t n, uint32_t frames, uint32_t rate)   /* "2.31s", "45ms" */
{
    const uint32_t ms = rate ? (uint32_t)((uint64_t)frames * 1000u / rate) : 0;
    if (ms < 1000) snprintf(b, n, "%ums", (unsigned)ms);
    else snprintf(b, n, "%u.%02us", (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 10));
}

static void meter(int x, int y, int w, int h, int lvl)   /* the input's level: 6 dB a step, the top two steps red */
{
    int bits = 0, k;
    while (bits < 15 && (lvl >> bits)) bits++;
    for (k = 0; k < 15; k++) {
        const int sy = y + h - (k + 1) * h / 15, sh = h / 15 - 1 > 0 ? h / 15 - 1 : 1;
        g_fill(&cv, x, sy, w, sh, k < bits ? (k >= 13 ? SM_RED : SM_PAPER) : 0x262624);
    }
}

/* the value as a tile shows it: a position as time in the file, an enum's name as a word */
static void tile_value(KApp *a, const KEngine *en, int i, const KSample *s, char *b, size_t n)
{
    const KParamDesc *d = &en->edit[i];
    const KPage pg = i < en->page2 ? K_PAGE_SOUND : K_PAGE_EDIT;   /* (EDIT's half: read as EDIT's items, where SOUND does not show them) */
    const int ii = i < en->page2 ? i : i - en->page2, v = k_param_get(a, pg, ii);
    const char *unit = "";
    char val[16];
    if (s && i < 4 && i != 2) {   /* STRT END LPST: where, in the file */
        const int64_t len = s->nframes, st = len * k_param_get(a, K_PAGE_SOUND, 0) / 128, e = len * (k_param_get(a, K_PAGE_SOUND, 1) + 1) / 128;
        const int64_t at = i == 0 ? st : i == 1 ? e : st + (e - st) * v / 128;
        secs(b, n, (uint32_t)at, s->rate);
        return;
    }
    if (d->fmt == KF_ONOFF) { snprintf(b, n, "%s", k_switch_name(d, v)); return; }
    ui_item_value(a, pg, ii, val, sizeof val, &unit);
    snprintf(b, n, "%s%s", val, d->fmt == KF_ENUM ? "" : unit);
    if (d->fmt == KF_ENUM && strlen(b) > 2) { size_t q; for (q = 1; b[q]; q++) if (b[q] >= 'A' && b[q] <= 'Z') b[q] = (char)(b[q] + 32); }   /* "Fwd", "Ping"; LP stays */
}

/* one tile: big (the row in hand: the colour full, the value large) or small (a strip of the colour, one line) */
static void tile(KApp *a, const KEngine *en, int i, int x, int y, int w, int h, uint32_t col, int big, const KSample *s)
{
    const KParamDesc *d = &en->edit[i];
    const int hot = hot_param(a, i) || touched_recent(a, i), v = i < en->page2 ? k_param_get(a, K_PAGE_SOUND, i) : k_param_get(a, K_PAGE_EDIT, i - en->page2);
    static const char *const NAME[14] = {"Start", "End", "Loop", "Loop st", "Root", "Tune", "Dir", "Key",
                                         "Cutoff", "Res", "Type", "Env", "Level", "Vel"};   /* (words, not the cards' four capitals) */
    char val[24];
    const char *name = i < 14 ? NAME[i] : ui_item_name(a, K_PAGE_SOUND, i);
    tile_value(a, en, i, s, val, sizeof val);
    if (big) {
        const uint32_t ink = SM_INK;
        g_fill(&cv, x, y, w, h, col);
        g_text(&cv, x + 4, y + 1, GF_S, name, k_mix(col, ink, 70));
        g_text(&cv, x + 4, y + h - g_text_h(GF_M) - 2, GF_M, val, ink);
        if (d->fmt != KF_ENUM && d->fmt != KF_ONOFF && d->max > d->min) {   /* how far: a bar along the foot */
            const int fw = (int)((int64_t)(v - d->min) * (w - 2) / (d->max - d->min));
            g_fill(&cv, x + 1, y + h - 2, w - 2, 1, k_mix(col, ink, 30));
            g_fill(&cv, x + 1, y + h - 2, fw, 1, ink);
        }
    } else {
        g_fill(&cv, x, y, w, h, 0x1C1C1B);
        g_fill(&cv, x, y, 3, h, col);
        const int vw = g_text_w(GF_S, val);
        g_text(&cv, x + w - 4 - vw, y + (h - g_text_h(GF_S)) / 2, GF_S, val, SM_PAPER);
        text_fit(x + 7, y + (h - g_text_h(GF_S)) / 2, GF_S, name, w - 11 - vw - 4, SM_DIM);   /* (the value first: the name gives way) */
    }
    if (hot) {   /* the one the keys turn: a frame of paper (black inside a lit tile) */
        const uint32_t fc = big ? SM_INK : SM_PAPER;
        g_fill(&cv, x, y, w, 2, fc); g_fill(&cv, x, y + h - 2, w, 2, fc); g_fill(&cv, x, y, 2, h, fc); g_fill(&cv, x + w - 2, y, 2, h, fc);
    }
    k_set_bounds(a, K_NODE_PARAM, i, (KRect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h});
}

/* the file browser (k_browse.h, TOOLS on the page): the folder's files down the left, a name and its length a row, the one in hand
 * on paper; at the right, on a screen wide enough, the waveform of the one in hand (the track has it already: the keys play it) */
static void browse_body(KApp *a, int x, int y, int w, int h, const KSample *s, int big_scr)
{
    const int n = k_browse_count(), sel = k_browse_sel(), rh = big_scr ? 20 : 15, pw = w >= 300 ? w * 2 / 5 : 0, lw = w - (pw ? pw + 6 : 0);
    const int vis = h / rh < SAMPLER_ROWS ? h / rh : SAMPLER_ROWS;
    int first = sel - vis / 2, i;
    char b[24], nm[K_SMP_NAME];
    (void)a;
    sampler_rows_n = 0;
    g_fill(&cv, x, y, w, h, SM_WELL);
    if (!n) {
        const int ty = y + h / 2 - g_text_h(GF_S);
        g_text_box(&cv, x, ty, w, 15, GF_S, "No WAV files", SM_PAPER, 0);
        text_fit(x + 8, ty + 18, GF_S, k_smp_dir(), w - 16, SM_DIM);
        return;
    }
    if (first > n - vis) first = n - vis;
    if (first < 0) first = 0;
    for (i = 0; i < vis && first + i < n; i++) {
        const int f = first + i, ry = y + i * rh, on = f == sel, ty = ry + (rh - g_text_h(GF_S)) / 2;
        const uint32_t ms = k_browse_ms(f), ink = on ? SM_INK : SM_PAPER;
        size_t l;
        int vw = 0;
        snprintf(nm, sizeof nm, "%s", k_browse_name(f));
        l = strlen(nm);
        if (l > 4) nm[l - 4] = 0;   /* (no ".wav": every one is) */
        if (on) g_fill(&cv, x, ry, lw, rh, SM_PAPER);
        else if (i) g_fill(&cv, x + 6, ry, lw - 12, 1, 0x232322);
        if (ms) {
            secs(b, sizeof b, ms, 1000);
            vw = g_text_w(GF_S, b);
            g_text(&cv, x + lw - 6 - vw, ty, GF_S, b, on ? SM_INK : SM_DIM);
        }
        if (on) g_fill(&cv, x, ry, 3, rh, SM_COL[0]);
        text_fit(x + 8, ty, GF_S, nm, lw - 16 - vw - 6, ink);
        sampler_rows[sampler_rows_n] = (KRect){(int16_t)x, (int16_t)ry, (int16_t)lw, (int16_t)rh};
        sampler_row_at[sampler_rows_n++] = f;
    }
    if (n > vis) {   /* where the window is in the list: a thin bar at the list's right edge */
        const int bh = h * vis / n < 6 ? 6 : h * vis / n, by = y + (h - bh) * first / (n - vis);
        g_fill(&cv, x + lw - 2, by, 2, bh, SM_DIM);
    }
    if (pw && s) {   /* the one in hand */
        const int px = x + lw + 6, yc = y + h / 2, amp = h / 2 - 4;
        int k;
        g_fill(&cv, px - 6, y, 6, h, SM_BG);
        peaks(s->frames, s->nframes, s->channels, pw);
        g_fill(&cv, px, yc, pw, 1, 0x262624);
        for (k = 0; k < pk.w; k++) wave_col(px + k, k, yc, amp, SM_PAPER);
        snprintf(b, sizeof b, "%u Hz%s", (unsigned)s->rate, s->channels == 2 ? " st" : "");
        g_text(&cv, px + 4, y + h - g_text_h(GF_S) - 2, GF_S, b, SM_DIM);
    }
}

int sampler_view(KApp *a)
{
    const int t = a->track;
    const KEngine *en;
    const KSample *s;
    const int x0 = whole.x, y0 = whole.y, W = whole.w, H = whole.h, big_scr = H >= 200;
    const int all = (a->lay & K_LAY_SYNTH_ALL) != 0;   /* (a screen that joins EDIT to SOUND: its row too) */
    const int m = big_scr ? 8 : 4, hb = big_scr ? 24 : 17, gap = big_scr ? 6 : 3;
    const int rows = all ? 4 : 2, row = a->win / 4;
    const int tb = big_scr ? 40 : 34, ts = big_scr ? 24 : 17;   /* a big tile's height, a small one's */
    const int th_all = tb + (rows - 1) * (ts + gap);
    const int wy = y0 + m + hb + gap, wh = H - (wy - y0) - gap - th_all - m;
    const int rec = k_rec_state(), live = k_rec_live(), browsing = k_browse_is_open() && k_browse_track() == a->track;
    char b[64];
    int k, c;
    if (a->page != K_PAGE_SOUND || k_track_missing(a, t) || k_engine_uid(a->sound[t]) != K_SAMPLER_UID || W < 200 || H < 120) return 0;
    en = k_engine_at(a->sound[t]);
    s = k_smp_sample(t);
    sampler_live = live;
    sampler_rec_btn.w = 0;
    sampler_file_btn.w = 0;
    sampler_rows_n = 0;
    g_fill(&cv, x0, y0, W, H, SM_BG);
    {   /* the bar: the tag, the file, its length; with a pointer or a finger, Rec at the right (the keys have REC) */
        const int tw = g_text_w(GF_S, "Sampler") + 14, by = y0 + m, ty = by + (hb - g_text_h(GF_S)) / 2;
        const int btn = (prof->input & (K_IN_MOUSE | K_IN_TOUCH)) != 0;
        const char *bl = rec != K_REC_IDLE ? "Stop" : "Rec";
        const int rw = btn ? g_text_w(GF_S, bl) + 18 : 0, re = x0 + W - m - (btn ? rw + 8 : 0);   /* (the right edge of the words) */
        int nx;
        g_fill(&cv, x0 + m, by, tw, hb, SM_COL[0]);
        g_text_box(&cv, x0 + m, by, tw, hb, GF_S, "Sampler", SM_INK, 0);
        if (browsing) {   /* the files: a teal block, how many, the one in hand's place among them */
            const int fw = g_text_w(GF_S, "Files") + 14, cnt = k_browse_count();
            g_fill(&cv, x0 + m + tw + 3, by, fw, hb, SM_COL[2]);
            g_text_box(&cv, x0 + m + tw + 3, by, fw, hb, GF_S, "Files", SM_INK, 0);
            if (cnt) snprintf(b, sizeof b, "%d / %d", k_browse_sel() + 1, cnt); else b[0] = 0;
            g_text(&cv, x0 + W - m - g_text_w(GF_S, b), ty, GF_S, b, SM_PAPER);
            browse_body(a, x0 + m, wy, W - 2 * m, y0 + H - m - wy, s, big_scr);
            return 1;
        }
        if (rec != K_REC_IDLE || !s) {   /* recording, or nothing yet: a block saying so */
            const char *w = rec == K_REC_ON ? "Rec" : rec == K_REC_WAIT ? "Wait" : k_smp_path(t)[0] ? "Missing" : "Empty";
            const uint32_t bc = rec == K_REC_ON ? SM_RED : rec == K_REC_WAIT ? (now / 400 & 1 ? SM_RED : 0x5A1712) : 0x2A2A28;
            const int ww = g_text_w(GF_S, w) + 14;
            g_fill(&cv, x0 + m + tw + 3, by, ww, hb, bc);
            g_text_box(&cv, x0 + m + tw + 3, by, ww, hb, GF_S, w, rec != K_REC_IDLE ? 0xFFFFFF : SM_PAPER, 0);
            nx = x0 + m + tw + 3 + ww + 8;
            if (rec == K_REC_IDLE) sampler_file_btn = (KRect){(int16_t)nx, (int16_t)by, (int16_t)(re - nx), (int16_t)hb};
            if (rec == K_REC_IDLE && k_smp_path(t)[0]) text_fit(nx, ty, GF_S, k_smp_path(t), re - nx, SM_DIM);
            else if (rec == K_REC_IDLE && !btn && (prof->input & K_IN_KEYBOARD)) keycap(nx, by + (hb - 16) / 2, "R", 0);
            if (rec == K_REC_ON) {   /* the time taken */
                secs(b, sizeof b, k_rec_frames(), k_rec_rate());
                g_text(&cv, re - g_text_w(GF_S, b), ty, GF_S, b, SM_RED);
            }
        } else {
            const int lw = (secs(b, sizeof b, s->nframes, s->rate), g_text_w(GF_S, b));
            nx = x0 + m + tw + 8;
            g_text(&cv, re - lw, ty, GF_S, b, SM_PAPER);
            text_fit(nx, ty, GF_S, s->name, re - lw - 8 - nx, SM_PAPER);
            sampler_file_btn = (KRect){(int16_t)nx, (int16_t)by, (int16_t)(re - lw - 8 - nx), (int16_t)hb};   /* (a click: the files) */
        }
        if (btn) {
            const int rx = x0 + W - m - rw;
            g_fill(&cv, rx, by, rw, hb, rec != K_REC_IDLE ? SM_PAPER : SM_RED);
            g_text_box(&cv, rx, by, rw, hb, GF_S, bl, rec != K_REC_IDLE ? SM_INK : 0xFFFFFF, 0);
            sampler_rec_btn = (KRect){(int16_t)rx, (int16_t)by, (int16_t)rw, (int16_t)hb};
        }
    }
    {   /* the waveform */
        const int mw = live ? (big_scr ? 8 : 5) : 0, wx = x0 + m, ww = W - 2 * m - (mw ? mw + 3 : 0), yc = wy + wh / 2, amp = wh / 2 - 3;
        g_fill(&cv, wx, wy, ww, wh, SM_WELL);
        g_fill(&cv, wx, yc, ww, 1, 0x262624);
        if (rec != K_REC_IDLE) {   /* what comes in, in red, the whole of it across */
            const uint32_t n = k_rec_frames();
            if (n > 1) {
                const int16_t *f = k_rec_data();
                const int w = (int)(n < (uint32_t)ww ? n : (uint32_t)ww);
                peaks(f, n, 1, w);
                for (k = 0; k < w; k++) wave_col(wx + k, k, yc, amp, SM_RED);
                pk.f = 0;   /* (it grows: looked at again next frame) */
            }
        } else if (s) {
            const int64_t n = s->nframes;
            const int64_t st = n * k_param_get(a, K_PAGE_SOUND, 0) / 128, e = n * (k_param_get(a, K_PAGE_SOUND, 1) + 1) / 128;
            const int loop = k_param_get(a, K_PAGE_SOUND, 2), ls = (int)(st + (e - st) * k_param_get(a, K_PAGE_SOUND, 3) / 128);
            const int xs = wx + (int)(st * ww / n), xe = wx + (int)(e * ww / n), xl = wx + (int)(ls * ww / n);
            uint32_t at[8];
            int nh;
            peaks(s->frames, s->nframes, s->channels, ww);
            if (loop) g_fill(&cv, xl, wy, xe - xl, wh, k_mix(SM_WELL, SM_COL[2], 22));   /* the loop: a teal field */
            for (k = 0; k < pk.w; k++) {
                const int px = wx + k;
                wave_col(px, k, yc, amp, px >= xs && px < xe ? SM_PAPER : SM_DIM);
            }
            /* the marks: a bar the height of the well, a block at its head in the control's colour */
            g_fill(&cv, xs, wy, 2, wh, SM_COL[0]); g_fill(&cv, xs, wy, 7, 7, SM_COL[0]);
            g_fill(&cv, xe - 2, wy, 2, wh, SM_COL[1]); g_fill(&cv, xe - 7, wy + wh - 7, 7, 7, SM_COL[1]);
            if (loop) { g_fill(&cv, xl, wy + 7, 1, wh - 7, SM_COL[2]); g_fill(&cv, xl, wy + wh - 7, 7, 7, SM_COL[2]); }
            nh = k_audio_sampler_heads(t, at, 8);
            for (k = 0; k < nh; k++) g_fill(&cv, wx + (int)((int64_t)at[k] * ww / n), wy, 1, wh, 0xFFFFFF);
            if (nh) sampler_live = 1;
        }
        if (mw) meter(wx + ww + 3, wy, mw, wh, k_rec_level());
    }
    {   /* the tiles: SOUND's two rows (and EDIT's, where the screen joins them), the row in hand large */
        int y = wy + wh + gap;
        for (k = 0; k < rows; k++) {
            const int r = k, n0 = r * 4, big = r == row, h = big ? tb : ts;
            int n = en->nparams - n0;
            if (!all && n0 >= en->page2) break;
            if (n > 4) n = 4;
            if (!all && n0 + n > en->page2) n = en->page2 - n0;
            for (c = 0; c < n; c++) {
                const int tw = (W - 2 * m - 3 * gap) / 4, tx = x0 + m + c * (tw + gap);
                tile(a, en, n0 + c, tx, y, tw, h, SM_COL[c], big, s);
            }
            y += h + gap;
        }
    }
    return 1;
}
#endif
