/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "theme.h"
#include "kreisel_engine.h"  /* K_UID: map identities, not catalogue indices */
#include <math.h>

uint32_t k_mix(uint32_t a, uint32_t b, int pct)
{
    uint32_t out = 0;
    int sh;
    for (sh = 16; sh >= 0; sh -= 8) {
        int x = (int)((a >> sh) & 255u), y = (int)((b >> sh) & 255u);
        int d = (y - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << sh;
    }
    return out;
}

static void derive(KTheme *t, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t primary, uint32_t secondary, uint32_t ok)
{
    t->ok = ok;
    t->bg = c1; t->text = c3; t->theme = c2; t->accent = secondary; t->rec = primary; t->ink = c1;
    t->surf = k_mix(c1, c2, 12);
    t->mid = k_mix(c1, c3, 78);
    t->dim = k_mix(c1, c3, 54);
    t->line = k_mix(c1, c3, 26);
    t->sel = k_mix(c1, c2, 80);
    t->tint = k_mix(c1, c2, 12);
    t->raise = k_mix(t->surf, c3, 12);
    t->key = k_mix(c1, c3, 78);
    t->edge = k_mix(c1, c3, 30);
    t->shadow = 0x000000;
    t->shadow_pct = 0;     /* flat, as Felucca 2.0: a panel is an outline, not a lift (set a value for a ledge shadow) */
    t->shadow_blur = 0; t->shadow_dy = 2;
    t->rim_pct = 0; t->top_pct = 0; t->bot_pct = 0;
}

void k_theme(KTheme *t, KThemeId id)
{
    (void)id;
    derive(t, 0x010101, 0xB4B4B4, 0xF5F5F7, 0xFF453A, 0xC2C6CC, 0x32D74B);   /* the accent a light grey (black and grey; was cyan, before that blue 0x3D8BFF) */
}

const char *k_theme_name(KThemeId id) { (void)id; return "Dark"; }

/* Canonical palette. Keep these values stable even when no corresponding
 * engine is installed: the colour is part of an effect's visual identity. */
static const uint32_t FX_COLORS[K_FX_COLOR_COUNT] = {
    0xE5484D, /* fuzz: red */
    0x4285E8, /* compressor: blue */
    0x9299A3, /* EQ: grey */
    0xE9C344, /* drive: yellow */
    0xF08A42, /* phaser: orange */
    0xA176DB, /* flanger: purple */
    0xE774AF, /* chorus: pink */
    0x39AE74, /* tape echo: green */
    0xAC815E, /* vibrato: brown */
    0x303238, /* harmonizer: black (outline needed on dark backgrounds) */
    0x37BDCF, /* filter: cyan */
    0xA3CF4E, /* auto-wah: lime */
    0xE7E9EB, /* tremolo: white (outline needed on light backgrounds) */
    0x6171C8, /* reverb: indigo */
    0x40BAA6  /* spring: turquoise */
};

uint32_t k_fx_color(KFxColorId id)
{
    return (unsigned)id < K_FX_COLOR_COUNT ? FX_COLORS[id] : 0;
}

/* Legacy slots retain their existing indices, and plugin order may change.
 * Only assign a family when its DSP identity is known; other effects remain
 * neutral rather than implying they implement an unrelated effect type. */
uint32_t k_fx_color_for_uid(uint32_t uid)
{
    switch (uid) {
    case K_UID('D','R','V','E'): return k_fx_color(K_FX_COLOR_DRIVE);
    case K_UID('F','L','N','G'): return k_fx_color(K_FX_COLOR_FLANGER);
    case K_UID('D','E','L','Y'): return k_fx_color(K_FX_COLOR_TAPE_ECHO); /* delay/echo family; not tape modelling */
    case K_UID('R','O','O','M'): return k_fx_color(K_FX_COLOR_REVERB);
    case K_UID('F','I','L','T'): return k_fx_color(K_FX_COLOR_FILTER);
    case K_UID('F','U','Z','Z'): return k_fx_color(K_FX_COLOR_FUZZ);
    case K_UID('W','A','H','0'): return k_fx_color(K_FX_COLOR_AUTO_WAH);
    case K_UID('P','H','S','R'): return k_fx_color(K_FX_COLOR_PHASER);
    case K_UID('T','R','M','L'): return k_fx_color(K_FX_COLOR_TREMOLO);
    default: return 0;
    }
}

static double lin(uint32_t v)
{
    double c = (double)v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
static double lum(uint32_t c)
{
    return 0.2126 * lin((c >> 16) & 255u) + 0.7152 * lin((c >> 8) & 255u) + 0.0722 * lin(c & 255u);
}
double k_contrast(uint32_t a, uint32_t b)
{
    double la = lum(a), lb = lum(b);
    return (la > lb ? la + 0.05 : lb + 0.05) / (la > lb ? lb + 0.05 : la + 0.05);
}
