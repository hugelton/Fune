/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_GFX_H
#define KREISEL_GFX_H
#include <stdint.h>
#include "theme.h"

/* A software canvas of 0xAARRGGBB pixels, drawn with the same baked fonts as a small device would use. w, h: its size as the UI
 * lays it out; s > 1 (a desktop's high-density screen): its pixels are s x s a unit, pw x ph of them, and every g_* call draws
 * at that density (shapes, text, icons and curves sharp, not enlarged). s 0 or 1: a pixel a unit (pw, ph unused). */
/* a pixel: 0xAARRGGBB, or (KREISEL_PX16: the FM-1, whose panel is RGB565 anyway) RGB565, half the memory. GPIX packs a colour,
 * GPIX_RGB unpacks one to 0xRRGGBB (the bits repeated down: white stays white) */
#ifdef KREISEL_PX16
typedef uint16_t gpix_t;
#define GPIX(c) ((gpix_t)((((uint32_t)(c) >> 8) & 0xF800u) | (((uint32_t)(c) >> 5) & 0x07E0u) | (((uint32_t)(c) >> 3) & 0x001Fu)))
#define GPIX_RGB(p) (((((uint32_t)(p)) & 0xF800u) << 8) | ((((uint32_t)(p)) & 0xE000u) << 3) | ((((uint32_t)(p)) & 0x07E0u) << 5) | \
                     ((((uint32_t)(p)) & 0x0600u) >> 1) | ((((uint32_t)(p)) & 0x001Fu) << 3) | ((((uint32_t)(p)) & 0x001Cu) >> 2))
#else
typedef uint32_t gpix_t;
#define GPIX(c) ((gpix_t)(0xFF000000u | (uint32_t)(c)))
#define GPIX_RGB(p) ((uint32_t)(p) & 0xFFFFFFu)
#endif
typedef struct { int w, h; gpix_t *px; int s, pw, ph; } GCanvas;
int g_init_scaled(GCanvas *c, int w, int h, int s);   /* w x h units at s pixels a unit (g_free frees it) */
typedef enum { GF_S, GF_M, GF_L } GFont;   /* 12 px labels, 15 px values, 28 px big readout */

int g_init(GCanvas *c, int w, int h);
void g_free(GCanvas *c);
void g_clear(GCanvas *c, uint32_t col);
void g_fill(GCanvas *c, int x, int y, int w, int h, uint32_t col);
uint32_t g_mid(uint32_t a, uint32_t b);   /* the colour a flat gradient (gradients off) draws */
void g_fill_around(GCanvas *c, int x, int y, int w, int h, int hx, int hy, int hw, int hh, uint32_t col);   /* all but the hole */
void g_rrect(GCanvas *c, int x, int y, int w, int h, int r, uint32_t col);
void g_rrect_frame(GCanvas *c, int x, int y, int w, int h, int r, uint32_t edge, uint32_t fill);   /* in a 1 px border: one call, no overdraw */
void g_rrect_grad(GCanvas *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom);   /* a vertical gradient */
void g_rrect_grad_a(GCanvas *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bottom, int alpha);   /* gradient at alpha */
void g_rrect_a(GCanvas *c, int x, int y, int w, int h, int r, uint32_t col, int alpha);
void g_rrect_a_under(GCanvas *c, int x, int y, int w, int h, int r, uint32_t col, int alpha, int ux, int uy, int uw, int uh, int ur);   /* a shadow under an opaque rounded rectangle drawn next */   /* alpha 0..255 */
/* a soft shadow of a rounded rectangle, offset (dx, dy) and `blur` px wider at its edge; pct is its strength at the core */
void g_shadow(GCanvas *c, int x, int y, int w, int h, int r, int dx, int dy, int blur, uint32_t col, int pct);
/* Depth helpers (Logic-like): integer only, opaque gradients cost a flat fill; the lines and shadow rows blend. */
int g_pix5(GCanvas *c, int x, int y, const char *s, uint32_t col, int scale);   /* the 5 x 7 pixel font (pixfont.c), a cell 6 x 8 a scale; returns end x */
int g_pix5_w(const char *s, int scale);
int g_pix8(GCanvas *c, int x, int y, const char *s, uint32_t col, int scale);   /* the 8 x 8 pixel font (pixfont8.c: Press Start 2P, OFL), capitals; returns end x */
int g_pix8_w(const char *s, int scale);
void g_gradients(int on);   /* 0 (the default): gradients are drawn flat, in their middle colour */
void g_vgrad(GCanvas *c, int x, int y, int w, int h, uint32_t top, uint32_t bot);   /* a vertical gradient, square */
void g_hline_a(GCanvas *c, int x, int y, int w, uint32_t col, int alpha);            /* a 1 px line at alpha 0..255 */
void g_drop(GCanvas *c, int x, int y, int w, int h, int inset, int rows, int pct);    /* a soft shadow: rows fading lines under a part */
void g_bevel(GCanvas *c, int x, int y, int w, int h, int r, int hi, int lo);          /* a light top line and a dark bottom line (alphas) */
void g_panel(GCanvas *c, int x, int y, int w, int h, int r, uint32_t face, int shadow);   /* raised: outline, lit face, bevel, shadow (pct, 0: none) */
void g_sunken(GCanvas *c, int x, int y, int w, int h, int r, uint32_t base);          /* a recess, darker than base, shaded at the top */
void g_colored(GCanvas *c, int x, int y, int w, int h, int r, uint32_t hue);          /* a colour part: lighter top, highlight, dark foot */
void g_knob_face(GCanvas *c, int cx, int cy, int r, uint32_t face);                   /* a metal disc inside g_knob's ring of radius r */
void g_font_vector(int on);   /* 1: filled from outlines at any size (default); 0: the baked bitmaps */
int g_text(GCanvas *c, int x, int y, GFont f, const char *s, uint32_t col);        /* y: top of the line box; returns end x */
int g_text_w(GFont f, const char *s);
int g_text_h(GFont f);
/* Icons: Fukiai (assets/icons, MIT) as its Multicolor draws them: a background face and the primary line work, each in its
 * own colour (tools/gen_icons.py bakes the outlines). g_icon draws the line work alone, in one colour. */
