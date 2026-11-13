/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_THEME_H
#define KREISEL_THEME_H

#include <stdint.h>

/* A theme is five colours (0xRRGGBB); every other token is a fixed blend of them:
 *   color1 the background, color2 the identity colour (values, gauges, selections), color3 the text,
 *   primary REC and warnings, secondary the one active thing (focus, the selected track, a hot value). */
typedef enum { K_THEME_DARK, K_THEME_COUNT } KThemeId;   /* Dark only (2026-11-05: no Light theme) */

/* FX identity colours, independent of DSP availability and of slot/order.
 * Each effect has one stable accent; actual parameter amounts use the knob arc. */
typedef enum {
    K_FX_COLOR_FUZZ, K_FX_COLOR_COMP, K_FX_COLOR_EQ, K_FX_COLOR_DRIVE,
    K_FX_COLOR_PHASER, K_FX_COLOR_FLANGER, K_FX_COLOR_CHORUS,
    K_FX_COLOR_TAPE_ECHO, K_FX_COLOR_VIBRATO, K_FX_COLOR_HARMONIZER,
    K_FX_COLOR_FILTER, K_FX_COLOR_AUTO_WAH, K_FX_COLOR_TREMOLO,
    K_FX_COLOR_REVERB, K_FX_COLOR_SPRING, K_FX_COLOR_COUNT
} KFxColorId;
uint32_t k_fx_color(KFxColorId id);             /* 0 for an invalid palette id */
uint32_t k_fx_color_for_uid(uint32_t uid);     /* 0 when the effect has no assigned identity */

typedef struct {
    uint32_t bg, surf, text, theme, accent, rec, ink;
    uint32_t mid, dim, line, sel, tint, raise;
    uint32_t ok;            /* a status green (charging, done): fixed */
    uint32_t key, edge;     /* a keycap's fill (its label is ink); a 1 px outline */
    uint32_t shadow;        /* what a raised part casts down and to the right ... */
    int shadow_pct;         /* ... and how strongly at its core (a light theme needs less) */
    int shadow_blur, shadow_dy;      /* 0 blur: a hard ledge shadow_dy px deep under a raised part */
    int rim_pct, top_pct, bot_pct;   /* a raised face: its top edge toward white, its top lightened, its bottom darkened (0: flat) */
} KTheme;

uint32_t k_mix(uint32_t a, uint32_t b, int pct);   /* a + (b - a) * pct / 100 per channel */
void k_theme(KTheme *t, KThemeId id);
const char *k_theme_name(KThemeId id);
double k_contrast(uint32_t a, uint32_t b);         /* WCAG contrast ratio */

#endif
