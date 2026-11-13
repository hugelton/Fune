/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The header (the track, the page, the transport, the preset), the second header, the balloons and hints, the keycaps,
 * and SAVE's question. */
#include "draw_int.h"

/* -------------------------------------------------------------- header --- */
/* the second header: the engine (its icon and name) and the preset (no presets yet: the number is the sound's) */
void draw_header2(const KApp *a)
{
    char b[24];
    int cy = LINE_Y + 1 + (HEAD2_H - 1) / 2, k = a->sound[a->track], w;
    g_icon2(&cv, MARGIN, cy - 10, 20, (GIcon)engine_icon(k), k_mix(th.bg, th.text, 22), th.text);
    g_text(&cv, MARGIN + 26, cy - g_text_h(GF_M) / 2, GF_M, trk_engine(a, a->track), k_track_missing(a, a->track) ? th.rec : th.text);
    snprintf(b, sizeof b, "%s", trk_preset(a, a->track));
    w = g_text_w(GF_M, b);
    g_text(&cv, cv.w - MARGIN - 4 - w, cy - g_text_h(GF_M) / 2, GF_M, b, th.mid);
}
/* the header at rest: the track, play, the tempo and four beat dots, the page's icon. What a control just changed (the
 * level, the octave, the preset, a new page) drops in a balloon from the top for a moment (draw_balloon). */
/* the balloon (a preset's name, the octave, a page, SAVE's word): the hint's box, dropping from the top to just under the header,
 * the tempo left in sight. (MASTER is not a balloon but a gauge beside the tempo: draw_header.) */
