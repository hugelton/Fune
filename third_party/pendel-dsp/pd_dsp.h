/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * From Pendel (the author's own), relicensed MPL-2.0 for Kreisel, 2026-11-07. */
/* The DSP module interface and the fixed-point helpers every module shares.
 *
 * Samples are pd_sample (int32_t) in Q15: 32767 is full scale. Buffers may run above full scale between
 * modules (headroom); only the final output is saturated to 16 bits. No floating point, no 64-bit
 * multiplies (see felucca-bsp docs/CPU.md), no allocation after init. */
#ifndef PD_DSP_H
#define PD_DSP_H

#include <stddef.h>
#include <stdint.h>

#include "pd_tables.h"

typedef int32_t pd_sample;

#define PD_BLOCK 32            /* most frames a process call is given */
#define PD_UNIT 32768          /* 1.0 in Q15 */
#define PD_PARAM_MAX 32767     /* every parameter is set as 0..PD_PARAM_MAX; the module maps it */
#define PD_PARAM_MID 16384     /* the middle: where a centred value is 0 and a new value sounds as before it was there */
/* a switch: on above the middle (the host keeps its values 0..100, 50 the default: off) */
static inline int pd_on(int32_t v) { return v > PD_PARAM_MID; }
/* set_param(state, PD_HOST_TEMPO, bpm): the song's tempo (40 .. 240 BPM), not a parameter of the module's: the host sends it to
 * a built-in effect that keeps time (a synced delay, a synced wobble); every other ignores the id */
#define PD_HOST_TEMPO 0xF0u
/* the note divisions a synced time steps through, shortest first: 1/32 .. 1/1, with the triplets and dotted between, in 192ths
 * of a whole note */
#define PD_DIVS 16
static const uint8_t PD_DIV_192[PD_DIVS] = { 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 128, 144, 192 };
static inline int pd_div_index(int32_t v) { int32_t i = (v < 0 ? 0 : v > PD_PARAM_MAX ? PD_PARAM_MAX : v) * PD_DIVS >> 15; return i; }
/* a division's length in samples at bpm (a whole note is 4 beats: d / 192 * 240 / bpm s); 32-bit: d fs 5 < 2^32 below 4.4 MHz */
static inline uint32_t pd_div_samples(int idx, uint32_t bpm, uint32_t fs)
{
    if (bpm < 20u) bpm = 20u;
    return (uint32_t)PD_DIV_192[idx < 0 ? 0 : idx >= PD_DIVS ? PD_DIVS - 1 : idx] * fs * 5u / (4u * bpm);
}

/* A DSP module. `state` is state_size bytes, `mem` is mem_size(rate) bytes (delay lines and the like),
 * both taken from the arena at init. process() works in place (in == out) as well as out of place;
 * a source (a synth) ignores `in`. set_param() may divide and is called between blocks, never inside one. */
typedef struct PendelDSP {
    const char *name;
    unsigned state_size;
    size_t (*mem_size)(uint32_t sample_rate);                 /* NULL: no extra memory */
    unsigned num_params;
    const char *const *param_names;
    void (*init)(void *state, void *mem, uint32_t sample_rate);
    void (*process)(void *state, const pd_sample *in, pd_sample *out, unsigned frames);
    void (*reset)(void *state);
    void (*set_param)(void *state, unsigned id, int32_t value);
} PendelDSP;

/* ---------------------------------------------------------------- arena --- */
/* Memory for modules, handed out once at start-up from a static pool (the BSP's .pool on hardware). */
typedef struct {
    uint8_t *base;
    size_t size, used;
} pd_arena;

void pd_arena_init(pd_arena *a, void *base, size_t size);
void *pd_arena_alloc(pd_arena *a, size_t n);              /* 8-byte aligned, zeroed; NULL when full */

/* A module and its state, made from the arena. Returns 0, or -1 when the arena is full. */
typedef struct {
    const PendelDSP *dsp;
    void *state;
} pd_node;

int pd_node_make(pd_node *n, pd_arena *a, const PendelDSP *dsp, uint32_t sample_rate);

/* ------------------------------------------------------------- helpers --- */
static inline int32_t pd_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t pd_sat16(int32_t v) { return pd_clamp(v, -32768, 32767); }
/* a * b >> 15 for |a| < 2^16 and |b| <= 2^15 (the product stays in 32 bits) */
static inline int32_t pd_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* tanh-like: unity slope at 0, 1.0 from 1.5 on (t - 4/27 t^3), |x| any */
static inline int32_t pd_soft(int32_t x)
{
    int32_t t = pd_clamp(x, -49152, 49152), h = t >> 1;
    int32_t t3 = ((((h * h) >> 13) * h) >> 14);          /* h^2 < 2^30; (t^2 >> 15) * h < 2^31 */
    return t - ((t3 * 4855) >> 15);
}

/* Signed ramp step for 1..32 frames: reciprocal Q16, 32-bit only.
 * |delta| <= 2^20 as required by the input and amp gain envelopes. */
static inline int32_t pd_step_per_frame(int32_t delta, unsigned frames)
{
    static const uint32_t inv[33] = {0, 65536, 32768, 21845, 16384, 13107, 10922, 9362, 8192, 7281, 6553, 5957, 5461, 5041, 4681, 4369, 4096, 3855, 3640, 3449, 3276, 3120, 2978, 2849, 2730, 2621, 2520, 2427, 2340, 2259, 2184, 2114, 2048};
    uint32_t mag = delta < 0 ? (uint32_t)(-delta) : (uint32_t)delta;
    uint32_t q;
    if (!frames || frames > PD_BLOCK) return 0;
    q = (((mag >> 8) * inv[frames]) >> 8) + (((mag & 255u) * inv[frames]) >> 16);
    return delta < 0 ? -(int32_t)q : (int32_t)q;
}

/* sine of a 32-bit phase, Q15, linearly interpolated */
static inline int32_t pd_sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = PD_SINE[i], b = PD_SINE[i + 1];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/* a 0..PD_PARAM_MAX parameter looked up in a 257-point table, interpolated */
static inline int32_t pd_lut16(const int16_t *t, int32_t p)
{
    int32_t i = p >> 7, f = p & 127;
    return t[i] + (((t[i + 1] - t[i]) * f) >> 7);
}
static inline uint32_t pd_lut32(const uint32_t *t, int32_t p)
{
    int32_t i = p >> 7;
    uint32_t a = t[i], b = t[i + 1];
    return a + (((b - a) * (uint32_t)(p & 127)) >> 7);   /* steps below 2^25 (PD_TIME's are ~16k) */
}

#endif
