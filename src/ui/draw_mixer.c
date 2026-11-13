/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The levels, HOME (the tracks' rows), the mixer (its strips, faders, inserts) and the buses. */
#include "draw_int.h"

/* four values as bars, or as faders with handles */
/* a bus (BUS 1, BUS 2): its four values as bars (faders: as faders) */
void pic_levels(KApp *a, int faders)
{
    int i, n = k_param_count(a->page), bw = (pic.w - 2 * PIC_PAD - (n - 1) * 10) / n, gy = pic.y + PIC_PAD + 4, gh = pic.h - 2 * PIC_PAD - 8, x0, used;
    if (bw > 30) bw = 30;
    used = n * bw + (n - 1) * 10;
    x0 = pic.x + (pic.w - used) / 2;
    for (i = 0; i < n; i++) {
        int v = k_param_get(a, a->page, i), x = x0 + i * (bw + 10), hot = hot_param(a, i);   /* (by the values' table: a bus's are the song's) */
        if (faders) {
            int cx = x + bw / 2, hy = gy + gh - gh * v / 100;
            slot(cx, gy, gh);
            g_panel(&cv, cx - 7, hy - 7, 14, 14, 4, hot ? th.accent : th.text, 40);
            g_fill(&cv, cx - 3, hy - 1, 6, 2, th.ink);
        } else {
            g_sunken(&cv, x, gy, bw, gh, 4, th.bg);
            if (v > 0) g_colored(&cv, x, gy + gh - gh * v / 100, bw, gh * v / 100 < 4 ? 4 : gh * v / 100, 4, hot ? th.accent : th.sel);
        }
    }
}

/* the mixer, after Logic's strips: for each track the two sends (a pill and a knob each), the pan as a knob, the level in dB
 * with the track's number beside it, the fader with its ticks and the level meter. The cards under are the selected track's
 * rows (LEVEL, PAN, BUS1, BUS2); the controls of that row light up on every strip, and none of the strips is the one. What does not fit the height is left out: the pan, then the sends. */
/* the fill of a level meter at x, y (its top) of w x h px, lvl 0..100: green up to 70, yellow to 90, red above */
static void meter_fill(int x, int y, int w, int h, int lvl)
{
    static const int EDGE[3] = {0, 70, 90};
    uint32_t col[3] = {th.ok, 0xFFD60A, th.rec};
    int k, bottom = y + h;
    if (lvl > 100) lvl = 100;
    for (k = 0; k < 3; k++) {
        int hi = k == 2 ? lvl : (lvl < EDGE[k + 1] ? lvl : EDGE[k + 1]), lo = EDGE[k];
        int y0, y1;
        if (hi <= lo) continue;
        y0 = bottom - h * hi / 100; y1 = bottom - h * lo / 100;
        g_fill(&cv, x, y0, w, y1 - y0, col[k]);
    }
}
/* The mixer, in parts (as the FX page): a strip's values read from the values' table by id and track (mix_val); what it shows, a
 * part each: the level a track just sounded (track_meter), a send knob named under it (send_knob), an insert's chip (insert_chip),
 * M and S (ms_key), the strip's name on its colour (strip_foot); laid out as small strips (pic_mixer_strips: the FM-1, a
 * handheld) or a console's (pic_mixer_wide). The values change through k_param_set, k_mute_toggle, k_solo_toggle. */
static int mix_val(const KApp *a, int t, int id)   /* a strip's value (K_P_LEVEL, K_P_PAN, K_P_SEND1, K_P_SEND2; K_P_BUS1 + 3 a bus's) */
{
    const KParamRef r = {(uint8_t)id, (uint8_t)t, 0, 0, 0};
    return *k_param_at((KApp *)a, r);
}
static int track_meter(const KApp *a, int t)   /* the level a track just sounded, 0..127: the audio's (k_set_track_peaks: every note, played
                                                 * or sequenced); none yet, its steps, falling over 450 ms */
{
    const uint32_t age = now - a->hit_ms[t];
    if (a->trk_live) return a->trk_lv[t] * 127 / 100;
    return age < 450u ? (int)((uint32_t)a->hit_v[t] * (450u - age) / 450u) : 0;
}
static void send_knob(int cx, int cy, int r, int v, int on, int lx, int ly, int lw, int lh, int k)   /* B1 / B2: a knob, its name under it */
{
    knob(cx, cy, r, v, 0, on ? th.accent : th.ok, th.line, th.text);
    g_text_box(&cv, lx, ly, lw, lh, GF_S, k ? "B2" : "B1", on ? th.accent : th.mid, 0);
}
static void ms_key(int x, int y, int w, int h, const char *label, int on, uint32_t lit)   /* M or S: lit in its colour when on */
{
    g_colored(&cv, x, y, w, h, 4, on ? lit : k_mix(th.bg, th.text, 12));
    g_text_box(&cv, x, y, w, h, GF_S, label, on ? th.ink : th.mid, 0);
}
static void insert_chip(int x, int y, int w, int h, int type, int named)   /* an insert slot: its effect's name in its colour ('-' none) */
{
    g_rrect_frame(&cv, x, y, w, h, 3, type ? fx_accent(type) : k_mix(th.bg, th.text, 14), th.bg);
    if (named) {   /* its name, as many letters as the slot holds (a narrow strip: WOBBLE to WOB.) */
        char nm[16];
        int len;
        snprintf(nm, sizeof nm, "%s", type ? fx_name(type) : "-");
        for (len = (int)strlen(nm); len > 1 && g_text_w(GF_S, nm) > w - 4; len--) nm[len - 1] = 0;
        if (len > 2 && len < (int)strlen(type ? fx_name(type) : "-")) nm[len - 1] = '.';   /* (cut: it says so, WOBBLE to WOB.; the a11y audit) */
        g_text_box(&cv, x, y, w, h, GF_S, nm, type ? fx_accent(type) : th.dim, 0);
    }
}
static void strip_foot(int x, int y, int w, int h, int ty, int th_, uint32_t bar, const char *name, uint32_t ink)   /* a strip's name on its colour */
{
    g_colored(&cv, x, y, w, h, 4, bar);
    g_text_box(&cv, x, ty, w, th_, GF_S, name, ink, 0);
}
static void pic_mixer_strips(KApp *a)
{
    int t, n = K_TRACKS, gap = 4, sw = (pic.w - (n - 1) * gap) / n, room = pic.h - 40 - ((a->lay & K_LAY_MIX_KEYS) ? 18 : 60);   /* what is left after the footer and a fader of at least 50 (the Zero: shorter, so Pan, B1 and B2 fit) */
    int show_sends = room >= 40, show_pan;
    room -= show_sends ? 40 : 0;
    show_pan = room >= 28;
    for (t = 0; t < n; t++) {
        int x0 = pic.x + t * (sw + gap), y = pic.y + 4, cx = x0 + sw / 2, v = mix_val(a, t, K_P_LEVEL), k;
        int pan = mix_val(a, t, K_P_PAN), gy, gh, hy, tr, ty0, yb = pic.y + pic.h - 3 - 13 - 1, fx = cx - 5, mx = fx + 14;   /* the fader and its meter side by side, the pair centred under the pan knob */
        int mine = t == a->track;   /* the cards are the track in hand's: its controls light when one is touched */
        int tv = mine && card_wanted(a, 0), tp = mine && card_wanted(a, 1), lvl;
        k_set_bounds(a, K_NODE_TRACK, t, (KRect){x0, pic.y, sw, pic.h});
        if (t > 0 && t != a->track && t - 1 != a->track) g_fill(&cv, x0 - gap / 2 - 1, pic.y + 2, 1, pic.h - 4, th.line);   /* (none beside the strip in hand: its footer bar says which it is) */
        if (t == a->track) {   /* the track in hand: a 1 px outline in its colour, corners cut; nothing tinted inside */
            uint32_t hc = th.text;   /* (the strip in hand: the app's mark, its track named by the bar at its foot) */
            g_fill(&cv, x0 + 2, pic.y, sw - 4, 1, hc); g_fill(&cv, x0 + 2, pic.y + pic.h - 1, sw - 4, 1, hc);
            g_fill(&cv, x0, pic.y + 2, 1, pic.h - 4, hc); g_fill(&cv, x0 + sw - 1, pic.y + 2, 1, pic.h - 4, hc);
            g_fill(&cv, x0 + 1, pic.y + 1, 1, 1, hc); g_fill(&cv, x0 + sw - 2, pic.y + 1, 1, 1, hc);
            g_fill(&cv, x0 + 1, pic.y + pic.h - 2, 1, 1, hc); g_fill(&cv, x0 + sw - 2, pic.y + pic.h - 2, 1, 1, hc);
        }
        if (show_sends) {   /* the two buses: a large knob each, side by side, named under it */
            int r = sw / 4 - 2 > 9 ? 9 : sw / 4 - 2;   /* (never past the pan's 12: a wide strip made them the bigger) */
            for (k = 0; k < 2; k++) {
                const int on = mine && card_wanted(a, 2 + k), bx = x0 + sw * (2 * k + 1) / 4;
                send_knob(bx, y + r, r, mix_val(a, t, K_P_SEND1 + k), on, bx - 12, y + 2 * r + 1, 24, 10, k);
                if (mine && no_cards(a)) k_set_bounds(a, K_NODE_PARAM, 2 + k, (KRect){bx - r - 2, y - 2, 2 * r + 4, 2 * r + 14});   /* (no cards: the cursor
                                                 * on the strip's own parts) */
            }
            y += 2 * r + 11 + 6;
        }
        if (show_pan) {   /* the pan: a larger knob, bipolar */
            knob(cx, y + 16, 12, pan, 1, tp ? th.accent : th.ok, th.line, th.text);   /* larger */
            if (mine && no_cards(a)) k_set_bounds(a, K_NODE_PARAM, 1, (KRect){cx - 14, y + 2, 28, 28});
            y += 24;   /* (the fader's own inset is the gap under the pan) */
        }
        gy = y; gh = yb - gy;
        if (gh < 10) gh = 10;
        /* the fader: the rail as the strip's room allows; the handle's middle travels from half a handle (10 px) under the rail's
         * top to half a handle over its foot, so the handle never runs past either end */
        ty0 = gy + 10;   /* the top of the rail */
        tr = gh - 20 - 20;
        if (tr < 1) tr = 1;
        hy = ty0 + 10 + tr * (100 - v) / 100;
        slot(fx, ty0, gh - 20);
        if (mine && no_cards(a)) k_set_bounds(a, K_NODE_PARAM, 0, (KRect){fx - 10, gy, mx + 6 - (fx - 10), gh});   /* (the fader with its meter) */
        g_panel(&cv, fx - 8, hy - 10, 16, 20, 4, tv ? th.accent : th.text, 45);
        g_fill(&cv, fx - 4, hy, 9, 1, th.ink);
        /* the meter: the level a track just sounded (until there is audio, its steps), green, red at the top */
        lvl = track_meter(a, t);
        g_sunken(&cv, mx - 3, gy + 10, 7, gh - 20, 2, th.bg);   /* (the meter keeps the full length, 70 px; the fader's travel is the 60 centred in it) */
        if (lvl > 0) meter_fill(mx - 2, gy + 10, 5, gh - 20, lvl);   /* green, yellow from 70, red from 90 */
        {   /* the strip's label, in its footer: the track's number on a bar in its colour, the width of the strip */
            char nb[2] = {(char)('1' + t), 0};
            int fy = pic.y + pic.h - 3 - 13;
            strip_foot(x0 + 3, fy, sw - 6, 13, fy - 1, 15, t == a->track ? track_hue(t) : k_mix(th.bg, track_hue(t), 55), nb, t == a->track ? th.ink : th.text);   /* (the others dimmer) */
        }
    }
}

/* the inserts: the synth, the two slots, the output, as a signal chain; the slot the cards hold is in the accent */
/* text that fits maxw px: cut short with "..." where it is too long */
void text_fit(int x, int y, GFont f, const char *str, int maxw, uint32_t col)
{
    char b[48];
    size_t n = strlen(str);
    if (g_text_w(f, str) <= maxw || n >= sizeof b - 4) { g_text(&cv, x, y, f, str, col); return; }
    while (n > 1) {
        n--;
        memcpy(b, str, n);
        strcpy(b + n, "...");
        if (g_text_w(f, b) <= maxw) break;
    }
    g_text(&cv, x, y, f, b, col);
}
/* a name in at most two lines of width maxw, centred about cy: it breaks at the space when it does not fit on one line, and each line is cut
 * short with "..." if it is still too long */
static void text_two_lines(int x, int cy, GFont f, const char *str, int maxw, uint32_t col)
{
    char l1[24], l2[24];
    const char *sp = strchr(str, ' ');
    int h = g_text_h(f);
    if (g_text_w(f, str) <= maxw || !sp) { text_fit(x, cy - h / 2, f, str, maxw, col); return; }
    snprintf(l1, sizeof l1, "%.*s", (int)(sp - str), str);
    snprintf(l2, sizeof l2, "%s", sp + 1);
    text_fit(x, cy - h - 1, f, l1, maxw, col);
    text_fit(x, cy + 1, f, l2, maxw, col);
}
/* HOME: the four tracks one above the other, as Logic Pro's track view shows them, in one box (a well). No ruler; the rows divided by lines, a line between the headers and the regions. A header: the the
 * preset name, the track in hand lit. A region: a block in the
 * track's colour filling its row, the pattern in miniature in a lighter ink (a note's height for a synth, the lanes
 * hit for a drum track). The playhead a line through the rows. SELECT or a tap picks the track; the cards are
 * the four levels. */
static void pic_home_rows(KApp *a)
{
    int t, i, ruler = 0, rh = (pic.h - ruler - 1) / K_TRACKS, hw = 70, rx = pic.x + hw, rw = pic.w - hw, px = rx + 3, pw = rw - 3 - 10, cw = pw / K_STEPS;
    for (t = 0; t < K_TRACKS; t++) {
        int y = pic.y + ruler + t * rh, x = pic.x, cur = t == a->track, drum = k_is_drum(a, t), lo = 127, hi = 0, lvl;
        int mx = rx + rw - 8, ry = y + 2, rrh = rh - 3, my = ry + 4, mh = rrh - 8, top = ry + 5, area = rrh - 9;
        uint32_t hue = track_hue(t), ink = k_mix(hue, 0xFFFFFF, 78);
        k_set_bounds(a, K_NODE_TRACK, t, (KRect){x, y, pic.w, rh});
        {   /* the header: the preset's name (the track is known by its colour) */
            char pn[16];
            snprintf(pn, sizeof pn, "%s", k_track_missing(a, t) ? gone_word(a, t) : trk_preset(a, t));
            g_colored(&cv, x + 2, y + 3, 3, rh - 5, 1, cur ? hue : k_mix(hue, th.bg, 35));   /* the track's colour: a line at the left edge */
            {   const int iw = preset_icon(a, t, x + 9, y + rh / 2, 12, cur ? th.text : th.mid);
                text_two_lines(x + 9 + iw, y + rh / 2, GF_S, pn, hw - 12 - iw, cur ? th.text : th.mid); }
            /* (its preset's name, in two lines if need be) */
        }
        g_colored(&cv, rx + 1, ry, rw - 3, rrh, 4, cur ? hue : k_mix(th.bg, hue, 82));   /* the region */
        for (i = 0; i < K_STEPS; i++)
            if ((a->steps[t][a->pattern[t]] >> i) & 1) { int n = step_top(a, t, a->pattern[t], i); if (n < lo) lo = n; if (n > hi) hi = n; }
        for (i = 0; i < K_STEPS; i++) {   /* the pattern in miniature, under the name */
            int cx = px + i * cw, on = k_step_on(a, t, i), by, bh = 3;
            if (i % 4 == 0) g_fill(&cv, cx, top, 1, area, k_mix(hue, 0xFFFFFF, cur ? 22 : 14));
            if (!on) continue;
            if (drum) {   /* the lanes hit on the step: a bar as tall as their number */
                int l, hits = 0;
                for (l = 0; l < K_LANES; l++) hits += (a->lane_on[t][l] >> i) & 1;
                bh = 3 + hits * (area - 3) / K_LANES;
                by = top + area - bh;
            } else {   /* a synth: the note's height within the pattern's range */
                by = hi > lo ? top + (area - 3) * (hi - step_top(a, t, a->pattern[t], i)) / (hi - lo) : top + (area - 3) / 2;   /* (one pitch: in the middle, not along the top edge) */
            }
            g_rrect(&cv, cx + 1, by, cw - 1 < 2 ? 2 : cw - 1, bh, 1, ink);
        }
        lvl = track_meter(a, t);   /* the level it just sounded, at the right */
        g_sunken(&cv, mx, my, 4, mh, 2, k_mix(hue, 0x000000, 35));
        if (lvl > 0) meter_fill(mx, my, 4, mh, lvl);
    }
    if (a->playing && a->step >= 0) {   /* the playhead: a notch on the ruler and a line down through the rows */
        int lx = px + a->step * cw + cw / 2;
        g_fill(&cv, lx, pic.y, 1, pic.h, th.text);
    }
}
void pic_tracks(KApp *a)
{
    KRect keep = pic;
    well(pic);   /* the main view in a box */
    pic = (KRect){keep.x + 2, keep.y + 2, keep.w - 4, keep.h - 4};
    pic_home_rows(a);
    pic = keep;
}
/* HOME: the four tracks 2 x 2, a tile each. A tile: the preset's name, the
 * pattern in miniature (a note's height for a synth, the lanes hit for a drum track), the level it just sounded at the right
 * (no line at the left, no fill: the track in hand has an edge in its colour).
 * The track in hand lit; the playhead a line through the pattern. SELECT or a tap picks the track; the cards are the levels. */
void pic_home(KApp *a)
{
    /* the tracks as tiles: 2 x 2 for four (or fewer), four a row for more (eight: 4 x 2) */
    const int cols = K_TRACKS <= 4 ? 2 : 4, rows = (K_TRACKS + cols - 1) / cols;
    int t, i, gap = 4, tw0 = (pic.w - (cols - 1) * gap) / cols, th0 = (pic.h - (rows - 1) * gap) / rows;
    for (t = 0; t < K_TRACKS; t++) {
        const int col = t % cols, rw = t / cols;
        int x = pic.x + col * (tw0 + gap), y = pic.y + rw * (th0 + gap), cur = t == a->track, drum = k_is_drum(a, t);
        int tw = col == cols - 1 ? pic.w - col * (tw0 + gap) : tw0, tht = rw == rows - 1 ? pic.h - rw * (th0 + gap) : th0;   /* (the right / lower tile takes the spare pixels) */
        int lo = 127, hi = 0, lvl, ty = y + 6 + g_text_h(GF_S) + 4;
        int px = x + 7, pw = tw - 7 - 12, cw = pw / K_STEPS, top = ty + 2, area = y + tht - 6 - top;
        int mx = x + tw - 8, my = y + 6, mh = tht - 12;
        uint32_t hue = track_hue(t), ink = k_mix(hue, cur ? 0xFFFFFF : th.bg, cur ? 30 : 25);
        char pn[16];
        k_set_bounds(a, K_NODE_TRACK, t, (KRect){x, y, tw, tht});
        well((KRect){x, y, tw, tht});
        if (cur) { g_rrect(&cv, x, y, tw, tht, 8, th.text); g_sunken(&cv, x + 1, y + 1, tw - 2, tht - 2, 7, th.bg); }   /* the track in hand: a white edge (the app's mark; the tile's notes carry its colour) */
        snprintf(pn, sizeof pn, "%s", k_track_missing(a, t) ? gone_word(a, t) : trk_preset(a, t));
        {   const int iw = preset_icon(a, t, px, y + 5 + (g_text_h(GF_S) + 2) / 2, 12, cur ? th.text : th.mid);
            g_text_box(&cv, px + iw, y + 5, pw - iw, g_text_h(GF_S) + 2, GF_S, pn, cur ? th.text : th.mid, -1); }
        for (i = 0; i < K_STEPS; i++)
            if ((a->steps[t][a->pattern[t]] >> i) & 1) { int n = step_top(a, t, a->pattern[t], i); if (n < lo) lo = n; if (n > hi) hi = n; }
        if (area > 6 && cw > 0)
            for (i = 0; i < K_STEPS; i++) {   /* the pattern in miniature */
                int cx = px + i * cw, by, bh = 3;
                if (i % 4 == 0) g_fill(&cv, cx, top, 1, area, k_mix(th.bg, th.text, 12));
                if (a->playing && a->step == i) g_fill(&cv, cx, top, cw, area, k_mix(th.bg, th.text, 18));
                if (!k_step_on(a, t, i)) continue;
                if (drum) {
                    int l, hits = 0;
                    for (l = 0; l < K_LANES; l++) hits += (a->lane_on[t][l] >> i) & 1;
                    bh = 3 + hits * (area - 3) / K_LANES;
                    by = top + area - bh;
                } else
                    by = hi > lo ? top + (area - 3) * (hi - step_top(a, t, a->pattern[t], i)) / (hi - lo) : top + (area - 3) / 2;
                g_rrect(&cv, cx + 1, by, cw - 1 < 2 ? 2 : cw - 1, bh, 1, cur ? k_mix(hue, 0xFFFFFF, 20) : ink);
            }
        lvl = track_meter(a, t);   /* the level it just sounded */
        g_sunken(&cv, mx, my, 4, mh, 2, k_mix(hue, 0x000000, 35));
        if (lvl > 0) meter_fill(mx, my, 4, mh, lvl);
    }
}

/* the whole mixer is one box, the strips inside it */
/* a fader standing up, as a console's: its dB scale left of the rail, the meter right of it */
static void mix_strip_fader(int cx, int top, int bottom, int v, int lvl, int lit, uint32_t hue, int numbers)
{
    static const int DB[6] = {6, 0, -6, -12, -24, -48};
    const int h = bottom - top, rail = cx - 4, mx = cx + 12;
    int k, hy;
    const int tr = h - 16 > 1 ? h - 16 : 1;   /* the handle's travel: half a handle (8 px) in from each end of the rail */
    for (k = 0; k < 6; k++) {
        const int y = top + 8 + tr - tr * k_db_level_i(DB[k]) / 100;   /* (the scale where the handle's middle stands at that level) */
        char b[4];
        g_fill(&cv, rail - 7, y, 4, 1, DB[k] ? th.dim : th.mid);
        snprintf(b, sizeof b, "%d", DB[k] < 0 ? -DB[k] : DB[k]);
        if (numbers) g_text_box(&cv, rail - 31, y - 7, 22, 14, GF_S, b, DB[k] ? th.dim : th.mid, 1);   /* (a narrow strip: the ticks alone) */
    }
    slot(rail, top, h);
    hy = top + 8 + tr - tr * v / 100;
    g_panel(&cv, rail - 9, hy - 8, 19, 16, 4, lit ? th.accent : th.text, 45);
    g_fill(&cv, rail - 5, hy, 11, 1, lit ? th.ink : hue);
    g_sunken(&cv, mx - 3, top, 7, h, 2, th.bg);
    if (lvl > 0) meter_fill(mx - 2, top + 1, 5, h - 2, lvl);
}
/* the mixer on a larger screen (the Switch's 640 x 360): channel strips standing, of a fixed width, as Logic's: the four tracks,
 * the two buses (bus 1 a reverb, bus 2 a delay) and the master, centred. A strip from the top: its two inserts, its two sends,
 * its pan, the level in dB, the fader with its scale and meter, M and S, and at its foot its name on its colour (the track's
 * number and engine; a bus its effect). (The buses and sends are drawn as they will be: no audio takes them yet.) */
static void pic_mixer_wide(KApp *a)
{
    const int n = K_TRACKS + 3, gap = 6, sw = (pic.w - (n - 1) * gap) / n > 80 ? 80 : (pic.w - (n - 1) * gap) / n;
    const int total = n * sw + (n - 1) * gap, x00 = pic.x + (pic.w - total) / 2;
    int s, k;
    for (s = 0; s < n; s++) {
        const int x0 = x00 + s * (sw + gap), cx = x0 + sw / 2, trk = s < K_TRACKS, bus = s >= K_TRACKS && s < K_TRACKS + 2;
        const int t = trk ? s : a->track, mine = trk && t == a->track, foot = pic.y + pic.h - 18;
        int y = pic.y + 5, v, lvl = 0;
        char b[24];
        g_rrect_frame(&cv, x0, pic.y, sw, pic.h, 6, mine ? th.text : k_mix(th.bg, th.text, 12), k_mix(th.bg, 0x000000, 30));
        if (trk) { k_set_bounds(a, K_NODE_TRACK, t, (KRect){x0, foot - 2, sw, 19}); mix_strips[t] = (KRect){x0, pic.y, sw, pic.h}; }
        /* (the strip's target is its name at the foot: the pad steps strip to strip there, and up into the strip in hand's controls;
         * a touch anywhere on a strip picks its track: ui_tap) */
        for (k = 0; k < 2; k++) {   /* the inserts (a track's; a bus or the master: empty slots) */
            const int ft = trk ? a->params[t][K_PAGE_FX][k * 4] : 0;
            if (trk) mix_ins[t][k] = (KRect){(int16_t)(x0 + 5), (int16_t)y, (int16_t)(sw - 10), 15};   /* (a desktop: a click there, the effects to pick from) */
            insert_chip(x0 + 5, y, sw - 10, 15, ft, trk);
            y += 18;
        }
        y += 2;
        if (trk) for (k = 0; k < 6; k++) mix_ctl[t][k] = (KRect){0, 0, 0, 0};
        if (trk) mix_ctl_at = now;
        if (trk) for (k = 0; k < 2; k++) {   /* the sends */
            const int bx = x0 + sw / 4 + k * sw / 2, on = mine && (card_wanted(a, 2 + k) || hot_param(a, 2 + k));
            mix_ctl[t][2 + k] = (KRect){bx - 12, y - 2, 24, 34};
            send_knob(bx, y + 9, 9, mix_val(a, t, K_P_SEND1 + k), on, bx - 12, y + 20, 24, 12, k);
            if (mine) k_set_bounds(a, K_NODE_PARAM, 2 + k, (KRect){bx - 12, y - 2, 24, 34});   /* (the strip in hand: its controls are the targets) */
        }
        y += 34;
        if (trk) {   /* the pan */
            const int on = mine && (card_wanted(a, 1) || hot_param(a, 1));
            knob(cx, y + 12, 11, mix_val(a, t, K_P_PAN), 1, on ? th.accent : th.ok, th.line, th.text);
            mix_ctl[t][1] = (KRect){cx - 14, y - 1, 28, 27};
            if (mine) k_set_bounds(a, K_NODE_PARAM, 1, (KRect){cx - 14, y - 1, 28, 27});
        }
        y += 28;
        v = trk ? mix_val(a, t, K_P_LEVEL) : bus ? mix_val(a, 0, (s == K_TRACKS ? K_P_BUS1 : K_P_BUS2) + 3) : a->master;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        if (trk || bus) db_text(v, b, sizeof b);
        else snprintf(b, sizeof b, "%d", a->master);
        g_text_box(&cv, x0, y, sw, 14, GF_S, b, mine ? th.text : th.mid, 0);   /* the level, over its fader */
        y += 18;
        if (trk) lvl = track_meter(a, t);
        mix_strip_fader(sw >= 70 ? cx + 8 : cx, y + 4, foot - 26, v, lvl, mine && (card_wanted(a, 0) || hot_param(a, 0)), trk ? track_hue(t) : th.text, sw >= 70);
        if (trk) mix_ctl[t][0] = (KRect){x0 + 4, y + 4 + 8, sw - 8, foot - 26 - y - 4 - 16};   /* (its handle's travel) */
        if (mine) {
            k_set_bounds(a, K_NODE_PARAM, 0, (KRect){x0 + 4, y, sw - 8, foot - 26 - y + 4});   /* the fader: the level */
            mix_fader = (KRect){x0 + 4, y + 4 + 8, sw - 8, foot - 26 - y - 4 - 16};   /* (its handle's travel: a finger or the pointer takes it there) */
            mix_fader_at = now;
        }
        if (trk) {   /* M S */
            const int mu = (a->mute >> t) & 1, so = (a->solo >> t) & 1, by = foot - 20, bw = (sw - 14) / 2;
            mix_ctl[t][4] = (KRect){x0 + 5, by, bw, 16}; mix_ctl[t][5] = (KRect){x0 + 9 + bw, by, bw, 16};
            ms_key(x0 + 5, by, bw, 16, "M", mu, 0x3D8BFF);
            ms_key(x0 + 9 + bw, by, bw, 16, "S", so, 0xFFD60A);
        }
        {   /* the name, at the foot: on the track's colour (the master and the buses: grey) */
            const uint32_t bar = trk ? (mine ? track_hue(t) : k_mix(th.bg, track_hue(t), 60)) : k_mix(th.bg, th.text, 20);
            if (trk) snprintf(b, sizeof b, sw >= 70 ? "%d %s" : "%d", t + 1, trk_engine(a, t));   /* (a narrow strip: the number) */
            else snprintf(b, sizeof b, "%s", bus ? (sw >= 70 ? (s == K_TRACKS ? "B1 REV" : "B2 DLY") : (s == K_TRACKS ? "B1" : "B2")) : sw >= 70 ? "MASTER" : "MST");
            strip_foot(x0 + 3, foot, sw - 6, 15, foot - 1, 16, bar, b, trk && mine ? th.ink : th.text);
        }
    }
}
/* the FM-1's mixer on its side: a row a track, its number at the left edge; the level a fader lying along the row,
 * the track's meter running inside its slot; the pan; the two buses one over the other, each named before its knob */
static void pic_mixer_rows(KApp *a)
{
    static const int EDGE[3] = {0, 70, 90};
    const uint32_t mcol[3] = {th.ok, 0xFFD60A, th.rec};
    int t, n = K_TRACKS, rh = pic.h / n;
    for (t = 0; t < n; t++) {
        const int y = pic.y + t * rh, cy = y + rh / 2, mine = t == a->track, v = mix_val(a, t, K_P_LEVEL), pan = mix_val(a, t, K_P_PAN);
        const int tv = mine && card_wanted(a, 0), tp = mine && card_wanted(a, 1), lvl = track_meter(a, t) > 100 ? 100 : track_meter(a, t);
        /* even gaps: | 4 | number 16 | G | name over fader | G | pan 18 | G | B1 B2 33 | 4 | */
        const int G = 10, bus_x = pic.x + pic.w - 4 - 33, pan_cx = bus_x - G - 9;
        const int fx0 = pic.x + 4 + 16 + G, fx1 = pan_cx - 9 - G, sw = fx1 - fx0, hx = fx0 + 5 + (sw - 10) * v / 100, fy = y + rh - 15;   /* (the fader low, the preset's name over it) */
        const uint32_t hue = track_hue(t);
        char nb[2] = {(char)('1' + t), 0};
        int k;
        k_set_bounds(a, K_NODE_TRACK, t, (KRect){pic.x, y, pic.w, rh});
        if (t && t != a->track && t - 1 != a->track) g_fill(&cv, pic.x + 4, y, pic.w - 8, 1, th.line);
        if (mine) {   /* the track in hand: a white outline, corners cut */
            g_fill(&cv, pic.x + 2, y, pic.w - 4, 1, th.text); g_fill(&cv, pic.x + 2, y + rh - 1, pic.w - 4, 1, th.text);
            g_fill(&cv, pic.x, y + 2, 1, rh - 4, th.text); g_fill(&cv, pic.x + pic.w - 1, y + 2, 1, rh - 4, th.text);
            g_fill(&cv, pic.x + 1, y + 1, 1, 1, th.text); g_fill(&cv, pic.x + pic.w - 2, y + 1, 1, 1, th.text);
            g_fill(&cv, pic.x + 1, y + rh - 2, 1, 1, th.text); g_fill(&cv, pic.x + pic.w - 2, y + rh - 2, 1, 1, th.text);
        }
        g_rrect(&cv, pic.x + 4, cy - 8, 16, 16, 4, mine ? hue : k_mix(th.bg, hue, 55));   /* the number in its colour, at the left edge, on a square */
        g_text_box(&cv, pic.x + 4, cy - 8, 16, 16, GF_S, nb, mine ? th.ink : th.text, 0);
        text_fit(fx0, y + 5, GF_S, k_track_missing(a, t) ? gone_word(a, t) : trk_preset(a, t), sw, mine ? th.text : th.mid);   /* its preset, over the fader */
        g_rrect(&cv, fx0, fy - 3, sw, 6, 3, k_mix(th.bg, th.text, 12));   /* the slot, the meter inside it: green, yellow from 70, red from 90 */
        for (k = 0; k < 3; k++) {
            const int hi = k == 2 ? lvl : (lvl < EDGE[k + 1] ? lvl : EDGE[k + 1]), lo = EDGE[k];
            if (hi > lo) g_fill(&cv, fx0 + 2 + (sw - 4) * lo / 100, fy - 1, (sw - 4) * (hi - lo) / 100, 2, mcol[k]);
        }
        g_rrect(&cv, hx - 5, fy - 7, 10, 14, 3, tv ? th.accent : th.text);   /* the handle */
        g_fill(&cv, hx, fy - 4, 1, 8, th.ink);
        knob(pan_cx, cy - 4, 9, pan, 1, tp ? th.accent : th.ok, th.line, th.text);   /* the pan, */
        g_text_box(&cv, pan_cx - 14, cy + 6, 28, 12, GF_S, "Pan", tp ? th.accent : th.mid, 0);   /* named under it, as B1 B2 are (a word, not an icon) */
        for (k = 0; k < 2; k++) {   /* B1 over B2: the name, then its knob */
            const int on = mine && card_wanted(a, 2 + k), by = cy - 8 + k * 16, bx = bus_x + 33 - 6;
            g_text_box(&cv, bus_x, by - 6, 18, 12, GF_S, k ? "B2" : "B1", on ? th.accent : th.mid, -1);
            knob(bx, by, 6, mix_val(a, t, K_P_SEND1 + k), 0, on ? th.accent : th.ok, th.line, th.text);
        }
    }
}
void pic_mixer(KApp *a)
{
    if (cv.w == 240 && cv.h == 240) {   /* the FM-1: on its side */
        KRect keep = pic;
        well(pic);
        pic = (KRect){keep.x + 1, keep.y + 2, keep.w - 2, keep.h - 4};   /* (a thin inset: the numbers to the left edge) */
        pic_mixer_rows(a);
        pic = keep;
        return;
    }
    KRect keep = pic;
    if (cv.w >= 600) { pic_mixer_wide(a); return; }   /* (the Switch: strips of their own width) */
    well(pic);   /* a dark recess: the strips stand out of it */
    pic = (KRect){keep.x + 3, keep.y + 2, keep.w - 6, keep.h - 4};
    pic_mixer_strips(a);
    pic = keep;
}

/* The FX page, in parts (the restructure's pilot): what a slot holds (fx_slot_controls),
 * the parts it is drawn with (a selector for its effect, a stack of label, knob and value for each control), and two ways of laying
 * them out: rows of a small screen (the FM-1's, a handheld's: the knobs' row in hand, hand-laid) and a rack on a big one. The values
 * are the values' table's (ui_item_value); the effect changes only through k_fx_type_step / k_fx_type_set. */

/* a slot's controls in the order a rack shows them: its effect's parameters as the page numbers them, then MIX. The count */
int fx_slot_controls(const KApp *a, int slot, int *items, int max)
{
    const int n = k_page_count(a, K_PAGE_FX);
    int pass, item, k = 0;
    for (pass = 0; pass < 2; pass++)
        for (item = 0; item < n && k < max; item++) {
            int s2 = -1, param = -3;
            k_fx_item(a, item, &s2, &param);
            if (s2 == slot && (pass == 0 ? param >= 0 : param == -2)) items[k++] = item;
        }
    return k;
}
/* the item that holds a slot's effect on this screen, or -1 (ALGORITHM picks it there: no item) */
int fx_type_item(const KApp *a, int slot)
{
    const int n = k_page_count(a, K_PAGE_FX);
    int item;
    for (item = 0; item < n; item++) {
        int s2 = -1, param = -3;
        k_fx_item(a, item, &s2, &param);
        if (s2 == slot && param == -1) return item;
    }
    return -1;
}

/* part: a rotary, its arc in the effect's colour (dimmer on a slot not in hand) */
static void fx_rotary(int cx, int cy, int r, int v, uint32_t hue, int lit)
{
    uint32_t arc = lit ? hue : k_mix(th.bg, hue, 55);
    uint32_t ring = k_mix(th.bg, th.text, lit ? 32 : 20);
    g_knob_face(&cv, cx, cy, r - 3, k_mix(th.bg, th.text, 18));
    g_knob(&cv, cx, cy, r, v, 0, arc, ring, arc);
}
void knob_text(const KApp *a, KPage page, int item, KnobText *k)
{
    char value[16];
    const char *unit;
    k->label = ui_item_name(a, page, item);
    k->v = k_param_get(a, page, item);
    k->v = k->v < 0 ? 0 : k->v > 100 ? 100 : k->v;
    ui_item_value(a, page, item, value, sizeof value, &unit);
    snprintf(k->shown, sizeof k->shown, "%s%s", value, unit);
    {   KParamProps pr;   /* (a switch: drawn as one; the value's table says which, an effect's too) */
        k->sw = k_param_props(a, k_param_ref(a, page, item), &pr) && pr.d.fmt == KF_ONOFF ? k_switch_on(&pr.d, k_param_get(a, page, item)) : -1; }
    {   /* a value that names a wave: drawn as one (, small, white, no frame) */
        static const char *const W[][2] = {{"Sin", "Sine"}, {"Tri", "Triangle"}, {"Saw", "Saw"}, {"Sqr", "Square"}, {"S&H", "S&H"}};
        int i;
        k->wave = -1;
        for (i = 0; i < 5; i++) if (!strcmp(value, W[i][0]) || !strcmp(value, W[i][1])) k->wave = i;
    }
}
/* a wave in w x h, a white line (no frame): its kind as KnobText.wave */
static void mini_wave(int x, int y, int w, int h, int kind, uint32_t col)
{
    static const int8_t SIN[13] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50, 0};   /* (a sine in twelve, of 100) */
    const int mid = y + h / 2, a = h / 2;
    int i;
    if (kind == 0) {
        for (i = 0; i < 12; i++) g_line(&cv, x + i * w / 12, mid - SIN[i] * a / 100, x + (i + 1) * w / 12, mid - SIN[i + 1] * a / 100, 1, col);
    } else if (kind == 1) {
        g_line(&cv, x, mid, x + w / 4, y, 1, col); g_line(&cv, x + w / 4, y, x + 3 * w / 4, y + h, 1, col); g_line(&cv, x + 3 * w / 4, y + h, x + w, mid, 1, col);
    } else if (kind == 2) {
        g_line(&cv, x, y + h, x + w / 2, y, 1, col); g_fill(&cv, x + w / 2, y, 1, h + 1, col); g_line(&cv, x + w / 2, y + h, x + w, y, 1, col);
    } else if (kind == 3) {
        g_fill(&cv, x, y, w / 2, 1, col); g_fill(&cv, x + w / 2, y, 1, h + 1, col);   /* high, down, low, up: a cycle */
        g_fill(&cv, x + w / 2, y + h, w - w / 2, 1, col); g_fill(&cv, x + w - 1, y, 1, h + 1, col);
    } else {   /* S&H: steps */
        static const int8_t ST[6] = {2, 0, 3, 1, 4, 2};
        for (i = 0; i < 6; i++) {
            const int sy = y + ST[i] * h / 4, sx = x + i * w / 6, ex = x + (i + 1) * w / 6;
            g_fill(&cv, sx, sy, ex - sx, 1, col);
            if (i < 5) { const int ny = y + ST[i + 1] * h / 4; g_fill(&cv, ex, sy < ny ? sy : ny, 1, (sy < ny ? ny - sy : sy - ny) + 1, col); }
        }
    }
}
/* x, w: its column; label at ly (lh tall), the knob at cy (r), the value at vy (lh tall) */
void knob_stack(const KnobText *k, int x, int w, int lh, int ly, int cy, int r, int vy, uint32_t hue, int lit, uint32_t lcol, uint32_t vcol)
{
    g_text_box(&cv, x, ly, w, lh, GF_S, k->label, lcol, 0);
    if (k->sw >= 0) {   /* a two-way switch: standing up, iOS's (a pill, its round handle up when on) */
        const int cx = x + w / 2, h = 2 * r + 2, ty = cy - h / 2, sw = 14;
        (void)hue;   /* (the handle white, its groove a light grey when on: not white under white) */
        g_rrect(&cv, cx - sw / 2, ty, sw, h, sw / 2, k->sw ? k_mix(th.bg, th.text, lit ? 55 : 35) : k_mix(th.bg, th.text, 16));
        g_rrect_a(&cv, cx - sw / 2 + 2, (k->sw ? ty + 2 : ty + h - sw + 2) + 1, sw - 4, sw - 4, (sw - 4) / 2, 0x000000, 110);
        g_rrect(&cv, cx - sw / 2 + 2, k->sw ? ty + 2 : ty + h - sw + 2, sw - 4, sw - 4, (sw - 4) / 2, lit ? th.text : th.mid);
    } else fx_rotary(x + w / 2, cy, r, k->v, hue, lit);
    if (k->wave >= 0) mini_wave(x + w / 2 - 9, vy + (lh - 8) / 2, 18, 8, k->wave, lit ? th.text : th.mid);   /* (its wave, not its name) */
    else g_text_box(&cv, x, vy, w, lh, GF_S, k->shown, vcol, 0);
}
/* BUS 1 and BUS 2 (): what the bus is (Bus 1 Reverb, Bus 2 Delay), a picture of what it
 * does (the reverb's tail as SIZE and DCY shape it, the delay's echoes as TIME and FB space and fade them, TONE their brightness), its
 * four values as knobs (their targets: no cards, as FX), and the tracks sending to it, each its send in its colour */