/* a balloon's slide, Q16 (no float): in over 180 ms easing out, 1 - (1 - s / 180)^2; and away, (held - hold)^2 / out^2 */
static uint32_t ease_in16(int32_t shown)
{
    const uint32_t r = shown >= 180 ? 0u : (uint32_t)(180 - shown);
    return 65536u - (r * r * 65536u + 16200u) / 32400u;   /* (r^2 2^16 < 2^31) */
}
static uint32_t mul16(uint32_t a, uint32_t b) { return a >= 65536u ? b : a * b >> 16; }   /* a b of two Q16 fractions 0..1 */
static uint32_t ease_out_sq16(int32_t held)   /* held in K_BALLOON_HOLD_MS .. + K_BALLOON_OUT_MS: ((held - hold) / out)^2, rounded */
{
    const uint32_t d = (uint32_t)(held - (int32_t)K_BALLOON_HOLD_MS), q = K_BALLOON_OUT_MS * K_BALLOON_OUT_MS / 4u;   /* (d^2 2^14 < 2^32: d < 512) */
    return (d * d * 16384u + q / 2u) / q;
}
void draw_balloon(const KApp *a)
{
    /* as the hint (draw_hint), from the top: down in 180 ms under the header, holds, and goes back up */
    char b[32];
    int32_t shown = (int32_t)(now - a->balloon.in_ms), held = (int32_t)(now - a->balloon.held_ms);
    int h = 26, w, x0, rest, y, tw, ic, slide = 0, shake = 0;
    uint32_t k, side;   /* (Q16) */
    if (!a->balloon.kind || a->balloon.kind == K_BALLOON_MASTER || a->balloon.kind == K_BALLOON_HINT) return;
    if (!k_balloon_active(&a->balloon, now)) return;
    if ((a->balloon.kind == K_BALLOON_PRESET || a->balloon.kind == K_BALLOON_NO_PRESET) && !(a->lay & K_LAY_PRESET_POP) &&
        (a->page == K_PAGE_SOUND || a->page == K_PAGE_EDIT || a->page == K_PAGE_ENV1 || a->page == K_PAGE_ENV2 ||
         a->page == K_PAGE_LFO || K_IS_HOME(a->page))) return;   /* a synth page names the preset itself;
                                                 * HOME too (its tile and its cards say it) */
    if (shown < 0) shown = 0;
    if (held < 0) held = 0;
    if (held >= (int32_t)(K_BALLOON_HOLD_MS + K_BALLOON_OUT_MS)) return;
    switch (a->balloon.kind) {
    case K_BALLOON_OCTAVE: snprintf(b, sizeof b, "Oct %+d", a->octave); break;
    case K_BALLOON_PRESET: case K_BALLOON_DIRTY: snprintf(b, sizeof b, "%s", trk_preset(a, a->track)); break;
    case K_BALLOON_NO_PRESET: snprintf(b, sizeof b, "No presets"); break;
    case K_BALLOON_MISSING: {   /* (every one missing a newer version of an engine here: Update) */
        int t, upd = 0, gone = 0;
        for (t = 0; t < K_TRACKS; t++) if (k_track_missing(a, t)) { if (k_track_newer(a, t)) upd = 1; else gone = 1; }
        snprintf(b, sizeof b, "%s", upd && !gone ? "Update" : "No plugin");
    } break;
    case K_BALLOON_SAVED: snprintf(b, sizeof b, "Saved"); break;
    case K_BALLOON_SAVE_FAIL: snprintf(b, sizeof b, "Not saved"); break;
    case K_BALLOON_LOADED: snprintf(b, sizeof b, "Loaded"); break;
    case K_BALLOON_ERASED: snprintf(b, sizeof b, "Erased"); break;
    case K_BALLOON_NAMED: snprintf(b, sizeof b, "Renamed"); break;
    case K_BALLOON_STOP: snprintf(b, sizeof b, "Stop to save"); break;
    case K_BALLOON_TRACKS: snprintf(b, sizeof b, "Song of more tracks"); break;
    case K_BALLOON_PATTERN: snprintf(b, sizeof b, "Pattern %c", 'A' + (a->pattern[a->track] & 3)); break;
    case K_BALLOON_UNDO: snprintf(b, sizeof b, "Undo"); break;   /* (a larger device's: it does not open here) */
#if K_WITH_CONNECT
    case K_BALLOON_LEARN: snprintf(b, sizeof b, "%s", a->learn_say); break;   /* MIDI learn's stage (kreisel_io.h) */
#endif
    default: snprintf(b, sizeof b, "%s", track_page_name(a, a->page)); break;
    }
    k = ease_in16(shown);   /* down in 180 ms, easing out */
    side = a->balloon.kind == K_BALLOON_PRESET && a->balloon.dir ? 65536u - k : 0u;   /* PRESETS: from the side it turned to, not from the top */
    if (side) k = 65536u;
    if (held >= (int32_t)K_BALLOON_HOLD_MS) {   /* and away: up, or a preset's back to the side it came from */
        const uint32_t oo = ease_out_sq16(held);
        if (a->balloon.kind == K_BALLOON_PRESET && a->balloon.dir) { side = oo; k = 65536u; }
        else k = mul16(k, 65536u - oo);
    }
    /* (a later detent while it is up: the name changes in place, no slide: turned on and on, it is not animated) */
    if (a->balloon.kind == K_BALLOON_DIRTY && shown < 420) {   /* no: a shake, as a wrong password's field (two and a half swings, dying) */
        const int sn = g_isin((uint32_t)shown * 35791394u + (uint32_t)shown * 16u / 120u), num = sn * 7 * (420 - shown), den = 420 * 16384;   /* (3.5 turns in 420 ms) */
        shake = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
    }
    ic = (a->balloon.kind == K_BALLOON_PRESET || a->balloon.kind == K_BALLOON_DIRTY) && !k_track_missing(a, a->track) ? 16 + 4 : 0;   /* a preset: its engine's icon before it */
    tw = g_text_w(GF_M, b);
    if (tw > cv.w - 2 * MARGIN - 40 - ic) tw = cv.w - 2 * MARGIN - 40 - ic;
    w = tw + ic + 24 < 96 ? 96 : tw + ic + 24;
    x0 = (cv.w - w) / 2 + shake + a->balloon.dir * (int)((side * (uint32_t)((cv.w + w) / 2 + 8) + 32768u) >> 16); rest = HEAD_H + 6;
    y = rest - (int)(((uint32_t)(rest + h + 6) * (65536u - k) + 32768u) >> 16);
    g_drop(&cv, x0, y, w, h, 6, 3, 50);
    g_rrect_frame(&cv, x0, y, w, h, 9, a->balloon.kind == K_BALLOON_DIRTY ? th.rec : th.edge, th.surf);   /* (refused: a red rim) */
    {
        const int tx = x0 + (w - tw - ic) / 2 + slide;
        if (ic) preset_icon(a, a->track, tx, y + h / 2, 16, th.mid);
        text_fit(tx + ic, y + (h - g_text_h(GF_M)) / 2 - 1, GF_M, b, tw, th.text);
    }
}
/* the hint (K_BALLOON_HINT): a balloon that pops up from the bottom of the screen, holds, and drops back: the control on a round
 * cushion, then what it does, "(ALGORITHM) move row" */
