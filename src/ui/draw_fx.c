/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FX (the inserts' effects), LFO, ARP and SCALE as pictures. */
#include "draw_int.h"
/* part: a choice by name with a step either way, as text, "<  NAME  >" (a small screen): x, y where its text starts; arrows[] its two
 * targets (a pointer or a finger) */
static void select_text(int x, int y, GFont f, const char *name, uint32_t name_col, uint32_t caret_col, KRect arrows[2])
{
    const int nh = g_text_h(f), aw = g_text_w(f, "<"), nx = x + aw + 8, ne = g_text_w(f, name);
    g_text(&cv, x, y, f, "<", caret_col);
    g_text(&cv, nx, y, f, name, name_col);
    g_text(&cv, nx + ne + 8, y, f, ">", caret_col);
    arrows[0] = (KRect){x - 6, y - 6, aw + 12, nh + 12};
    arrows[1] = (KRect){nx + ne + 2, y - 6, aw + 12, nh + 12};
}
/* part: the same as raised keys (a big screen, as SYNTH's engine): < and > boxes either side of the name's box, nw wide. The name's box */
static KRect select_raised(int x, int y, int nw, int h, const char *name, uint32_t name_col, KRect arrows[2])
{
    const int cb = 24;
    const uint32_t face = k_mix(th.surf, 0xFFFFFF, 10);
    g_raised(&cv, &th, x, y, cb, h, 4, face, 3);
    g_raised(&cv, &th, x + cb + 2 + nw + 2, y, cb, h, 4, face, 3);
    g_icon(&cv, x + (cb - 14) / 2, y + (h - 14) / 2, 14, GI_CARET_L, th.text);
    g_icon(&cv, x + cb + 2 + nw + 2 + (cb - 14) / 2, y + (h - 14) / 2, 14, GI_CARET_R, th.text);
    g_raised(&cv, &th, x + cb + 2, y, nw, h, 4, k_mix(th.surf, 0xFFFFFF, 6), 3);
    g_text_box(&cv, x + cb + 2, y, nw, h, GF_M, name, name_col, 0);
    arrows[0] = (KRect){x, y, cb, h};
    arrows[1] = (KRect){x + cb + 2 + nw + 2, y, cb, h};
    return (KRect){x + cb + 2, y, nw, h};
}

/* layout: a rack (a big screen with no cards: the desktop's, the Switch's): the two slots side by side as two units, each its
 * effect on raised keys, its icon in a lit window, then every control it has, MIX last, four a row, each its own target */
static void pic_fx_wide(KApp *a)
{
    const int gap = 6, w = (pic.w - gap) / 2;
    int slot;
    for (slot = 0; slot < K_FX_SLOTS; slot++) {
        const int x0 = pic.x + slot * (w + gap), y0 = pic.y, type = fx_type(a, slot * 4), bh = 24;
        const uint32_t hue = fx_accent(type);
        const int nw = w - 2 * 24 - 4 - 70, bx = x0 + 60, by = y0 + 8;
        int items[K_FX_ITEMS], k, idx, ky, cw, rows;
        char title[4] = {'F', 'X', (char)('1' + slot), 0};
        g_panel(&cv, x0, y0, w, pic.h, 8, k_mix(th.surf, 0x000000, 15), 40);
        g_text(&cv, x0 + 12, by + (bh - g_text_h(GF_M)) / 2, GF_M, title, type ? hue : th.mid);
        {   /* the effect: < NAME > (the name its own target: a drag, the wheel; the carets a step) */
            const KRect nb = select_raised(bx, by, nw, bh, fx_name(type), type ? th.text : th.mid, fx_arrows[slot]);
            const int ti = fx_type_item(a, slot);
            if (ti >= 0) k_set_bounds(a, K_NODE_PARAM, ti, nb);
        }
        {   /* the window: the effect's icon, lit in its colour (dark when the slot is empty) */
            const KRect wv = {x0 + 12, by + bh + 10, w - 24, 54};
            const int ic = fx_icon(type);
            g_rrect_frame(&cv, wv.x, wv.y, wv.w, wv.h, 5, k_mix(th.bg, th.text, 10), k_mix(th.bg, 0x000000, 40));
            if (type && ic >= 0) {
                g_rrect_a(&cv, wv.x + wv.w / 2 - 40, wv.y + 6, 80, wv.h - 12, 20, hue, 28);   /* (a glow behind it) */
                g_icon(&cv, wv.x + (wv.w - 36) / 2, wv.y + (wv.h - 36) / 2, 36, (GIcon)ic, hue);
            } else g_text_box(&cv, wv.x, wv.y, wv.w, wv.h, GF_S, type ? fx_name(type) : "Empty", th.dim, 0);
        }
        if (!type) continue;
        ky = by + bh + 10 + 54 + 12;
        {   const KLayout *L = k_effect_layout(type, k_screen_kind(cv.w, cv.h));
            if (L) {   /* the effect's own table in the unit's controls' room (every control its target) */
                plug_fx = slot; plug_fx_lit = 1;
                plug_layout(a, L, (KRect){x0 + 12, ky, w - 24, pic.y + pic.h - 8 - ky}, 0);
                plug_fx = -1;
                continue;
            }
        }
        k = fx_slot_controls(a, slot, items, K_FX_ITEMS);
        rows = (k + 3) / 4;
        cw = (w - 24) / (k < 4 ? k : 4);   /* (fewer than four: spread across) */
        {   const int room = pic.y + pic.h - 8 - ky, rh0 = rows ? room / rows : 0, rh = rh0 > 96 ? 96 : rh0, fh = g_text_h(GF_S);
            const int r = rh / 2 - 14 > 26 ? 26 : rh / 2 - 14 < 10 ? 10 : rh / 2 - 14;
            ky += (room - rows * rh) / 2;   /* (the knobs in the middle of what is left) */
            for (idx = 0; idx < k; idx++) {
                const int item = items[idx], kx = x0 + 12 + (idx % (k < 4 ? k : 4)) * cw;
                const int cy = ky + (idx / 4) * rh + (rh - 2 * r - 2 * fh - 8) / 2 + fh + 4 + r;
                const int hot = hot_param(a, item) || touched_recent(a, item);
                KnobText kt;
                knob_text(a, K_PAGE_FX, item, &kt);
                knob_stack(&kt, kx, cw, fh, cy - r - fh - 4, cy, r, cy + r + 3, hue, 1, hot ? th.text : th.mid, hot ? hue : th.text);
                k_set_bounds(a, K_NODE_PARAM, item, (KRect){kx + 4, cy - r - fh - 4, cw - 8, 2 * r + 2 * fh + 8});
            }
        }
    }
}
/* layout: rows (a small screen, hand-laid: the FM-1's, a handheld's): a box a slot, the one in hand lit, its effect's name at the
 * left and the knobs' row in hand at the right (its first row, or the row of extras the window is on) */
void pic_fx(KApp *a)
{
    if (no_cards(a)) { pic_fx_wide(a); return; }
    int selected = 0, h = (pic.h - 2) / 2, slot;
    k_fx_item(a, a->win, &selected, 0);
    for (slot = 0; slot < K_FX_SLOTS; slot++) {
        int top = pic.y + slot * (h + 2), on = slot == selected;
        int type = fx_type(a, slot * 4), gx = pic.x + 10, gy = top + 18;
        const int algo = (a->lay & K_LAY_FX_ALGO) != 0, ext = on && a->win >= 8;   /* (ALGORITHM picks the type: four a row) */
        int cols = ext || algo ? 4 : 3, tight = cols == 4 && pic.w < 280;   /* (a narrow screen: the name small, the knobs apart) */
        int xbase = pic.x + (tight ? 66 : 88), space = pic.w - (tight ? 70 : 95), radius = (h - 30) / 3, fh = g_text_h(GF_S), ly, cy;
        uint32_t hue = fx_accent(type), ink = on ? th.text : th.dim;
        char title[4] = {'F', 'X', (char)('1' + slot), 0};
        const GFont nf = tight ? GF_S : GF_M;
        if (radius < 8) radius = 8;
        if (tight) gx = pic.x + 6;
        if (radius > (tight ? 13 : 15)) radius = tight ? 13 : 15;
        if (space < 0) space = 0;
        ly = top + (h - (2 * fh + 2 * radius + 4)) / 2;   /* label, knob and value kept together, the stack in the middle */
        if (ly < top + 2) ly = top + 2;
        cy = ly + fh + 2 + radius;
        if (on) panel_box((KRect){pic.x, top, pic.w, h});
        else ghost_box(pic.x, top, pic.w, h);
        g_text(&cv, gx, top + 2, GF_S, title, on ? hue : th.dim);
        fx_arrows[slot][0] = fx_arrows[slot][1] = (KRect){0, 0, 0, 0};
        if (!algo && (prof->input & K_IN_TOUCH))   /* a pointer, a finger: < and > beside the name step the effect (no ALGORITHM here) */
            select_text(gx, gy + 6, nf, fx_name(type), type ? ink : th.dim, on ? th.mid : th.dim, fx_arrows[slot]);
        else g_text(&cv, gx, gy + 6, nf, fx_name(type), type ? ink : th.dim);   /* (no icon: the colour names the effect, and the
                                                                         * longest name, TREMOLO, fits beside the knobs) */
        if (on) { const int ti = fx_type_item(a, slot); if (ti >= 0) k_set_bounds(a, K_NODE_PARAM, ti, (KRect){gx - 4, gy, xbase - gx, g_text_h(nf) + 12}); }   /* (the effect: its name) */
        if (!type) {   /* an empty insert in hand: how to put one in (, a balloon in the slot) */
            if (on) {   /* the key that does it on its cushion (as the tools' OCT+), then what it does: the FM-1's ALGORITHM, a keyboard's
                                                 * , and ., a pad's A held + < >; a pointer has the < > beside the name already */
                const char *k1 = algo ? "ALGORITHM" : (prof->input & K_IN_KEYBOARD) ? "," : (prof->input & K_IN_PAD) ? "A" : 0;
                const char *k2 = algo ? 0 : (prof->input & K_IN_KEYBOARD) ? "." : (prof->input & K_IN_PAD) ? "< >" : 0;
                if (k1) {
                    const int w = keycap_w(k1) + (k2 ? 4 + keycap_w(k2) : 0) + 6 + g_text_w(GF_S, "Insert");
                    int kx = pic.x + pic.w - w - 10;
                    const int ky = top + (h - 16) / 2 + 6;   /* (right of the name) */
                    kx += keycap(kx, ky, k1, 1) + 4;
                    if (k2) kx += keycap(kx, ky, k2, 1) + 4;
                    g_text(&cv, kx + 2, ky + (16 - fh) / 2, GF_S, "Insert", th.accent);
                }
            }
            continue;
        }
        {   const KLayout *L = k_effect_layout(type, k_screen_kind(cv.w, cv.h));
            if (L) {   /* the effect's own table beside its name (lit as the knobs reach it); its name the effect's target */
                const int ti = on ? fx_type_item(a, slot) : -1;
                if (ti >= 0) k_set_bounds(a, K_NODE_PARAM, ti, (KRect){gx - 4, gy, xbase - gx, g_text_h(nf) + 12});
                plug_fx = slot; plug_fx_lit = on;
                plug_layout(a, L, (KRect){xbase, top + 2, pic.x + pic.w - 4 - xbase, h - 4}, 0);
                plug_fx = -1;
                continue;
            }
        }
        for (int i = 0; i < cols; i++) {
            const int item = ext ? a->win + i : slot * 4 + (algo ? 0 : 1) + i;
            int x0, x1, hot;
            KnobText kt;
            {
                int s = -1, param = -3;
                k_fx_item(a, item, &s, &param);
                if (s != slot || param == -3 || param == -1) continue;
            }
            if (!ui_item_name(a, K_PAGE_FX, item)[0]) continue;
            knob_text(a, K_PAGE_FX, item, &kt);
            x0 = xbase + space * i / cols;
            x1 = xbase + space * (i + 1) / cols;
            hot = on && hot_param(a, item);
            knob_stack(&kt, x0, x1 - x0, fh + 1, ly, cy, radius, cy + radius + 2, hue, on,
                       hot ? th.text : on ? th.mid : th.dim, hot ? th.text : on ? hue : th.dim);
            if (on) k_set_bounds(a, K_NODE_PARAM, item, (KRect){x0, ly - 2, x1 - x0, cy + radius + 4 + fh - ly});   /* (no cards: the knob is its target) */
        }
    }
}

/* the LFO: its wave over the cycles RATE gives, shifted by PHS, faded in over FADE (as Felucca 2.0's LFO page draws it) */
void pic_lfo(KApp *a)
{
    const KVal *P = a->params[a->track][K_PAGE_LFO]; int i, N = 160, gx = pic.x + 4, gy = pic.y + 6, gw = pic.w - 8, gh = pic.h - 12;
    {   /* t (1 + 7 P0 / 127) + P2 / 127 cycles, as a whole number of 20320ths; faded in over (.9 P3 / 127 + .02) */
        int p16[2 * 161];
        const int sc = 127 + 7 * P[0], fd = 5 * (45 * P[3] + 127);
        g_fill(&cv, gx, gy + gh / 2, gw, 1, th.line);
        for (i = 0; i <= N; i++) {
            const int num = i * sc + 160 * P[2], cyc = num / 20320, f = (int)((uint32_t)(num % 20320) * 211367u >> 16);   /* (the cycle; its phase, Q16) */
            const int amp = P[3] > 2 ? (int)((uint32_t)(6350 * i) * 2048u / (uint32_t)fd) : 65536;
            int w = lfo_wave16(P[1], cyc, f);
            if (amp < 65536) w = (w >> 1) * (amp >> 1) >> 14;
            p16[2 * i] = gx * 16 + i * (gw - 1) * 16 / N;
            p16[2 * i + 1] = (((16 * gy + 8 * gh) << 16) - w * (gh / 2) * 136 / 10) >> 16;   /* (.85 of half the height, 16ths: 13.6) */
        }
        curve_glow16(p16, N + 1, k_mix(th.theme, 0xFFFFFF, 30));
    }
}
/* ARP. While it plays (a note in the last two seconds): what it really plays, its last eight notes as bars by their pitch, the
 * newest at the right, lit as it sounds and fading, its name over it. Quiet: the pattern its MODE makes of a triad (the same
 * selection rules as the audio's), eight notes with GATE shown by the bar's width. */
static int arp_live_view(KApp *a)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int t = a->track, n = a->arp_n[t] < 8 ? a->arp_n[t] : 8, gx = pic.x + 6, gy = pic.y + 8 + 16, gw = pic.w - 12, gh = pic.h - 16 - 16, bw = gw / 8;
    const uint32_t age = now - a->arp_ms[t], hue = th.accent;
    int i, lo = 127, hi = 0;
    char b[8];
    if (!a->arp_n[t] || age >= 2000u || k_is_drum(a, t)) return 0;
    for (i = 0; i < n; i++) { const int p = a->arp_hist[t][(a->arp_n[t] - 1 - i) & 7]; if (p < lo) lo = p; if (p > hi) hi = p; }
    if (hi - lo < 12) { lo = (lo + hi) / 2 - 6; hi = lo + 12; }   /* (at least an octave tall: a single note is not a wall) */
    for (i = 0; i < 8; i++) {
        const int bx = gx + i * bw, back = 7 - i;   /* (the newest at the right) */
        g_sunken(&cv, bx + 1, gy, bw - 2, gh, 2, th.bg);
        if (back < n) {
            const int p = a->arp_hist[t][(a->arp_n[t] - 1 - back) & 7], bh = 4 + (gh - 4) * (p - lo) / (hi - lo);
            const int glow = back == 0 ? (age < 300u ? (int)(300u - age) * 100 / 300 : 0) : 0;
            const uint32_t col = back == 0 ? k_mix(k_mix(th.text, hue, 60), 0xFFFFFF, glow * 40 / 100) : k_mix(th.bg, th.text, 70 - back * 6);
            g_colored(&cv, bx + 1, gy + gh - bh, bw - 2, bh, 2, col);
        }
    }
    {   const int p = a->arp_hist[t][(a->arp_n[t] - 1) & 7];
        snprintf(b, sizeof b, "%s%d", NN[p % 12], p / 12 - 1);
        g_text_box(&cv, pic.x, pic.y + 4, pic.w - 8, 16, GF_M, b, age < 300u ? k_mix(th.text, hue, 50) : th.mid, 1); }
    return 1;
}
void pic_arp(KApp *a)
{
    if (arp_live_view(a)) return;
    const int mode = table_val(a, K_PAGE_ARP, 0), oct = table_val(a, K_PAGE_ARP, 2);   /* (their ranges: the values' table's) */
    const int span = 3 * oct, n = 8, gx = pic.x + 6, gy = pic.y + 8;
    const int gw = pic.w - 12, gh = pic.h - 16, bw = gw / n;
    const int gate = (bw - 2) * table_val(a, K_PAGE_ARP, 3) / 127;
    uint32_t rng = 0x9E3779B9u;
    unsigned walk = 0;
    int i;
    for (i = 0; i < n; i++) {
        const int bx = gx + i * bw;
        unsigned extra = 0;
        int k = k_arp_pick((unsigned)mode, (uint32_t)i, (unsigned)span, &rng, &walk, &extra);
        int width = gate < 3 ? 3 : gate;
        g_sunken(&cv, bx + 1, gy, bw - 2, gh, 2, th.bg);
        if (mode == 14) {
            int p;
            /* CHORD draws simultaneous notes as horizontal stripes. */
            for (p = 0; p < span; p++) {
                int y = gy + gh - 3 - p * (gh - 7) / span;
                g_colored(&cv, bx + 1, y, width, 2, 1, th.text);
            }
        } else if (k >= 0) {
            int bh = gh * (k + 1 + (int)extra * 3) / (span + 3);
            if (bh < 3) bh = 3;
            g_colored(&cv, bx + 1, gy + gh - bh, width, bh, 2, th.text);
        } else {
            g_colored(&cv, bx + 1, gy + gh - 3, width, 3, 1, th.dim);
        }
    }
}
/* the scale: the twelve notes as bars, those of the scale tall, the root in the accent (the scales' semitones are Felucca 2.0's) */
void pic_scl(KApp *a)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int root0 = table_val(a, K_PAGE_SCL, 0), scl = table_val(a, K_PAGE_SCL, 1);   /* (MASK's index: within the table's 0..15) */
    int i, gx = pic.x + 4, gw = pic.w - 8, bw = gw / 12, gy = pic.y + 6, gh = pic.h - 30;
    for (i = 0; i < 12; i++) {
        int note = (root0 + i) % 12, in = (MASK[scl & 15] >> i) & 1, root = i == 0, bx = gx + i * bw, bh = in ? gh : gh / 3;
        g_colored(&cv, bx + 2, gy + gh - bh, bw - 4, bh, 3, root ? th.accent : in ? th.text : th.line);
        g_text_box(&cv, bx, gy + gh + 4, bw, 15, GF_S, NN[note], root ? th.accent : in ? th.mid : th.dim, 0);
    }
}