void pic_bus(KApp *a)
{
    const int b2 = a->page == K_PAGE_BUS2, n = 4, fh = g_text_h(GF_S);
    const uint32_t hue = fx_accent(b2 ? 3 : 4);
    const int v0 = k_param_get(a, a->page, 0), v1 = k_param_get(a, a->page, 1), v2 = k_param_get(a, a->page, 2);
    const KRect p = pic;
    const int sh = p.h * 2 / 5 < 60 ? 60 : p.h * 2 / 5, sy = p.y + fh + 8;
    int i, x;
    panel_box(p);
    x = g_text(&cv, p.x + 8, p.y + 4, GF_S, b2 ? "Bus 2" : "Bus 1", hue);
    g_text(&cv, x + 8, p.y + 4, GF_S, b2 ? "Delay" : "Reverb", th.text);
    {   /* the picture: on a dark screen, time left to right */
        const int gx = p.x + 8, gw = p.w - 16, gy = sy, gh = sh, base = gy + gh - 6;
        const uint32_t line = k_mix(k_mix(th.bg, hue, 55), th.text, v2 * 60 / 100);   /* TONE: duller .. brighter */
        g_rrect(&cv, gx, gy, gw, gh, 4, k_mix(th.bg, 0x000000, 40));
        g_fill(&cv, gx + 4, base, gw - 8, 1, k_mix(th.bg, th.text, 14));
        if (!b2) {   /* a reverb: a dense tail, its start spread by SIZE, its length by DCY */
            const int len = (gw - 12) * (15 + v1 * 85 / 100) / 100, pre = 2 + v0 * 10 / 100;
            uint32_t r = 0x9E3779B9u;
            for (i = 0; i < len; i += 2) {
                const int left = len - i, amp = (gh - 14) * left / len * left / len;
                r ^= r << 13; r ^= r >> 17; r ^= r << 5;
                {   const int h = (int)((uint32_t)amp * (60u + (r >> 25)) / 188u) + (i < pre * 2 ? 0 : 1);
                    if (i >= pre) g_fill(&cv, gx + 6 + i, base - h, 1, h, line); }
            }
        } else {   /* a delay: echoes TIME apart, each FB of the one before */
            const int step = 8 + (gw - 30) * v0 / 100 / 3;
            int k, h = gh - 14;
            for (k = 0; k < 16 && h >= 2; k++) {
                const int ex = gx + 8 + k * step;
                if (ex > gx + gw - 6) break;
                g_fill(&cv, ex, base - h, 3, h, k ? line : th.text);
                h = h * (10 + v1 * 80 / 100) / 100;
            }
        }
    }
    {   /* the four values as knobs, their own targets */
        const int ky = sy + sh + 4, kw = (p.w - 16) / n, r = 11, cy = ky + fh + 2 + r;
        for (i = 0; i < n; i++) {
            KnobText kt;
            const int kx = p.x + 8 + i * kw, hot = hot_param(a, i);
            knob_text(a, a->page, i, &kt);
            knob_stack(&kt, kx, kw, fh + 1, ky, cy, r, cy + r + 2, hue, 1, hot ? th.text : th.mid, hot ? th.text : hue);
            k_set_bounds(a, K_NODE_PARAM, i, (KRect){kx, ky - 2, kw, cy + r + 4 + fh - ky});
        }
        {   /* the tracks that send here: a short bar each, as much as its send */
            const int ry = cy + r + fh + 8, bw = (p.w - 16 - 40) / K_TRACKS;
            if (ry + fh + 4 <= p.y + p.h) {
                g_text(&cv, p.x + 8, ry, GF_S, "Sends", th.dim);
                for (i = 0; i < K_TRACKS; i++) {
                    const int s = a->params[i][K_PAGE_MIXER][b2 ? 3 : 2], bx = p.x + 48 + i * bw, by = ry + fh / 2 - 2;
                    g_rrect(&cv, bx, by, bw - 4, 4, 2, k_mix(th.bg, th.text, 12));
                    if (s > 0) g_rrect(&cv, bx, by, (bw - 4) * (s > 100 ? 100 : s) / 100, 4, 2, track_hue(i));
                }
            }
        }
    }
}