void draw_hint(const KApp *a)
{
    const int lanes = (prof->input & K_IN_KEYS) && k_is_drum(a, a->track);   /* a drum track on a device with keys: they pick the lane */
    const char *cap = lanes ? "BLACK KEY" : "SELECT", *what = lanes ? "select lane" : "move row";   /* (the white keys are the lane's steps) */
    /* the page's (SYNTH: a page opened) or a drum track's, arrived at on any page (KApp.hint: only where the keys pick the lane) */
    const KBalloon *b = a->balloon.kind == K_BALLOON_HINT && k_balloon_active(&a->balloon, now) ? &a->balloon : &a->hint;
    int32_t shown = (int32_t)(now - b->in_ms), held = (int32_t)(now - b->held_ms);
    int cw, ww, w2, h = 26, x0, rest, y, travel;
    uint32_t k;   /* (Q16) */
    if (!(prof->input & (K_IN_KNOBS | K_IN_KEYS))) return;   /* (SELECT, the black keys: a device that has them; a mouse or a pad has its own ways) */
    if (b->kind != K_BALLOON_HINT || !k_balloon_active(b, now) || (b == &a->hint && !lanes)) return;
    if (shown < 0) shown = 0;
    if (held < 0) held = 0;
    if (held >= (int32_t)(K_BALLOON_HOLD_MS + K_BALLOON_OUT_MS)) return;
    k = ease_in16(shown);   /* up in 180 ms, easing out */
    if (held >= (int32_t)K_BALLOON_HOLD_MS) k = mul16(k, 65536u - ease_out_sq16(held));   /* and down */
    cw = g_text_w(GF_S, cap) + 12; ww = g_text_w(GF_M, what); w2 = 8 + cw + 8 + ww + 12;
    x0 = (cv.w - w2) / 2; rest = cv.h - MARGIN - 6 - h; travel = cv.h - rest;
    y = rest + (int)(((uint32_t)travel * (65536u - k) + 32768u) >> 16);
    g_drop(&cv, x0, y, w2, h, 6, 3, 50);
    g_rrect_frame(&cv, x0, y, w2, h, 9, th.edge, th.surf);
    g_rrect(&cv, x0 + 6, y + 4, cw, h - 8, (h - 8) / 2, k_mix(th.surf, th.text, 72));   /* the cushion: round at the ends */
    g_text_box(&cv, x0 + 6, y + 4, cw, h - 8, GF_S, cap, th.ink, 0);
    g_text(&cv, x0 + 6 + cw + 8, y + (h - g_text_h(GF_M)) / 2 - 1, GF_M, what, th.text);   /* (1 px up: on the cushion's middle) */
}
/* a key of the device drawn as a cap (OCT+, K1, SAVE): where the screen would otherwise say which key does what (
 * no lines of how-to on the screens). lit: the key that does it (accent), else a grey cap. Its width */
