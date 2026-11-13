/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_GFX_REC_H
#define KREISEL_GFX_REC_H
/* The drawing as a list of shapes (tools/ui_sketch.c, for Sketch): only in a build with G_RECORD. Each g_* call writes one JSON
 * line (its kind, its numbers, its colours, its text) to g_rec_out, the outermost call only: a panel is one panel, not the
 * rectangles and lines it is made of. Nothing of this is in the device builds. */
#ifdef G_RECORD
#include <stdio.h>
#include <stdint.h>
extern FILE *g_rec_out;
extern int g_rec_depth;
void g_rec(const char *kind, const int *iv, int ni, const uint32_t *cv, int nc, const char *s);
void g_rec_curve16(const int *p16, int n, int th16, uint32_t col, int base);
static inline void g_rec_leave(int *d) { (void)d; g_rec_depth--; }
#define REC_GUARD int g_rec_guard__ __attribute__((cleanup(g_rec_leave), unused)) = g_rec_depth++
#define REC(kind, ints, cols, s) \
    REC_GUARD; do { if (g_rec_depth == 1 && g_rec_out) { const int iv__[] = ints; const uint32_t cv__[] = cols; \
        g_rec(kind, iv__, (int)(sizeof iv__ / sizeof iv__[0]), cv__, (int)(sizeof cv__ / sizeof cv__[0]), s); } } while (0)
#define REC_CURVE16(p16, n, th16, col, base) \
    REC_GUARD; do { if (g_rec_depth == 1 && g_rec_out) g_rec_curve16(p16, n, th16, col, base); } while (0)
#define I_(...) {__VA_ARGS__}
#else
#define REC(kind, ints, cols, s) ((void)0)
#define REC_CURVE16(p16, n, th16, col, base) ((void)0)
#define I_(...) {0}
#endif
#endif
