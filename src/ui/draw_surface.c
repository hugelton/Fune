/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A touch-only device's surface: what is played and pressed on the glass, beside the page
 * the UI draws as it always does. It is an outside keyboard put on the screen, not a copy of the FM-1's keys: it sends the
 * core's own entries (k_note_in, k_lane_hit, k_bend_in, k_mod_in, PLAY, REC, the octave, the cursor's moves), never the FM-1's
 * key numbers or its key combinations.
 *
 * Laid out as a split screen (as Logic Pro, VS Code, Illustrator): a bar of tabs along the bottom, the
 * surfaces' icons at its left, play, rec and the octave at its right. A tab slides its panel up from the bar and the page shrinks
 * into the room above it as it comes: the same picture, smaller, its proportions kept (relaid out, the page was
 * squashed), so nothing is covered and nothing moves about. The same tab again, or
 * the tracker view closing, slides it down. The page and the bar use the UI's own icons and colours.
 *
 * Panels:
 *  - Keys: a keyboard from F (as the FM-1's), velocity by where the key is pressed, glissando; bend and mod strips
 *  - Pads: the drum track's 8 lanes
 *  - Macros: the track's 4 macros as wide strips
 *  - Pad: a game pad for the tracker view: arrows, OK and back, the value up and down; held, they repeat
 * A drum track brings Pads, a synth track Keys (when a panel is up); the tracker view brings Pad up and gives the panel before
 * back when it closes. Each finger keeps what it holds (up to 10). The first finger on the page goes to ui_touch.
 * Not built for the FM-1 (no touch: KREISEL_BITMAP_FONT). */
#include "draw_int.h"
#ifndef KREISEL_BITMAP_FONT

enum { F_KEYS, F_PADS, F_MACROS, F_PAD, F_COUNT };
enum { G_NONE, G_PAGE, G_KEY, G_LANE, G_BEND, G_MOD, G_MACRO, G_REPEAT, G_VEL };
enum { B_PLAY, B_REC, B_PATTERN, B_MENU, B_TOOLS, B_SAVE, B_OCTDN, B_OCTUP, B_TAB0, B_PAD_UP = B_TAB0 + F_COUNT, B_PAD_DOWN, B_PAD_LEFT,
       B_PAD_RIGHT, B_PAD_OK, B_PAD_BACK, B_PAD_INC, B_PAD_DEC, B_COUNT };
#define FINGERS 10
#define SIDE_W 0           /* (the buttons' column is inside the surface, at its right) */
#define KEY_BASE 53          /* the leftmost F at octave 0 (F3): the FM-1's leftmost key */
#define BAR_H 0              /* (no bar since 2026-11-13: the surface is always up) */
#define SLIDE_MS 180u        /* a panel coming up or going down */

static GCanvas scv;
static int SW_, SH_, panel_h, sf_open = 1, sf_up = 1, sf_face = F_KEYS, auto_pad, pad_was_open, pad_face_before, last_track = -1, mod_v;
static uint32_t slide_t0, slide_ms;   /* (the slide: when it began; 0 none) */
static int slide_from, slide_to;      /* (its height, from and to, in units) */
static KRect side_r, vel_r, page_box, shown_r, bar_r, panel_r, sf_content, sf_btn[B_COUNT], bend_r, mod_r, keys_r;
static struct { int id, grab, a, y0, x0, v0; uint32_t next; } fg[FINGERS];
static int page_finger = -1, sf_relayout, fg_ready;
int ui_surface_relayout(void) { const int r = sf_relayout; sf_relayout = 0; return r; }   /* (0 now: the page is never laid out again for a panel, it is shown smaller) */

static int sf_in(KRect r, int x, int y) { return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
static KRect rr(int x, int y, int w, int h) { return (KRect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ---------------------------------------------------------------------------------------------------- the layout --- */
static void sf_place(void)   /* the bar's buttons, and the panel's parts at its full height */
{
    int i, x, y, cw, ch;
    for (i = 0; i < B_COUNT; i++) sf_btn[i] = rr(0, 0, 0, 0);
    bar_r = rr(0, SH_, SW_, 0);   /* (no bar: the surface always up) */
    panel_r = rr(0, SH_ - panel_h, SW_ - SIDE_W, panel_h);
    side_r = rr(0, 0, 0, 0);
    x = panel_r.x + 10; y = panel_r.y + 10; ch = panel_h - 20; cw = panel_r.w - 20;
    {   /* the buttons in one column at the panel's right: play, rec, the next pattern, the page list, the tools, save */
        static const int ORDER[6] = {B_PLAY, B_REC, B_PATTERN, B_MENU, B_TOOLS, B_SAVE};
        const int bw = 34, bh = (ch - 5 * 3) / 6;
        for (i = 0; i < 6; i++) sf_btn[ORDER[i]] = rr(x + cw - bw, y + i * (bh + 3), bw, bh);
        cw -= bw + 10;
    }
    vel_r = mod_r = bend_r = rr(0, 0, 0, 0);
    if (sf_face == F_KEYS) {   /* (as Logic Pro's: Velocity, Mod, Pitch, each a strip with its name over it; the octave over them) */
        sf_btn[B_OCTDN] = rr(x, y - 4, 26, 20); sf_btn[B_OCTUP] = rr(x + 78, y - 4, 26, 20);
        vel_r = rr(x, y + 20, 32, ch - 20); mod_r = rr(x + 36, y + 20, 32, ch - 20); bend_r = rr(x + 72, y + 20, 32, ch - 20);   /* (each its name inside) */
        x += 112; cw -= 112;
    } else if (sf_face == F_PADS) {
        vel_r = rr(x, y, 32, ch);
        x += 40; cw -= 40;   /* (pads: the Velocity strip) */
    }
    sf_content = rr(x, y, cw, ch);
    keys_r = rr(x, y, cw, ch);
    if (sf_face == F_PAD && sf_open) {   /* a cross at the left, back and OK in the middle, the value at the right */
        const int c = imin(ch / 3, 52), cx = x + 4, cy = y + (ch - 3 * c) / 2, bw = 72, rx = x + cw - 80, vh = imin(ch / 2 - 6, 64);
        sf_btn[B_PAD_UP] = rr(cx + c, cy, c, c); sf_btn[B_PAD_DOWN] = rr(cx + c, cy + 2 * c, c, c);
        sf_btn[B_PAD_LEFT] = rr(cx, cy + c, c, c); sf_btn[B_PAD_RIGHT] = rr(cx + 2 * c, cy + c, c, c);
        {   const int mx = cx + 3 * c + (rx - (cx + 3 * c) - 2 * bw - 10) / 2, by = y + (ch - 56) / 2;
            sf_btn[B_PAD_BACK] = rr(mx, by, bw, 56); sf_btn[B_PAD_OK] = rr(mx + bw + 10, by, bw, 56); }
        sf_btn[B_PAD_INC] = rr(rx, y + ch / 2 - 4 - vh, 76, vh);
        sf_btn[B_PAD_DEC] = rr(rx, y + ch / 2 + 4, 76, vh);
    }
}
void ui_surface_layout(int w, int h, int *pw, int *ph)
{
    SW_ = w; SH_ = h;
    panel_h = h * 26 / 100 > 130 ? 130 : h * 26 / 100;   /* (a quarter of the height, at most 130: it was too big) */
    if (h - 360 >= 96 && h - 360 <= 170) panel_h = h - 360;   /* (the web app's 520 high: the page keeps the desktop's 360) */   /* (room for a 16:9 page: the
                                                 * web app's 640 x 520 leaves it 640 x 360) */
    if (h - panel_h < 240) panel_h = h - 240;   /* (the page keeps 240 units: a phone's panel is lower) */
    if (panel_h < 96) panel_h = 96;
    sf_place();
    page_box = rr(0, 0, w - SIDE_W, h - panel_h);   /* (the page above the panel, which is always up; the buttons' column at the right) */
    shown_r = page_box;
    *pw = page_box.w; *ph = page_box.h;
}
static void slide(int open, uint32_t now)   /* a panel up (open) or down; the page's room follows (ui_surface_relayout) */
{
    if (open == sf_open) return;
    slide_from = sf_up ? panel_h : 0; slide_to = open ? panel_h : 0;
    sf_open = open; slide_t0 = now ? now : 1; slide_ms = SLIDE_MS;
    if (!open) sf_up = 0;
    sf_place();
}
int ui_surface_init(int s)
{
    if (scv.px) g_free(&scv);
    return g_init_scaled(&scv, SW_, SH_, s < 1 ? 1 : s);
}
const GCanvas *ui_surface_canvas(void) { return &scv; }

/* --------------------------------------------------------------------------------------------------- the keyboard --- */
static const int8_t WHITE_F[7] = {0, 2, 4, 6, 7, 9, 11};      /* F G A B C D E, from F */
static const int8_t BLACK_AFTER_F[7] = {1, 1, 1, 0, 1, 1, 0};  /* F# G# A# - C# D# - */
static const int8_t WHITE_C[7] = {0, 2, 4, 5, 7, 9, 11};      /* C D E F G A B, from C (a desktop: as its computer keys) */
static const int8_t BLACK_AFTER_C[7] = {1, 1, 0, 1, 1, 1, 0};  /* C# D# - F# G# A# - */
static int sf_desk, sf_vel = 100;   /* (the velocity of a key or a pad clicked or typed: the Velocity strip) */
int ui_surface_velocity(void) { return sf_vel; }
void ui_surface_velocity_delta(int d) { sf_vel += d; if (sf_vel < 1) sf_vel = 1; if (sf_vel > 127) sf_vel = 127; }
static int sf_desk_unused;   /* a desktop browser: the keys from C, ten white ones, each with its computer key (z s x d c v g b h n j m , l . /) */
void ui_surface_set_desktop(int on) { sf_desk = on; }
#define WHITE (sf_desk ? WHITE_C : WHITE_F)
#define BLACK_AFTER (sf_desk ? BLACK_AFTER_C : BLACK_AFTER_F)
static const char DESK_KEYS[] = "awsedftgyhujkolp;'";   /* (as Logic Pro's: C3 .. F4 at octave 0; the same as kw_key's) */
static uint8_t key_down[128];   /* notes held on the computer's keys (lit as a finger's) */
void ui_surface_key_held(int note, int on) { if (note >= 0 && note < 128) key_down[note] = (uint8_t)(on != 0); }
static int nwhite(void) { const int n = keys_r.w / (sf_desk ? 24 : 40); return n < 7 ? 7 : n > 28 ? 28 : n; }   /* (a desktop: narrow keys, as Logic's) */
static int key_base(const KApp *a) { return (sf_desk ? 36 : KEY_BASE) + a->octave * 12; }   /* (a desktop: from C2, the typed keys from C3) */
static int typed_key(const KApp *a, int note) { const int k = note - key_base(a) - (sf_desk ? 12 : 0); return sf_desk && k >= 0 && k < (int)sizeof DESK_KEYS - 1 ? DESK_KEYS[k] : 0; }
static KRect white_r(int i) { const int n = nwhite(), w = keys_r.w / n; return rr(keys_r.x + i * w, keys_r.y, w - 1, keys_r.h); }
static KRect black_r(int i) { const KRect r = white_r(i); const int bw = r.w * 62 / 100; return rr(r.x + r.w - bw / 2, r.y, bw, r.h * 62 / 100); }
/* the key under (x, y): its note, the velocity from where it is pressed (top soft .. bottom hard); -1 none */
static int key_at(const KApp *a, int x, int y, int *vel)
{
    const int n = nwhite();
    int i;
    for (i = 0; i < n; i++)
        if (BLACK_AFTER[i % 7] && i + 1 < n && sf_in(black_r(i), x, y)) {
            const KRect r = black_r(i);
            *vel = sf_desk ? sf_vel : 40 + (y - r.y) * 87 / (r.h ? r.h : 1);
            return key_base(a) + (i / 7) * 12 + WHITE[i % 7] + 1;
        }
    for (i = 0; i < n; i++)
        if (sf_in(white_r(i), x, y)) {
            const KRect r = white_r(i);
            *vel = sf_desk ? sf_vel : 40 + (y - r.y) * 87 / (r.h ? r.h : 1);
            return key_base(a) + (i / 7) * 12 + WHITE[i % 7];
        }
    return -1;
}
static int held_note(int note) { int i; if (note >= 0 && note < 128 && key_down[note]) return 1; for (i = 0; i < FINGERS; i++) if (fg[i].grab == G_KEY && fg[i].a == note) return 1; return 0; }
static int held_lane(int l) { int i; for (i = 0; i < FINGERS; i++) if (fg[i].grab == G_LANE && fg[i].a == l) return 1; return 0; }
static KRect pad_r(int l) { const int w = (sf_content.w - 3 * 8) / 4, h = (sf_content.h - 8) / 2; return rr(sf_content.x + (l % 4) * (w + 8), sf_content.y + (l / 4) * (h + 8), w, h); }
static KRect macro_r(int i) { const int h = (sf_content.h - 3 * 8) / 4; return rr(sf_content.x, sf_content.y + i * (h + 8), sf_content.w, h); }

/* ---------------------------------------------------------------------------------------------------- drawing --- */
static void chevron(int cx, int cy, int up, int size, uint32_t col)   /* a thin arrowhead, up or down */
{
    g_line(&scv, cx - size, cy + (up ? size / 2 : -size / 2), cx, cy + (up ? -size / 2 : size / 2), 2, col);
    g_line(&scv, cx, cy + (up ? -size / 2 : size / 2), cx + size, cy + (up ? size / 2 : -size / 2), 2, col);
}
static void sf_icon(int b, KRect r, uint32_t col)
{
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    switch (b) {   /* (the UI's own icons, as the header has them) */
    case B_REC: g_icon(&scv, cx - 9, cy - 9, 18, GI_RECORD, col); break;
    case B_OCTDN: g_icon(&scv, cx - 9, cy - 9, 18, GI_CARET_L, col); break;
    case B_OCTUP: g_icon(&scv, cx - 9, cy - 9, 18, GI_CARET_R, col); break;
    case B_TAB0 + F_KEYS: g_icon(&scv, cx - 11, cy - 11, 22, GI_PAGE_SCL, col); break;
    case B_TAB0 + F_PADS: g_icon(&scv, cx - 11, cy - 11, 22, GI_PAGE_DRUM, col); break;
    case B_TAB0 + F_MACROS: g_icon(&scv, cx - 11, cy - 11, 22, GI_P_GENERIC, col); break;
    case B_TAB0 + F_PAD: g_icon(&scv, cx - 11, cy - 11, 22, GI_P_TRANSPOSE, col); break;
    case B_PAD_UP: chevron(cx, cy, 1, 10, col); break;
    case B_PAD_DOWN: chevron(cx, cy, 0, 10, col); break;
    case B_PAD_LEFT: g_icon(&scv, cx - 10, cy - 10, 20, GI_CARET_L, col); break;
    case B_PAD_RIGHT: g_icon(&scv, cx - 10, cy - 10, 20, GI_CARET_R, col); break;
    case B_PAD_OK: g_text_box(&scv, r.x, r.y, r.w, r.h, GF_M, "OK", col, 0); break;
    case B_PAD_BACK: g_text_box(&scv, r.x, r.y, r.w, r.h, GF_M, "Back", col, 0); break;
    case B_PAD_INC: g_fill(&scv, cx - 9, cy - 1, 18, 3, col); g_fill(&scv, cx - 1, cy - 9, 3, 18, col); break;
    case B_PAD_DEC: g_fill(&scv, cx - 9, cy - 1, 18, 3, col); break;
    default: break;
    }
}
static int btn_down(int b) { int i; for (i = 0; i < FINGERS; i++) if (fg[i].id >= 0 && fg[i].grab == G_REPEAT && fg[i].a == b) return 1; return 0; }
static int btn_live(int b)   /* a button there now: the pad's on the Pad face, play, rec and the octave on the others; no tabs */
{
    if (b >= B_TAB0 && b < B_TAB0 + F_COUNT) return 0;
    return b >= B_PAD_UP ? sf_face == F_PAD : 1;   /* (the column always; the octave's rects are set on the Keys only) */
}
static void draw_btn(const KApp *a, int b)
{
    const KRect r = sf_btn[b];
    const int tab = b >= B_TAB0 && b < B_TAB0 + F_COUNT, lit = btn_down(b);
    if (r.w <= 0) return;
    if (tab) {   /* a tab: its icon, lit and underlined while its panel is up */
        const int on = sf_open && b - B_TAB0 == sf_face;
        if (lit) g_rrect(&scv, r.x, r.y, r.w, r.h, 6, k_mix(th.bg, th.text, 10));
        sf_icon(b, r, on ? th.text : th.mid);
        if (on) g_fill(&scv, r.x + 12, r.y + r.h - 2, r.w - 24, 2, th.text);
        return;
    }
    if (b >= B_PLAY && b <= B_SAVE) {   /* the column's buttons: a dark key, its icon; play green while playing, rec red while recording */
        const int on = (b == B_PLAY && a->playing) || (b == B_REC && a->recording);
        const uint32_t bg = on ? (b == B_PLAY ? 0x2E7D3Bu : k_mix(th.bg, th.rec, 70)) : lit ? k_mix(th.bg, th.text, 24) : k_mix(th.bg, th.text, 12);
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        g_rrect(&scv, r.x, r.y, r.w, r.h, 5, bg);
        switch (b) {
        case B_PLAY: g_icon(&scv, cx - 7, cy - 7, 14, a->playing ? GI_STOP : GI_PLAY, th.text); break;   /* (what it does: stopped ▶, playing ■) */
        case B_REC: g_icon(&scv, cx - 7, cy - 7, 14, GI_RECORD, on ? th.text : th.rec); break;
        case B_PATTERN: {   /* the track's pattern, A .. D: pressed, the next */
            char t[2] = {(char)('A' + (a->pattern[a->track] & 3)), 0};
            g_text_box(&scv, r.x, r.y, r.w, r.h, GF_S, t, th.text, 0);
            break; }
        case B_MENU: g_icon(&scv, cx - 7, cy - 7, 14, GI_MENU, th.text); break;
        case B_TOOLS: g_icon(&scv, cx - 7, cy - 7, 14, GI_T_TOOLS, th.text); break;
        case B_SAVE: g_icon(&scv, cx - 7, cy - 7, 14, GI_SAVE, th.text); break;
        default: break;
        }
        return;
    }
    if (b >= B_PAD_UP) {   /* the game pad's keys: the UI's keycaps */
        g_rrect(&scv, r.x, r.y, r.w, r.h, 8, lit ? k_mix(th.key, th.text, 30) : th.key);
        g_rrect_frame(&scv, r.x, r.y, r.w, r.h, 8, th.edge, lit ? k_mix(th.key, th.text, 30) : th.key);
        sf_icon(b, r, th.ink);
        return;
    }
    sf_icon(b, r, lit ? th.text : th.mid);
}
/* a strip, as Logic Pro's: its name over a dark well; its value a line across it (lit: blue, or orange for Mod), the number over it */
static void strip(KRect r, const char *name, int v, int lo, int hi, uint32_t col, int number)
{   /* (its name inside the well, at the top) */
    const int top = r.y + 16, y = r.y + r.h - 8 - (v - lo) * (r.h - 8 - top + r.y - 6) / (hi - lo ? hi - lo : 1);
    if (r.w <= 0) return;
    g_rrect(&scv, r.x, r.y, r.w, r.h, 6, k_mix(th.bg, 0x000000, 45));
    g_rrect_frame(&scv, r.x, r.y, r.w, r.h, 6, k_mix(th.bg, th.text, 10), k_mix(th.bg, 0x000000, 45));
    g_text_box(&scv, r.x, r.y + 2, r.w, 14, GF_S, name, k_mix(th.bg, th.text, 40), 0);
    g_rrect(&scv, r.x + 5, y - 1, r.w - 10, 3, 1, col);
    if (number >= 0) {
        char t[8];
        snprintf(t, sizeof t, "%d", number);
        g_text_box(&scv, r.x, y < top + 18 ? y + 4 : y - 16, r.w, 14, GF_S, t, th.text, 0);
    }
}
#define SF_BLUE 0x3D8BFFu
#define SF_ORANGE 0xF5A04Au
static void draw_keys(const KApp *a)
{
    const int n = nwhite();
    const uint32_t hue = track_hue(a->track), white = k_mix(th.bg, th.text, 82), lab = k_mix(white, 0x000000, 45);
    int i;
    {   /* the octave over the strips: < Oct 0 > */
        char o[12];
        const KRect l = sf_btn[B_OCTDN], r = sf_btn[B_OCTUP];
        snprintf(o, sizeof o, a->octave ? "%+d" : "0", a->octave);   /* (the octave, short: the text was big) */
        g_text_box(&scv, l.x + l.w, l.y, r.x - l.x - l.w, l.h, GF_S, o, th.text, 0);
    }
    strip(vel_r, "Vel", sf_vel, 0, 127, SF_BLUE, sf_vel);
    strip(mod_r, "Mod", mod_v, 0, 127, SF_ORANGE, mod_v);
    strip(bend_r, "Bend", a->bend[a->track], -8192, 8191, SF_BLUE, -1);
    g_fill(&scv, keys_r.x - 2, keys_r.y - 2, keys_r.w + 3, keys_r.h + 4, k_mix(th.bg, 0x000000, 50));   /* (the keys' bed: square) */
    for (i = 0; i < n; i++) {   /* the white keys: light grey, a hair apart, the track's colour while held */
        const KRect r = white_r(i);
        const int note = key_base(a) + (i / 7) * 12 + WHITE[i % 7], k = typed_key(a, note);
        g_rrect(&scv, r.x, r.y, r.w, r.h, 3, held_note(note) ? k_mix(white, hue, 65) : white);
        g_fill(&scv, r.x, r.y, r.w, 4, held_note(note) ? k_mix(white, hue, 65) : white);   /* (its top square) */
        if (note % 12 == 0) {   /* every C its name at its foot (C3 at octave 0) */
            char o[6];
            snprintf(o, sizeof o, "C%d", note / 12 - 1);
            g_text_box(&scv, r.x, r.y + r.h - 16, r.w, 14, GF_S, o, lab, 0);
        }
        if (k) {   /* its computer key, over the C's name */
            char l[2] = {(char)(k >= 'a' && k <= 'z' ? k - 32 : k), 0};
            g_text_box(&scv, r.x, r.y + r.h - 34, r.w, 14, GF_S, l, lab, 0);
        }
    }
    for (i = 0; i < n; i++)
        if (BLACK_AFTER[i % 7] && i + 1 < n) {   /* the black keys: dark, a lighter rim, its computer key at its foot */
            const KRect r = black_r(i);
            const int note = key_base(a) + (i / 7) * 12 + WHITE[i % 7] + 1, k = typed_key(a, note);
            const uint32_t c = held_note(note) ? hue : 0x1C1C1E;
            g_rrect(&scv, r.x, r.y, r.w, r.h, 3, 0x050505);
            g_rrect(&scv, r.x + 1, r.y, r.w - 2, r.h - 2, 2, held_note(note) ? hue : 0x3A3A3C);
            g_rrect(&scv, r.x + 2, r.y, r.w - 4, r.h - 5, 2, c);
            g_fill(&scv, r.x, r.y, r.w, 3, 0x050505); g_fill(&scv, r.x + 1, r.y, r.w - 2, 3, held_note(note) ? hue : 0x3A3A3C);
            g_fill(&scv, r.x + 2, r.y, r.w - 4, 3, c);   /* (the top square) */
            if (k) {
                char l[2] = {(char)(k >= 'a' && k <= 'z' ? k - 32 : k), 0};
                g_text_box(&scv, r.x, r.y + r.h - 20, r.w, 14, GF_S, l, 0x9A9A9E, 0);
            }
        }
}
static void draw_pads(const KApp *a)
{
    const uint32_t hue = track_hue(a->track);
    int l;
    strip(vel_r, "Vel", sf_vel, 0, 127, SF_BLUE, sf_vel);
    for (l = 0; l < K_LANES; l++) {
        const KRect r = pad_r(l);
        const int piece = a->lane_p[a->track][l][0], here = l == a->cur_lane[a->track];
        g_rrect(&scv, r.x, r.y, r.w, r.h, 8, held_lane(l) ? hue : here ? k_mix(th.bg, th.text, 20) : k_mix(th.bg, th.text, 13));   /* (as Logic's pads: grey) */
        g_text_box(&scv, r.x + 8, r.y + 4, r.w - 12, 16, GF_S, k_audio_drum_piece(piece), held_lane(l) ? th.bg : here ? th.text : th.mid, -1);
    }
}
static void draw_macros(KApp *a)
{
    int i;
    for (i = 0; i < 4; i++) {
        const KRect r = macro_r(i);
        const KParamRef ref = k_param_ref(a, K_PAGE_HOME, i);
        KParamProps p;
        int fill = 0;
        if (k_param_props(a, ref, &p) && p.hi > p.lo) fill = (r.w - 8) * (k_param_get(a, K_PAGE_HOME, i) - p.lo) / (p.hi - p.lo);
        g_rrect(&scv, r.x, r.y, r.w, r.h, 8, k_mix(th.bg, th.text, 8));
        if (fill > 0) g_rrect(&scv, r.x + 4, r.y + 4, fill, r.h - 8, 6, k_mix(th.bg, track_hue(a->track), 40));
        g_text_box(&scv, r.x + 14, r.y, r.w - 28, r.h, GF_M, ui_item_name(a, K_PAGE_HOME, i), th.text, -1);
    }
}
/* the frame: the page (drawn by ui_draw) copied into its box, the rails and the surface beside it */
static void slide(int open, uint32_t now);
/* the page's pixels into shown_r: as they are when it has all its room, else smaller (bilinear; two channels at a time) */
static void page_blit(const GCanvas *pg)
{
    const int s = scv.s > 1 ? scv.s : 1, ox = shown_r.x * s, dw = imin(shown_r.w * s, scv.pw - ox), dh = imin(shown_r.h * s, scv.ph);
    static int *xi;
    static int xi_n;
    int x, y;
    if (dw <= 0 || dh <= 0) return;
    if (dw == pg->pw && dh >= pg->ph) {
        for (y = 0; y < pg->ph; y++) memcpy(&scv.px[(size_t)y * scv.pw + ox], &pg->px[(size_t)y * pg->pw], (size_t)dw * 4);
        return;
    }
    if (xi_n < dw) { free(xi); xi = malloc(sizeof *xi * (size_t)dw); xi_n = xi ? dw : 0; if (!xi) return; }
    for (x = 0; x < dw; x++) {   /* (each column's source, Q8: its left pixel and the weight of the right one) */
        int f = (int)(((int64_t)(2 * x + 1) * pg->pw * 256) / (2 * dw)) - 128;
        if (f < 0) f = 0;
        if (f > (pg->pw - 1) * 256) f = (pg->pw - 1) * 256;
        xi[x] = f;
    }
    for (y = 0; y < dh; y++) {
        int fy = (int)(((int64_t)(2 * y + 1) * pg->ph * 256) / (2 * dh)) - 128, y0, wy;
        const uint32_t *r0, *r1;
        uint32_t *d = &scv.px[(size_t)y * scv.pw + ox];
        if (fy < 0) fy = 0;
        if (fy > (pg->ph - 1) * 256) fy = (pg->ph - 1) * 256;
        y0 = fy >> 8; wy = fy & 255;
        r0 = &pg->px[(size_t)y0 * pg->pw]; r1 = y0 + 1 < pg->ph ? r0 + pg->pw : r0;
        for (x = 0; x < dw; x++) {
            const int x0 = xi[x] >> 8, wx = xi[x] & 255, x1 = x0 + 1 < pg->pw ? x0 + 1 : x0;
            const uint32_t a = r0[x0], b = r0[x1], c = r1[x0], e = r1[x1];
            const uint32_t rb0 = (((a & 0xFF00FFu) * (uint32_t)(256 - wx) + (b & 0xFF00FFu) * (uint32_t)wx) >> 8) & 0xFF00FFu;
            const uint32_t g0 = ((((a >> 8) & 0xFF00FFu) * (uint32_t)(256 - wx) + ((b >> 8) & 0xFF00FFu) * (uint32_t)wx) >> 8) & 0xFF00FFu;
            const uint32_t rb1 = (((c & 0xFF00FFu) * (uint32_t)(256 - wx) + (e & 0xFF00FFu) * (uint32_t)wx) >> 8) & 0xFF00FFu;
            const uint32_t g1 = ((((c >> 8) & 0xFF00FFu) * (uint32_t)(256 - wx) + ((e >> 8) & 0xFF00FFu) * (uint32_t)wx) >> 8) & 0xFF00FFu;
            const uint32_t rb = ((rb0 * (uint32_t)(256 - wy) + rb1 * (uint32_t)wy) >> 8) & 0xFF00FFu;
            const uint32_t g = ((g0 * (uint32_t)(256 - wy) + g1 * (uint32_t)wy) >> 8) & 0xFF00FFu;
            d[x] = rb | (g << 8);
        }
    }
}
const GCanvas *ui_surface_frame(KApp *a, uint32_t now)
{
    const GCanvas *pg = ui_draw_canvas();
    int i, y;
    if (!scv.px) return pg;
    if (a->track != last_track) {   /* another track: its kind's face (a drum track Pads, a synth Keys), unless the pad is up */
        if (!auto_pad) sf_face = k_is_drum(a, a->track) ? F_PADS : F_KEYS;   /* (a drum track the pads, any other the keys) */
        last_track = a->track;
        sf_place();
    }
    {   const int rows = a->rows_view && (a->page == K_PAGE_ROLL || a->page == K_PAGE_STEP || a->page == K_PAGE_DRUM);
        if (rows && !auto_pad) {   /* the tracker view: the pad up (the panel before kept) */
            pad_face_before = sf_face; pad_was_open = sf_open; auto_pad = 1;
            sf_face = F_PAD;
            if (sf_open) sf_place(); else slide(1, now);
        } else if (!rows && auto_pad) {   /* closed: the panel before back, or down */
            auto_pad = 0; sf_face = pad_face_before;
            if (pad_was_open) sf_place(); else slide(0, now);
        } }
    for (i = 0; i < FINGERS; i++)   /* a held arrow or value: again after 400 ms, then each 80 ms */
        if (fg[i].id >= 0 && fg[i].grab == G_REPEAT && fg[i].a >= B_PAD_UP && (int32_t)(now - fg[i].next) >= 0) {
            static const KActionType T[] = {K_ACT_UP, K_ACT_DOWN, K_ACT_LEFT, K_ACT_RIGHT};
            const int b = fg[i].a;
            if (b <= B_PAD_RIGHT) k_dispatch(a, (KAction){T[b - B_PAD_UP], 0, 0, now});
            else if (b == B_PAD_INC || b == B_PAD_DEC) k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, b == B_PAD_INC ? 1 : -1, 0, now});
            fg[i].next = now + 80u;
        }
    if (slide_ms) {   /* the slide: eased; up and settled, the page is laid out above it */
        const uint32_t t = now - slide_t0;
        if (t >= slide_ms) { slide_ms = 0; if (sf_open) sf_up = 1; }
    }
    {   /* the panel's height now (sliding: eased out) */
        int hnow = sf_open ? panel_h : 0;
        if (slide_ms) {
            const int t = (int)(now - slide_t0), q = t * 256 / (int)slide_ms, e = 256 - (256 - q) * (256 - q) / 256;
            hnow = slide_from + (slide_to - slide_from) * e / 256;
        }
        g_fill(&scv, 0, 0, SW_, SH_, th.bg);
        shown_r = page_box;   /* (the page in its room above the panel, as laid out: no letterbox) */
        page_blit(pg);
        if (hnow > 0) {   /* the panel, as far up as it has come */
            const int py = bar_r.y - hnow;
            g_fill(&scv, 0, py, SW_, hnow, th.bg);
            g_fill(&scv, 0, py, SW_, 1, k_mix(th.bg, th.text, 14));
            if (!slide_ms && sf_open) {   /* (its parts once it is up) */
                if (sf_face == F_KEYS) draw_keys(a);
                else if (sf_face == F_PADS) draw_pads(a);
                else if (sf_face == F_MACROS) draw_macros(a);
            }
        }
    }
    for (i = 0; i < B_COUNT; i++) if (!slide_ms && btn_live(i)) draw_btn(a, i);
    return &scv;
}

/* ------------------------------------------------------------------------------------------------------ fingers --- */
static int sf_finger(int id, int make)
{
    int i;
    if (!fg_ready) { for (i = 0; i < FINGERS; i++) { fg[i].id = -1; fg[i].grab = G_NONE; } fg_ready = 1; }
    for (i = 0; i < FINGERS; i++) if (fg[i].id == id && fg[i].grab != G_NONE) return i;
    if (!make) return -1;
    for (i = 0; i < FINGERS; i++) if (fg[i].grab == G_NONE) { fg[i].id = id; return i; }
    return -1;
}
static void sf_release(KApp *a, int i, uint32_t ms)
{
    switch (fg[i].grab) {
    case G_KEY: k_note_in(a, fg[i].a, 0); break;
    case G_LANE: k_lane_hit(a, fg[i].a, 0); break;
    case G_BEND: k_bend_in(a, 8192); break;   /* (back to the middle) */
    case G_PAGE: ui_touch(a, 2, fg[i].x0, fg[i].y0, ms); page_finger = -1; break;
    default: break;
    }
    fg[i].grab = G_NONE; fg[i].id = -1;
}
static void slide(int open, uint32_t now);
static void press_button(KApp *a, int b, uint32_t ms)
{
    switch (b) {
    case B_PLAY: k_dispatch(a, (KAction){K_ACT_PLAY, 0, 0, ms}); break;
    case B_REC: k_dispatch(a, (KAction){K_ACT_REC, 0, 0, ms}); break;
    case B_PATTERN: k_pattern_set(a, a->track, (a->pattern[a->track] + 1) & 3); break;   /* (the track in hand's next pattern, A after D) */
    case B_MENU: k_dispatch(a, (KAction){K_ACT_PAGE_MENU, 0, 0, ms}); break;
    case B_TOOLS: k_dispatch(a, (KAction){K_ACT_TOOLS, 0, 0, ms}); break;
    case B_SAVE: k_dispatch(a, (KAction){K_ACT_SAVE, 0, 0, ms}); break;
    case B_OCTDN: k_dispatch(a, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, ms}); break;
    case B_OCTUP: k_dispatch(a, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, ms}); break;
    case B_PAD_UP: k_dispatch(a, (KAction){K_ACT_UP, 0, 0, ms}); break;
    case B_PAD_DOWN: k_dispatch(a, (KAction){K_ACT_DOWN, 0, 0, ms}); break;
    case B_PAD_LEFT: k_dispatch(a, (KAction){K_ACT_LEFT, 0, 0, ms}); break;
    case B_PAD_RIGHT: k_dispatch(a, (KAction){K_ACT_RIGHT, 0, 0, ms}); break;
    case B_PAD_OK: k_dispatch(a, (KAction){K_ACT_ACCEPT, 0, 0, ms}); break;
    case B_PAD_BACK: k_dispatch(a, (KAction){K_ACT_BACK, 0, 0, ms}); break;
    case B_PAD_INC: k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, 1, 0, ms}); break;
    case B_PAD_DEC: k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, -1, 0, ms}); break;
    default:
        if (b >= B_TAB0 && b < B_TAB0 + F_COUNT) {   /* a tab: its panel up; again (it is up): down */
            const int f = b - B_TAB0;
            auto_pad = 0;
            if (sf_open && sf_face == f) slide(0, ms);
            else { sf_face = f; if (sf_open) sf_place(); else slide(1, ms); }
        }
        break;
    }
}
/* a finger: id the platform's, phase 0 down, 1 moved, 2 up; x, y in the screen's units */
/* a point on the glass in the page's own units (the page shown smaller while a panel is up) */
static void page_xy(int x, int y, int *px, int *py)
{
    *px = shown_r.w > 0 ? (x - shown_r.x) * page_box.w / shown_r.w : x;
    *py = shown_r.h > 0 ? (y - shown_r.y) * page_box.h / shown_r.h : y;
}
void ui_finger(KApp *a, int id, int phase, int x, int y, uint32_t ms)
{
    int i, b;
    if (phase == 0) {
        if ((i = sf_finger(id, 1)) < 0) return;
        fg[i].x0 = x; fg[i].y0 = y;
        for (b = 0; b < B_COUNT; b++)
            if (btn_live(b) && sf_in(sf_btn[b], x, y)) {
                fg[i].grab = G_REPEAT; fg[i].a = b; fg[i].next = ms + 400u;
                press_button(a, b, ms);
                return;
            }
        if (sf_up && sf_in(panel_r, x, y)) {
            int vel, n;
            if (sf_in(vel_r, x, y)) { fg[i].grab = G_VEL; fg[i].v0 = sf_vel; return; }
            if (sf_face == F_KEYS && sf_in(bend_r, x, y)) { fg[i].grab = G_BEND; return; }
            if (sf_face == F_KEYS && sf_in(mod_r, x, y)) { fg[i].grab = G_MOD; fg[i].v0 = mod_v; return; }
            if (sf_face == F_KEYS && (n = key_at(a, x, y, &vel)) >= 0) {
                fg[i].grab = G_KEY; fg[i].a = n;
                k_note_in(a, n, vel);
                return;
            }
            if (sf_face == F_PADS)
                for (n = 0; n < K_LANES; n++)
                    if (sf_in(pad_r(n), x, y)) {
                        const KRect r = pad_r(n);
                        fg[i].grab = G_LANE; fg[i].a = n;
                        k_lane_hit(a, n, sf_desk ? sf_vel : 40 + (y - r.y) * 87 / (r.h ? r.h : 1));
                        return;
                    }
            if (sf_face == F_MACROS)
                for (n = 0; n < 4; n++)
                    if (sf_in(macro_r(n), x, y)) { fg[i].grab = G_MACRO; fg[i].a = n; fg[i].v0 = k_param_get(a, K_PAGE_HOME, n); return; }
            fg[i].grab = G_NONE; fg[i].id = -1;   /* (the surface's ground: nothing) */
            return;
        }
        if (sf_in(shown_r, x, y) && page_finger < 0) {
            fg[i].grab = G_PAGE; page_finger = id;
            page_xy(x, y, &fg[i].x0, &fg[i].y0);
            ui_touch(a, 0, fg[i].x0, fg[i].y0, ms);
            return;
        }
        fg[i].grab = G_NONE; fg[i].id = -1;
        return;
    }
    if ((i = sf_finger(id, 0)) < 0) return;
    if (phase == 2) { sf_release(a, i, ms); return; }
    switch (fg[i].grab) {
    case G_PAGE:
        page_xy(x, y, &fg[i].x0, &fg[i].y0);
        ui_touch(a, 1, fg[i].x0, fg[i].y0, ms);
        break;
    case G_KEY: {   /* glissando: onto the next key, the one before let go */
        int vel, n = sf_in(keys_r, x, y) ? key_at(a, x, y, &vel) : -1;
        if (n >= 0 && n != fg[i].a) { const int was = fg[i].a; fg[i].a = n; if (!held_note(was)) k_note_in(a, was, 0); k_note_in(a, n, vel); }
        break; }
    case G_BEND: {   /* from where it was touched: half the strip is two semitones */
        const int half = bend_r.h / 2 > 8 ? bend_r.h / 2 : 8;
        k_bend_in(a, 8192 + (fg[i].y0 - y) * 8191 / half);
        break; }
    case G_VEL: {
        const int v = fg[i].v0 + (fg[i].y0 - y) * 127 / (vel_r.h > 8 ? vel_r.h : 8);
        sf_vel = v < 1 ? 1 : v > 127 ? 127 : v;
        break; }
    case G_MOD: {
        const int v = fg[i].v0 + (fg[i].y0 - y) * 127 / (mod_r.h > 8 ? mod_r.h : 8);
        mod_v = v < 0 ? 0 : v > 127 ? 127 : v;
        k_mod_in(a, mod_v);
        break; }
    case G_MACRO: {   /* along the strip: its whole width is the macro's range */
        const KParamRef ref = k_param_ref(a, K_PAGE_HOME, fg[i].a);
        KParamProps p;
        const KRect r = macro_r(fg[i].a);
        if (k_param_props(a, ref, &p)) k_ref_set(a, ref, fg[i].v0 + (x - fg[i].x0) * (p.hi - p.lo) / (r.w > 8 ? r.w : 8));
        break; }
    default: break;
    }
}
#else
int ui_surface_velocity(void) { return 100; }
void ui_surface_velocity_delta(int d) { (void)d; }
void ui_surface_set_desktop(int on) { (void)on; }
void ui_surface_key_held(int note, int on) { (void)note; (void)on; }
void ui_surface_layout(int w, int h, int *pw, int *ph) { *pw = w; *ph = h; }
int ui_surface_relayout(void) { return 0; }
#endif