int keycap(int x, int y, const char *label, int lit)
{
    const int w = g_text_w(GF_S, label) + 12;
    g_rrect(&cv, x, y, w, 16, 8, lit ? th.accent : k_mix(th.surf, th.text, 40));
    g_text_box(&cv, x, y, w, 16, GF_S, label, lit ? th.ink : th.text, 0);
    return w;
}
int keycap_w(const char *label) { return g_text_w(GF_S, label) + 12; }
int keycap_n(int x, int y, const char *label, int lit)   /* a narrow cap (less padding), where a line of them is tight (NAME) */
{
    const int w = g_text_w(GF_S, label) + 6;
    g_rrect(&cv, x, y, w, 16, 7, lit ? th.accent : k_mix(th.surf, th.text, 40));
    g_text_box(&cv, x, y, w, 16, GF_S, label, lit ? th.ink : th.text, 0);
    return w;
}
/* the device's own key for a job (K_KEY_*), as its cap says, or 0: it has none (its profile's table). Only a key the device has is
 * drawn or named (no FM-1 keys on the other devices; an MPC or a Behringer box has its own); where it has none,
 * a card is pressed again, a button is there to press, Enter or A does it, Esc or B lets it go */
const char *key_name(int role) { return prof && prof->keys && role >= 0 && role < K_KEY_ROLES ? prof->keys[role] : 0; }
/* SAVE's question on a synth page (KApp.save_ask): overwrite the user preset in hand? SAVE again (or OCT+) yes, anything else no */
void draw_save_ask(const KApp *a)
{
    const int w = cv.w - 2 * MARGIN - 24 < 220 ? cv.w - 2 * MARGIN - 24 : 220, h = key_name(K_KEY_SAVE) ? 78 : 58,   /* (no SAVE key drawn: shorter) */ x = (cv.w - w) / 2, y = (cv.h - h) / 2 + 6;
    const int lh = g_text_h(GF_M);
    char l2[32];
    if (!a->save_ask) return;
    g_rrect_a(&cv, 0, 0, cv.w, cv.h, 0, 0x000000, MENU_VEIL);   /* the page behind it sunk (flat: no shadow) */
    g_rrect_frame(&cv, x, y, w, h, 10, th.accent, th.surf);
    g_text_box(&cv, x, y + 8, w, lh, GF_M, "Overwrite preset?", th.text, 0);
    snprintf(l2, sizeof l2, "%s", a->preset_name[a->track]);
    g_text_box(&cv, x + 10, y + 10 + lh, w - 20, lh, GF_M, l2, th.accent, 0);
    if (key_name(K_KEY_SAVE)) keycap(x + (w - keycap_w(key_name(K_KEY_SAVE))) / 2, y + h - 24, key_name(K_KEY_SAVE), 1);   /* the answer: SAVE again (anything else lets it go) */
}
/* a debugging aid (ui_draw_outline): each part of the header in a red frame, as CSS's outline: 1px solid red */
static int outline_on;
void ui_draw_outline(int on) { outline_on = on; }
KRect hdr_tgt[8]; int hdr_kind[8], hdr_tn;   /* the header's parts a pad's cursor can stand on (ui_pad), left to right */
static void hdr_target(int kind, int x, int y, int w, int h) { if (hdr_tn < 8) { hdr_tgt[hdr_tn] = (KRect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}; hdr_kind[hdr_tn++] = kind; } }
static KRect hdr_box[16];
static int hdr_n;
static void hdr_part(int x, int y, int w, int h) { if (hdr_n < 16) hdr_box[hdr_n++] = (KRect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}; }
void draw_header(const KApp *a)
{
    char b[32];
    const int desk = (prof->input & K_IN_MOUSE) != 0, up = desk ? 0 : 1;   /* a desktop: the parts on the bar's middle (it is HEAD_H + 2 tall), the
                                                                           * icons of one size (18), the text not lifted (the FM-1's bitmaps are) */
    const int pi = desk ? 18 : PICON;
    int x = MARGIN, cy = desk ? (HEAD_H + 2) / 2 : HEAD_H / 2, w, d, right;
    hdr_n = 0; hdr_tn = 0;
    g_vgrad(&cv, 0, 0, cv.w, HEAD_H + 2, k_mix(th.bg, 0xFFFFFF, 9), k_mix(th.bg, 0xFFFFFF, 3));   /* the bar: a step lighter than the ground, a dark line under it */
    g_hline_a(&cv, 0, HEAD_H + 2, cv.w, 0x000000, 150);
    b[0] = (char)('1' + a->track); b[1] = 0;
    g_colored(&cv, x, cy - 8, 16, 16, 4, track_hue(a->track));   /* the track in hand: its number in its colour (16, the small font: 
                                                 * header C, mockups/2026-11-12_header_v2.png) */
    g_text_box(&cv, x, cy - 8, 16, 16, GF_S, b, th.ink, 0);
    hdr_part(x, cy - 8, 16, 16); hdr_target(HT_TRACK, x - 2, cy - 10, 20, 20);
    /* one gap between the parts of a group (HGAP): chip, play, record | tempo, beat, level | page, battery */
#define HGAP 8
    x += 16 + HGAP;
    g_icon(&cv, x - TICON * 6 / 22, cy - TICON / 2, TICON, a->playing ? GI_PLAY : GI_STOP, th.text);   /* the Fill glyphs: solid (its ink 6/22 in from its box, 11/22 wide) */
    hdr_part(x - TICON * 6 / 22, cy - TICON / 2, TICON, TICON); hdr_target(HT_PLAY, x - TICON * 6 / 22, cy - TICON / 2, TICON, TICON);
    x += TICON * 11 / 22 + HGAP;
    {   int left = 0;   /* (where the tempo may start: right of the record circle and the song's disc) */
    if (a->recording) {   /* a red circle while recording, in a place of its own: the tempo does not move for it */
        g_icon(&cv, x - 3, cy - 9, 18, GI_RECORD, th.rec); hdr_part(x - 3, cy - 9, 18, 18);
        left = x - 3 + 18 + 3;
    }
    if (a->song_play && a->playing) {   /* a song playing: Fukiai's disc, then the scene of how many (3/8), after the record circle */
        char sc[8];
        int sx = a->recording ? x + 18 : x - 3, last = 0, c, r;
        for (c = 0; c < K_SCENES; c++) for (r = 0; r < K_TRACKS; r++) if (a->song[c][r] < 4) last = c + 1;
        snprintf(sc, sizeof sc, "%d/%d", a->song_scene + 1, last);
        g_icon(&cv, sx, cy - pi / 2 - (desk ? 0 : 0), desk ? 18 : 16, GI_SONG, th.accent);
        left = g_text(&cv, sx + (desk ? 20 : 18), cy - g_text_h(GF_S) / 2 - up, GF_S, sc, th.text) + 4;
        hdr_part(sx, cy - 8, left - 4 - sx, 16);
    }
    snprintf(b, sizeof b, "%d.0", a->bpm);
    w = g_text_w(GF_M, b);
    {   /* the tempo with its four beat dots, and the output's meter: a pair, centred as a whole */
        int dots = desk ? HGAP + 25 : HGAP + 10, gap = HGAP, gw = cv.w <= 240 ? 30 : 44;   /* (a desktop: the four dots in a row) */   /* (the meter: wider than the master's gauge it replaced, 2026-11-07; the FM-1's
                                                 * shorter, for the preset's name) */
        const int mk = (prof->input & K_IN_MOUSE) ? 26 : 0;   /* a desktop has no level knob of its own: one after the meter */
        int px = (cv.w - (w + dots + gap + gw + mk)) / 2, gx;
        if (!(prof->input & K_IN_MOUSE)) {   /* a device: the tempo and the meter to the right, against the page's icon, so the preset's name
                                         * has the room after play; a desktop keeps them centred */
            int rr = cv.w - MARGIN - 4;
            if (a->battery >= 0) rr = cv.w - MARGIN - (BAT * 438 + 250) / 500 + BAT * 75 / 500 - (HGAP - 1);
            rr -= pi + HGAP + 6;
            if ((prof->input & K_IN_TOUCH) && k_tools_page(a) && !tools_rail(a)) rr -= HGAP + 4 + pi;
            px = rr - (w + dots + gap + gw);
        }
        if (px < left) px = left;   /* (the record circle, 53..71, and the song's disc have their places) */
        {   /* the track in hand's preset, after play (cut short with "..." where it is long) */
            const int sx = left > x ? left : x, avail = px - (cv.w <= 240 ? 8 : 14) - sx, small = cv.w <= 240; (void)small;
            header_preset = (KRect){0, 0, 0, 0};
            if (avail > 22 && !(a->lay & K_LAY_PRESET_POP)) {   /* (the FM-1: a balloon instead) */
                const int iw = 0, tw = g_text_w(GF_S, trk_preset(a, a->track));   /* (the name alone: no library icon before it) */
                const int shown = tw < avail - iw ? tw : avail - iw;
                text_fit(sx + iw, cy - g_text_h(GF_S) / 2, GF_S, trk_preset(a, a->track), avail - iw, th.text);
                header_preset = (KRect){(int16_t)(sx - 5), 1, (int16_t)(iw + shown + 10), (int16_t)HEAD_H};   /* (a click: the presets; lit as the bar's other buttons) */
                hdr_part(sx, cy - 9, 18, 18); hdr_part(sx + iw, cy - g_text_h(GF_S) / 2, shown, g_text_h(GF_S));
                hdr_target(HT_PRESET, sx, cy - 9, iw + shown, 18);
            }
        }
        hdr_part(px, cy - g_text_h(GF_M) / 2 - up, w, g_text_h(GF_M)); hdr_target(HT_TEMPO, px - 2, cy - g_text_h(GF_M) / 2 - up, w + 4, g_text_h(GF_M)); hdr_part(px + w + HGAP, cy - 5, dots - HGAP, 10); hdr_part(px + w + dots + gap, cy - 3, gw, 5);
        if (desk) hdr_target(HT_CLOCK, px + w + HGAP - 3, cy - 8, dots - HGAP + 6, 16);   /* (the dots: a click, the clock's balloon) */
        x = g_text(&cv, px, cy - g_text_h(GF_M) / 2 - up, GF_M, b, th.text);   /* (1 px up: the figures' ink on the bar's middle, as the chip, play, dots, battery) */
        for (d = 0; d < 4; d++)   /* four dots count the beat while playing: 2 x 2, as tall as the figures (10 px), clockwise from the top left; a
                                   * desktop: in a row */
            if (desk) g_rrect(&cv, px + w + HGAP + d * 7, cy - 2, 4, 4, 2, d == a->beat ? th.accent : th.dim);
            else g_rrect(&cv, px + w + HGAP + (d == 1 || d == 2) * 6, cy - 5 + (d >> 1) * 6, 4, 4, 2, d == a->beat ? th.accent : th.dim);
        gx = px + w + dots + gap;   /* the output's level, left over right: two lanes of 2 px, a pixel apart (5 px: centred on the bar
                                     * with the dots' 10), green to -14 dB, yellow to -5, red over; no number, no box, always shown */
        {   int ln;
            for (ln = 0; ln < 2; ln++) {
                const int ly = cy - 3 + ln * 3, fill = gw * a->out_lv[ln] / 100;
                g_fill(&cv, gx, ly, gw, 2, k_mix(th.bg, th.text, 14));
                if (fill > 0) {
                    const int g1 = gw * 70 / 100, g2 = gw * 90 / 100;
                    g_fill(&cv, gx, ly, fill < g1 ? fill : g1, 2, th.ok);
                    if (fill > g1) g_fill(&cv, gx + g1, ly, (fill < g2 ? fill : g2) - g1, 2, 0xFFD60A);
                    if (fill > g2) g_fill(&cv, gx + g2, ly, fill - g2, 2, th.rec);
                }
            } }
        header_master = (KRect){0, 0, 0, 0};
        if (mk) {   /* the output level (KApp.master): the wheel or a drag up and down turns it */
            const int kx = gx + gw + 8 + 9;
            header_master = (KRect){(int16_t)(kx - 14), 1, 28, (int16_t)HEAD_H};
            knob(kx, cy, 7, a->master, 0, th.sel, k_mix(th.bg, th.text, 30), th.text);   /* (18 across with its ring: the icons' size) */
            hdr_part(kx - 9, cy - 9, 18, 18); hdr_target(HT_MASTER, kx - 9, cy - 9, 18, 18);
        }
    } }
    w = pi;
    right = cv.w - MARGIN - 4;
    if (a->battery >= 0) {   /* the battery, top right: low is the warning colour */
        GIcon bi = a->charging ? GI_BATTERY_CHARGING : (GIcon)(GI_BATTERY_0 + a->battery);
        int bx = cv.w - MARGIN - (BAT * 438 + 250) / 500;   /* the glyph's nub (x 438 of 500) at the margin */
        right = bx + BAT * 75 / 500;                         /* its left edge (x 75) */
        uint32_t line = a->charging ? th.ok : a->battery <= 1 ? th.rec : th.text;   /* charging: the bolt is green */
        g_icon2(&cv, bx, cy - BAT / 2, BAT, bi, k_mix(th.bg, a->charging ? th.text : line, 30), line);
        hdr_part(bx, cy - BAT / 2, BAT, BAT);
        right -= HGAP - 1;   /* (the page icon's ink stops a pixel inside its box) */
    }
    header_page = (KRect){right - w, 0, w, HEAD_H};
    g_icon2(&cv, header_page.x, cy - pi / 2, pi, page_icon(a->page), k_mix(th.bg, th.text, 22), a->menu_open ? th.text : th.mid);
    hdr_part(header_page.x, cy - pi / 2, pi, pi); hdr_target(HT_PAGE, header_page.x, cy - pi / 2, pi, pi);
    header_tools = (KRect){0, 0, 0, 0};
    if ((prof->input & K_IN_TOUCH) && k_tools_page(a) && !tools_rail(a)) {   /* a touch screen: the page's tools, a button beside its icon */
        header_tools = (KRect){(int16_t)(header_page.x - HGAP - 4 - pi), 0, (int16_t)pi, HEAD_H};
        g_icon(&cv, header_tools.x, cy - pi / 2, pi, GI_T_TOOLS, a->tools_open ? th.accent : th.mid);
        hdr_part(header_tools.x, cy - pi / 2, pi, pi); hdr_target(HT_TOOLS, header_tools.x, cy - pi / 2, pi, pi);
        if (desk) header_tools = (KRect){(int16_t)(header_tools.x - 5), 1, (int16_t)(pi + 10), HEAD_H};   /* (lit as the bar's other buttons) */
    }
    if (outline_on) {   /* each part in a red frame, and the bar's middle line */
        int i;
        for (i = 0; i < hdr_n; i++) {
            const KRect r = hdr_box[i];
            g_fill(&cv, r.x, r.y, r.w, 1, 0xFF0000); g_fill(&cv, r.x, r.y + r.h - 1, r.w, 1, 0xFF0000);
            g_fill(&cv, r.x, r.y, 1, r.h, 0xFF0000); g_fill(&cv, r.x + r.w - 1, r.y, 1, r.h, 0xFF0000);
        }
        g_fill(&cv, 0, cy, cv.w, 1, 0x00A0FF);
    }
}
