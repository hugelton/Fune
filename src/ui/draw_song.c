/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SONG: the tracks as lanes of pattern regions over the scenes. */
#include "draw_int.h"

/* SONG as the track view shows the tracks, after Logic Pro's pattern regions and Felucca 2.0's timeline: one well, the four
 * tracks as lanes (no names, no line: the regions carry the track's colour), the eight scenes left to right under a thin ruler of their
 * numbers. In a lane the scenes that play the same pattern one after the other make one region: a block in the track's colour,
 * the pattern's letter at its top left, the pattern in miniature inside it, repeated a scene at a time with a notch between
 * (a note's height for a synth, the lanes hit for a drum track). A silent scene ("-") is the empty lane with a faint line. The
 * scenes' boundaries are faint lines across the lanes. The cell in hand: its region lit, the others of the track in hand a
 * little less dimmed. */
static void song_mini(const KApp *a, int t, int pat, int x, int y, int w, int h, uint32_t ink)
{
    int i, lo = 127, hi = 0, drum = k_is_drum(a, t);
    uint16_t on = a->steps[t][pat];
    if (w < 4 || h < 3) return;
    for (i = 0; i < K_STEPS; i++)
        if ((on >> i) & 1) { int n = step_top(a, t, pat, i); if (n < lo) lo = n; if (n > hi) hi = n; }
    for (i = 0; i < K_STEPS; i++) {
        int cx = x + i * w / K_STEPS, cw = x + (i + 1) * w / K_STEPS - cx, by, bh = 2;
        if (drum) {
            int l, hits = 0;
            for (l = 0; l < K_LANES; l++) hits += (k_lanes(a, t, pat)[l] >> i) & 1;   /* (that pattern's lanes) */
            if (!hits) continue;
            bh = 2 + hits * (h - 2) / K_LANES;
            by = y + h - bh;
        } else {
            if (!((on >> i) & 1)) continue;
            by = hi > lo ? y + (h - 2) * (hi - step_top(a, t, pat, i)) / (hi - lo) : y + (h - 2) / 2;
        }
        g_fill(&cv, cx, by, cw > 1 ? cw - 1 : 1, bh, ink);
    }
}
void page_song(KApp *a)
{
    const int desk = (prof->input & K_IN_MOUSE) != 0;
    int r, c, hw = desk ? 22 : 4, ruler = g_text_h(GF_S) + 2;   /* (no names, no line: the regions are in the track's colour; a desktop: a chip a track, to select its row) */
    KRect w = pic;
    int rx = w.x + hw, rw = w.w - hw - 4, top = w.y + 2 + ruler, lh = (w.y + w.h - 3 - top) / K_TRACKS;
    int foc = a->focused < a->focus_count && a->focus[a->focused].kind == K_NODE_SCENE ? a->focus[a->focused].index : -1;
    const int off = a->song_off, end = off + K_SONG_VIS;   /* the window: scenes off .. end - 1 */
    char b[8];
    well(w);
#define SX(c) (rx + ((c) - off) * rw / K_SONG_VIS)
    for (c = off; c < end; c++) {   /* the ruler: the scenes' numbers; their boundaries faint across the lanes */
        int x0 = SX(c), x1 = SX(c + 1);
        snprintf(b, sizeof b, "%d", c + 1);
        song_col_hdr[c - off] = (KRect){(int16_t)x0, (int16_t)(w.y + 1), (int16_t)(x1 - x0), (int16_t)ruler};
        if (k_selected(a, K_SEL_SCENE) == c) g_rrect(&cv, x0 + 2, w.y + 1, x1 - x0 - 4, ruler, 4, th.sel);
        g_text_box(&cv, x0, w.y + 2 - (k_selected(a, K_SEL_SCENE) == c), x1 - x0, ruler, GF_S, b, k_selected(a, K_SEL_SCENE) == c ? th.ink : foc >= 0 && foc % K_SCENES == c ? th.text : th.dim, 0);   /* (in a pill: 1 px up) */
        if (c > off) g_fill(&cv, x0, top, 1, K_TRACKS * lh, k_mix(th.bg, th.text, 10));
    }
    g_fill(&cv, w.x + 4, top - 1, w.w - 8, 1, k_mix(th.bg, th.text, 14));
    for (r = 0; r < K_TRACKS; r++) {
        int y = top + r * lh, cur = r == a->track;
        uint32_t hue = track_hue(r);
        if (r) g_fill(&cv, rx, y, rw, 1, k_mix(th.bg, th.text, 8));
        song_row_hdr[r] = (KRect){0, 0, 0, 0};
        if (desk) {   /* the track's chip: its number in its colour (selected: lit) */
            char nb[4]; snprintf(nb, sizeof nb, "%d", r + 1);
            song_row_hdr[r] = (KRect){(int16_t)(w.x + 2), (int16_t)(y + 2), (int16_t)(hw - 3), (int16_t)(lh - 4)};
            g_rrect(&cv, w.x + 2, y + 2, hw - 3, lh - 4, 4, k_selected(a, K_SEL_ROW) == r ? hue : k_mix(th.bg, hue, 35));
            g_text_box(&cv, w.x + 2, y + 2, hw - 3, lh - 4, GF_S, nb, k_selected(a, K_SEL_ROW) == r ? th.ink : th.text, 0);
        }
        for (c = off; c < end; c++) {   /* each cell is a focus node and its own block */
            int x0 = SX(c), x1 = SX(c + 1), pat = a->song[c][r];
            k_set_bounds(a, K_NODE_SCENE, r * K_SCENES + c, (KRect){x0, y, x1 - x0, lh});
            if (pat >= 4) g_fill(&cv, x0 + 3, y + lh / 2, x1 - x0 - 6, 1, k_mix(th.bg, th.text, 14));   /* silent: the empty lane */
        }
        for (c = off; c < end;) {
            int pat = a->song[c][r], e = c, k, lit = 0, x0, x1;
            uint32_t face, ink;
            if (pat >= 4) { c++; continue; }
            /* (one block a scene, never joined into runs) */
            for (k = c; k <= e; k++) lit |= foc == r * K_SCENES + k;
            x0 = SX(c) + 1; x1 = SX(e + 1) - 1;
            face = lit ? hue : cur ? k_mix(hue, th.bg, 30) : k_mix(hue, th.bg, 55);
            ink = lit ? th.ink : k_mix(hue, 0xFFFFFF, cur ? 55 : 35);
            g_rrect(&cv, x0, y + 2, x1 - x0, lh - 4, 3, face);   /* the block, flat */
            if (c > off && a->song[c - 1][r] == pat) g_fill(&cv, x0, y + 2, 3, lh - 4, face);   /* the same pattern beside it: that side square, */
            if (c + 1 < end && a->song[c + 1][r] == pat) g_fill(&cv, x1 - 3, y + 2, 3, lh - 4, face);   /* a run reads (--|--) */
            b[0] = (char)('A' + pat); b[1] = 0;
            g_text(&cv, x0 + 3, y + 2, GF_S, b, lit ? th.ink : k_mix(hue, 0xFFFFFF, 70));
            for (k = c; k <= e; k++) {   /* the pattern again each scene; a notch between the passes */
                int sx = SX(k) + 3, sw = SX(k + 1) - 3 - sx, my = y + 2 + g_text_h(GF_S) + 1;
                if (k > c) g_fill(&cv, SX(k), y + lh - 7, 1, 4, k_mix(face, 0x000000, 45));
                song_mini(a, r, pat, sx, my, sw, y + lh - 5 - my, ink);
            }
            c = e + 1;
        }
    }
    if (foc >= 0 && (prof->input & K_IN_KNOBS)) {   /* the scene SELECT is on (its knobs set its cells): its column a little lighter */
        const int sc = foc % K_SCENES, x0 = SX(sc), x1 = SX(sc + 1);
        g_rrect_a(&cv, x0 + 1, top + 1, x1 - x0 - 2, K_TRACKS * lh - 2, 3, 0xFFFFFF, 48);
    }
    if (a->song_play && a->playing && a->song_scene >= off && a->song_scene < end) {   /* the scene playing: framed white, its playhead across it
                                                 * */
        const int sc = a->song_scene, x0 = SX(sc), x1 = SX(sc + 1), hh = K_TRACKS * lh;
        const int px = x0 + 2 + (x1 - x0 - 4) * (a->step < 0 ? 0 : a->step) / K_STEPS;
        g_fill(&cv, x0, top, x1 - x0, 1, th.text); g_fill(&cv, x0, top + hh - 1, x1 - x0, 1, th.text);
        g_fill(&cv, x0, top, 1, hh, th.text); g_fill(&cv, x1 - 1, top, 1, hh, th.text);
        g_fill(&cv, px, top + 1, 1, hh - 2, th.text);
        g_rrect(&cv, x0 + 2, w.y + 1, x1 - x0 - 4, ruler, 4, th.text);
        snprintf(b, sizeof b, "%d", sc + 1);
        g_text_box(&cv, x0, w.y + 1, x1 - x0, ruler, GF_S, b, th.ink, 0);   /* (its digit 1 px up in the pill) */
    }
    if (k_selected(a, K_SEL_SCENE) >= off && k_selected(a, K_SEL_SCENE) < end) {   /* the selection: a veil over its column or row */
        const int si = k_selected(a, K_SEL_SCENE), x0 = SX(si), x1 = SX(si + 1);
        g_rrect_a(&cv, x0, top, x1 - x0, K_TRACKS * lh, 3, th.sel, 50);
    } else if (k_selected(a, K_SEL_ROW) >= 0 && k_selected(a, K_SEL_ROW) < K_TRACKS) g_rrect_a(&cv, rx, top + k_selected(a, K_SEL_ROW) * lh, rw, lh, 3, th.sel, 50);
}
#undef SX
