/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A plugin's screens and what the host lends it (docs/PLUGIN_SDK.md: Screens). Included by kreisel_plugin.h.
 *
 * A plugin lays out its own SOUND and EDIT pages: a table of the host's widgets on a grid, a table for each kind of screen
 * (KLayout). The host draws every widget, in the theme, on every device; values still change only through the host (its
 * knobs, touches, keys: the core's commands). A widget may be a picture: there the plugin draws, through the integer calls the
 * host lends it (KDraw), reading values, never writing them. No table for a screen: the host's vanilla view.
 *
 * The plugin is told the device it runs on as what it has (KHostApi: the room, the screen's kind and size, the inputs), never
 * as a device's name. Everything here is const data or a call: nothing for the host to allocate. */
#ifndef KREISEL_PLUGIN_UI_H
#define KREISEL_PLUGIN_UI_H
#include <stdint.h>

#define K_HOST_API_VERSION 1
#define K_PLUGIN_EXT_VERSION 1

/* The kinds of screen (a layout says which it is for: a mask). SQUARE: the FM-1's 240 x 240. SMALL: wide and short, about 320 x
 * 170 .. 200 (the Cardputer Zero, the H700 handhelds). LARGE: a desktop's window, a tablet. */
enum { K_SCR_SQUARE = 1, K_SCR_SMALL = 2, K_SCR_LARGE = 4, K_SCR_ANY = 7 };
/* The kinds of input a device has (KProfile.input, KHostApi.input). KEYS: a music key bed; KEYBOARD: a text keyboard */
enum { K_IN_KNOBS = 1, K_IN_KEYS = 2, K_IN_PAD = 4, K_IN_TOUCH = 8, K_IN_KEYBOARD = 16, K_IN_MOUSE = 32 };

/* What the host says of itself. The plugin may keep the pointer (attach): the host keeps it current (the screen is known once the
 * UI starts). Grows at its end only; `size` says how much there is. */
typedef struct KHostApi {
    uint16_t size, version;          /* sizeof(KHostApi), K_HOST_API_VERSION */
    uint32_t fs;                     /* the sample rate */
    uint32_t ram;                    /* bytes plugins may keep, together (0: not limited) */
    uint16_t cost;                   /* microseconds a track may take a block (0: not limited) */
    uint16_t voices;                 /* voices a track plays */
    uint16_t has;                    /* K_NEED_*: what the device has (K_NEED_FLOAT: a fast FPU) */
    uint8_t screen;                  /* K_SCR_* of its screen (0: none, or not known yet) */
    uint8_t knobs;                   /* knobs it has (0: none; the FM-1: 4) */
    uint16_t screen_w, screen_h;     /* the screen in the UI's units */
    uint16_t input;                  /* K_IN_* */
} KHostApi;

/* The widgets. `param` is an index into the descriptor's edit[] (a picture's: its own number, given to draw). A widget bound to
 * a parameter of the other page is drawn but not taken. A host older than a widget skips it (the grid stays). On an effect's
 * layout (K_LPAGE_FX) `param` may also be K_WP_MIX: the insert's MIX, which is the host's, not the effect's. */
enum {
    K_W_KNOB = 1,      /* a knob: label, value, a ring with the value's arc */
    K_W_SLIDER,        /* a slider: upright when its cell is taller than wide, else lying */
    K_W_SWITCH,        /* on / off (a KF_ONOFF parameter; any other: off at its minimum) */
    K_W_VALUE,         /* the value as text, no control drawn (still taken by the knobs, a touch) */
    K_W_LABEL,         /* `text`, nothing bound (a section's name) */
    K_W_ENV,           /* an envelope's picture from four parameters, param .. param + 3: attack, decay, sustain, release */
    K_W_SCOPE,         /* the track's sound as the host hears it (after the plugin, before the fader) */
    K_W_PICTURE,       /* the plugin's own drawing: KPluginExt.draw */
    K_W_COUNT
};
/* How a widget is drawn (bits) */
enum {
    K_ST_FLAT = 1,      /* flat: no rings, no handles, no rounding; a bar's fill is the value */
    K_ST_FRAME = 2,     /* a frame round the cell */
    K_ST_NOLABEL = 4,   /* no label */
    K_ST_NOVALUE = 8,   /* no value text */
    K_ST_ACCENT = 16,   /* a label in the accent; a scope's line in the accent always */
    K_ST_LARGE = 32     /* the larger font (a label, a value) */
};
typedef struct {
    uint8_t kind, style;            /* K_W_*, K_ST_* */
    int8_t param;                   /* see above; -1 none */
    uint8_t x, y, w, h;             /* in cells of the layout's grid, from its top left */
    const char *text;               /* a label's text, or another label for a bound widget (0: the parameter's own) */
} KWidget;
/* The pages a layout is for. SOUND, EDIT: an engine's (a synth's, a drum's). FX: an effect's, drawn in the FX page in the place of
 * the insert that holds it (its name and the < > that change it stay the host's); only an effect may have it, and an effect has
 * no other. DRUM: a drum's SOUND page, where the lane in hand is played and named (taken there before a SOUND layout); only a
 * drum (K_PLUG_DRUM, or a synth whose descriptor says drum) may have it. */
enum { K_LPAGE_SOUND, K_LPAGE_EDIT, K_LPAGE_FX, K_LPAGE_DRUM, K_LPAGE_COUNT };
#define K_WP_MIX (-2)               /* (K_LPAGE_FX) a widget bound to the insert's MIX */
typedef struct {
    uint8_t screens;                /* K_SCR_*: the kinds of screen it is for (the first that matches is taken) */
    uint8_t page;                   /* K_LPAGE_* */
    uint8_t cols, rows;             /* the grid (1 .. 16 each) over the page under its preset's line */
    uint8_t n;                      /* widgets */
    const KWidget *w;
} KLayout;

/* The colours a picture may use: the theme's */
enum { K_COL_BG, K_COL_FAINT, K_COL_LINE, K_COL_MID, K_COL_TEXT, K_COL_ACCENT, K_COL_ALERT, K_COL_COUNT };
/* What a picture draws with: its frame from (0, 0) to (w - 1, h - 1), clipped; integers only. A call past the frame's budget (a few
 * hundred a frame) does nothing: a picture can slow a frame, never stop it. */
typedef struct KDraw {
    uint16_t size, version;
    int16_t w, h;                   /* the frame */
    uint8_t screen;                 /* K_SCR_* */
    uint8_t lit;                    /* the knobs hold the parameters it shows (1), or another row (0) */
    int16_t text_h;                 /* the small font's line */
    void (*fill)(const struct KDraw *g, int x, int y, int w, int h, int col);
    void (*line)(const struct KDraw *g, int x0, int y0, int x1, int y1, int col);
    int (*text)(const struct KDraw *g, int x, int y, const char *s, int col);   /* the small font, y its top; returns its end x */
    int (*text_w)(const struct KDraw *g, const char *s);
    int (*scope)(const struct KDraw *g, int8_t *dst, int n);   /* the track's sound (K_W_SCOPE's), n points at most: how many */
    void *host;                     /* (the host's) */
} KDraw;

/* What a plugin adds beyond its sound (KPlugin.ext). Grows at its end only. */
typedef struct KPluginExt {
    uint16_t size, version;         /* sizeof(KPluginExt), K_PLUGIN_EXT_VERSION */
    void (*attach)(const KHostApi *host);   /* at registration: what the device is (0: not wanted) */
    const KLayout *layouts;         /* its screens (0: the vanilla view everywhere) */
    uint8_t nlayouts;
    /* a picture (K_W_PICTURE): draw picture `id` from the values (the descriptor's edit[], as stored; n of them) */
    void (*draw)(const KDraw *g, int id, const int16_t *values, int n);
} KPluginExt;

const KHostApi *k_host_api(void);                      /* the host's (always the same pointer) */
void k_host_set_screen(int w, int h, unsigned input, int knobs);   /* the UI, at its start: the screen and the inputs */
int k_screen_kind(int w, int h);                       /* K_SCR_* of a w x h screen */
/* the layout a plugin has for a page on a kind of screen, or 0 (then the vanilla view) */
const KLayout *k_plugin_layout(int engine, int page, int screen);
/* the layout an effect (its type, k_effect_at's) has for the FX page on a kind of screen, or 0 (the host's view of the insert) */
const KLayout *k_effect_layout(int type, int screen);
#endif
