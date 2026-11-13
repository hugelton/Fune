/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_LEVEL_H
#define KREISEL_LEVEL_H
/* A fader's scale (the mixer's levels, the buses' returns): its 0..100 in dB, in even steps near the top so a turn reads as
 * dB. 100 +6 dB, 88 0 dB, 48 -20 dB (half a dB a step), 18 -50 dB (a dB a step), 1 -92.5 dB (2.5 dB a step), 0 silent.
 * The audio, the drawing and the tests all go by this one table. */
#define K_LEVEL_0DB 88
static inline float k_level_db(int v)
{
    if (v >= 48) return (float)(v - K_LEVEL_0DB) * .5f;
    if (v >= 18) return -20.f - (float)(48 - v);
    return -50.f - (float)(18 - v) * 2.5f;
}
static inline int k_db_level(float d)   /* the inverse: the step nearest d dB */
{
    float v = d >= -20.f ? K_LEVEL_0DB + d * 2.f : d >= -50.f ? 48.f + (d + 20.f) : 18.f + (d + 50.f) / 2.5f;
    int i = (int)(v < 0.f ? v - .5f : v + .5f);
    return i < 0 ? 0 : i > 100 ? 100 : i;
}
/* the same two in integers (the drawing: no FPU on the FM-1): the level in tenths of a dB (exact), and the step nearest d whole dB */
static inline int k_level_db10(int v)
{
    if (v >= 48) return (v - K_LEVEL_0DB) * 5;
    if (v >= 18) return -200 - (48 - v) * 10;
    return -500 - (18 - v) * 25;
}
static inline int k_db_level_i(int d)
{
    int i;
    if (d >= -20) i = K_LEVEL_0DB + d * 2;
    else if (d >= -50) i = 48 + (d + 20);
    else {   /* 18 + (d + 50) / 2.5, rounded half away from 0: (2 d + 190) / 5 */
        const int n = 2 * d + 190;
        i = n >= 0 ? (2 * n + 5) / 10 : -((-2 * n + 5) / 10);
    }
    return i < 0 ? 0 : i > 100 ? 100 : i;
}
#endif