typedef enum { GI_PLAY, GI_STOP, GI_PAGE_PATTERN, GI_PAGE_SONG, GI_PAGE_SOUND, GI_PAGE_ENV, GI_PAGE_FX,
               GI_BATTERY_0, GI_BATTERY_1, GI_BATTERY_2, GI_BATTERY_3, GI_BATTERY_4, GI_BATTERY_CHARGING, GI_RECORD, GI_PAGE_MIXER, GI_PAGE_BUS,
               GI_ENG_ANALOG, GI_ENG_LOFI, GI_ENG_NOISE, GI_ENG_PHASE, GI_ENG_TRIO, GI_ENG_WHEEL,   /* (the engines' order) */
               GI_P_NOISE, GI_P_ENV, GI_P_BITS, GI_P_FOLD, GI_P_GENERIC,
               GI_P_ATTACK, GI_P_DECAY, GI_P_SUSTAIN, GI_P_RELEASE, GI_P_CUTOFF, GI_P_TONE, GI_P_DRIVE, GI_P_LEVEL, GI_P_WAVE,
               GI_P_TUNE, GI_P_TIME, GI_P_HOLD, GI_P_CURVE, GI_P_AMOUNT, GI_P_PITCH, GI_P_SHAPE, GI_P_DIST, GI_P_CHORUS,
               GI_P_ECHO, GI_P_REVERB, GI_P_PAN, GI_P_SIZE, GI_P_FEEDBACK, GI_P_RETURN, GI_P_FADE, GI_P_OCTAVE, GI_P_GATE, GI_P_QUANT, GI_P_TRANSPOSE, GI_P_TEMPO, GI_P_SWING,
               GI_PAGE_LFO, GI_PAGE_ARP, GI_PAGE_SCL, GI_PAGE_CLOCK, GI_P_MIX, GI_P_DETUNE, GI_P_SUB, GI_P_RESO, GI_P_KEYTRACK, GI_PAGE_DRUM, GI_P_LEN, GI_P_VEL, GI_SONG,
               GI_T_FILL, GI_T_EUCLID, GI_T_RANDOM, GI_T_SHIFT, GI_T_HUMANIZE, GI_T_REVERSE, GI_T_COPY, GI_T_PASTE, GI_T_CLEAR, GI_T_UNDO,   /* the sequencer's tools */
               GI_T_INSERT, GI_T_DELETE, GI_T_SWAP, GI_T_INIT, GI_T_PAN, GI_T_UNMUTE, GI_T_TOOLS,   /* the other pages' tools, the header's TOOLS */
               GI_CONFIG,   /* the page switcher's Config (the settings) */
               GI_CARET_L, GI_CARET_R,   /* the engine box's < > (a desktop) */
               GI_LIBRARY,   /* the desktop header's presets */
               GI_FX_FILTER, GI_FX_WAH, GI_FX_PHASER, GI_FX_TREMOLO, GI_FX_WOBBLE, GI_FX_AMP,   /* the inserts' (fx_icon) */
               GI_SAVE, GI_MENU,   /* the touch surface's column */
               GI_COUNT } GIcon;   /* GI_SONG: the song playing (the header), Fukiai's symbol_disc */
/* a raised part: a soft shadow down and right, a rim of light along the top, a face a little lighter at the top */
void g_raised(GCanvas *c, const KTheme *t, int x, int y, int w, int h, int r, uint32_t face, int lift);
/* a smooth curve through n points (x, y pairs in 1/16 px, x rising), anti-aliased by distance; th16 = thick / 2 + 1/2 in 1/16 px
 * (1 px: 16, 1.2 px: 17, 2 px: 24). Integers only, on every device: one formula for all (the FM-1 has no FPU) */
void g_curve16(GCanvas *c, const int *p16, int n, int th16, uint32_t col);
int g_isin(uint32_t phase);   /* sin of a phase (2^32 a turn), Q14 */
uint32_t g_exp2(int32_t x24, int q);   /* 2^(x24 / 2^24) * 2^q, rounded (the integers' powf and expf) */
uint32_t g_isqrt(uint32_t v);   /* the integer square root */
int32_t g_log2(uint32_t x16);   /* log2(x16 / 2^16) in Q24 */
/* the area under such a curve (its points in 1/16 px), down to y = base */
void g_curve_fill16(GCanvas *c, const int *p16, int n, int base, uint32_t col);
void g_line(GCanvas *c, int x0, int y0, int x1, int y1, int thickness, uint32_t col);
/* A knob with a light gauge: a ring (270 degrees, from down-left round the top to down-right) with the arc to the value v
 * (0..100) lit, and a mark at the end of it. bipolar: the arc runs from the top (a pan); else from the start. A shared
 * part: the mixer uses it, and any page that wants a knob's face. */
void g_knob(GCanvas *c, int cx, int cy, int r, int v, int bipolar, uint32_t arc, uint32_t ring, uint32_t mark);
void g_icon(GCanvas *c, int x, int y, int size, GIcon id, uint32_t col);
void g_icon2(GCanvas *c, int x, int y, int size, GIcon id, uint32_t bg, uint32_t fg);   /* the face, then the line work */
void g_text_box(GCanvas *c, int x, int y, int w, int h, GFont f, const char *s, uint32_t col, int align);   /* -1 left, 0 centre, 1 right; centred vertically */
#endif
