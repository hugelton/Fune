/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_MATH_H
#define KREISEL_MATH_H
/* The float functions the UI and the audio use, with no libm: only +, -, * and comparisons (and a cast), so they run wherever float
 * arithmetic does (the FM-1 chip has no FPU: every operation is a library call, and the toolchain's libm was seen to fault there).
 * Accurate to about 1e-5 (sin, cos, sqrt) and 1e-4 (exp, pow): enough for drawing and for a preview voice. On the FM-1 the build defines
 * KREISEL_NO_LIBM and the ordinary names (sinf ...) become these; elsewhere nothing changes, and tests/math_test.c checks them against libm. */
#include <stdint.h>

static inline float k_fabsf(float x) { return x < 0.f ? -x : x; }
static inline float k_floorf(float x)
{
    if (x >= 8388608.f || x <= -8388608.f) return x;   /* already whole */
    int i = (int)x;
    return (float)(i - ((float)i > x));
}
static inline float k_ceilf(float x) { return -k_floorf(-x); }
static inline float k_sqrtf(float x)
{
    union { float f; uint32_t u; } v;
    float y;
    int i;
    if (x <= 0.f) return 0.f;
    v.f = x;
    v.u = 0x1fbd1df5u + (v.u >> 1);   /* a first guess from the exponent */
    y = v.f;
    for (i = 0; i < 3; i++) y = .5f * (y + x / y);
    return y;
}
static inline float k_sinf(float x)
{
    const float PI = 3.14159265f, TWO_PI = 6.28318531f, HALF_PI = 1.57079633f;
    float x2;
    x -= TWO_PI * k_floorf(x / TWO_PI + .5f);   /* into -pi .. pi */
    if (x > HALF_PI) x = PI - x;                /* sin(pi - x) = sin(x): into -pi/2 .. pi/2 */
    else if (x < -HALF_PI) x = -PI - x;
    x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6.f + x2 * (1.f / 120.f + x2 * (-1.f / 5040.f + x2 * (1.f / 362880.f)))));
}
static inline float k_cosf(float x) { return k_sinf(x + 1.57079633f); }
/* 2^x */
static inline float k_exp2f(float x)
{
    union { float f; uint32_t u; } v;
    float fl, f;
    int i;
    if (x < -126.f) return 0.f;
    if (x > 127.f) x = 127.f;
    fl = k_floorf(x);
    f = x - fl;                                 /* 0 .. 1 */
    i = (int)fl + 127;
    v.u = (uint32_t)i << 23;                    /* 2^fl */
    /* 2^f on 0..1: a degree-5 minimax polynomial */
    f = 1.f + f * (.693147181f + f * (.240226507f + f * (.0555041087f + f * (.00961812911f + f * .00133335581f))));
    return v.f * f;
}
static inline float k_expf(float x) { return k_exp2f(x * 1.44269504f); }
/* log2(x), x > 0 */
static inline float k_log2f(float x)
{
    union { float f; uint32_t u; } v;
    float m, e, t;
    if (x <= 0.f) return -126.f;
    v.f = x;
    e = (float)((int)((v.u >> 23) & 255u) - 127);
    v.u = (v.u & 0x007fffffu) | 0x3f800000u;   /* the mantissa, 1 .. 2 */
    m = v.f;
    t = (m - 1.f) / (m + 1.f);                  /* ln(m) = 2 atanh(t), t in 0 .. 1/3 */
    return e + 2.88539008f * t * (1.f + (t * t) * (.333333333f + (t * t) * (.2f + (t * t) * (.142857143f))));
}
static inline float k_powf(float x, float y)
{
    if (x <= 0.f) return y == 0.f ? 1.f : 0.f;
    return k_exp2f(y * k_log2f(x));
}

#ifdef KREISEL_NO_LIBM
#define sinf k_sinf
#define cosf k_cosf
#define sqrtf k_sqrtf
#define fabsf k_fabsf
#define floorf k_floorf
#define ceilf k_ceilf
#define expf k_expf
#define powf k_powf
#endif
#endif
