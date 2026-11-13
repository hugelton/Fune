/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The libm-free float functions (src/kreisel_math.h) agree with libm well enough for drawing and a preview voice. */
#include "kreisel_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float worst;
static void near(float got, float want, float tol, const char *what, float at)
{
    float d = fabsf(got - want);
    if (d > worst) worst = d;
    if (d > tol) printf("%s(%g): %g, want %g (off by %g)\n", what, at, got, want, d);
    assert(d <= tol);
}
int main(void)
{
    float x;
    for (x = -50.f; x <= 50.f; x += .0137f) {
        near(k_sinf(x), sinf(x), 2e-5f, "sin", x);
        near(k_cosf(x), cosf(x), 2e-5f, "cos", x);
        near(k_floorf(x), floorf(x), 0.f, "floor", x);
        near(k_ceilf(x), ceilf(x), 0.f, "ceil", x);
        near(k_fabsf(x), fabsf(x), 0.f, "fabs", x);
    }
    for (x = 0.001f; x <= 5000.f; x *= 1.07f) near(k_sqrtf(x) / sqrtf(x), 1.f, 1e-5f, "sqrt", x);
    for (x = -20.f; x <= 20.f; x += .05f) near(k_expf(x) / expf(x), 1.f, 2e-4f, "exp", x);
    for (x = .001f; x <= 100000.f; x *= 1.1f) near(k_log2f(x), log2f(x), 2e-4f, "log2", x);
    near(k_powf(16000.f / 30.f, .5f) / powf(16000.f / 30.f, .5f), 1.f, 3e-4f, "pow", 0.5f);   /* the cutoff curve of the synth page */
    near(k_powf(400.f, .37f) / powf(400.f, .37f), 1.f, 3e-4f, "pow", .37f);
    near(k_powf(2.f, 0.f), 1.f, 1e-6f, "pow", 0.f);
    near(k_powf(0.f, 2.f), 0.f, 1e-6f, "pow0", 0.f);
    printf("math: worst absolute error %g\n", worst);
    return 0;
}
