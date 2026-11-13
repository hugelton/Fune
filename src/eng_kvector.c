/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments
 * VECTOR: four selectable band-limited wavetables, bilinear XY and slow motion.
 * Uses the host ADSR, LFO and voice allocator. No filtering, heap, float,
 * 64-bit math or division in any sample processing loop. */
#include "vector_tables.h"

/* One shared morphed wavetable per track, three harmonic tiers.
 * 3 * 128 * 2 = 768 bytes per track, 3 KiB across all tracks; 6 KiB ROM.
 * Not per voice. A cache miss remixes the tables once in block() only. */
typedef struct {
    int16_t morphed[KV_MIPS][KV_LEN];
    uint32_t phase;
    uint8_t x, y, wave[4], valid;
    uint8_t base_x, base_y, last_motion, last_rate, ticks;
} kv_track_t;
static kv_track_t kv_t[NPART];

static void kvector_block(track_t *t)
{
    kv_track_t *q = &kv_t[(unsigned)(t - trk) % NPART];
    const int16_t *p = t->p;
    int32_t x = clamp(p[P_E0], 0, 127), y = clamp(p[P_E1], 0, 127);
    int32_t motion = clamp(p[P_E2], 0, 127), rate = clamp(p[P_E4], 0, 127);
    unsigned w[4], m, i;
    int32_t weight[4];
    q->phase += (uint32_t)rate * 49000u;  /* max ~2 Hz at 44.1k / 32 */
    if (motion && rate) {
        x = clamp(x + ((sine_i(q->phase) * motion) >> 16), 0, 127);
        y = clamp(y + ((sine_i(q->phase + 0x40000000u) * motion) >> 16), 0, 127);
    }
    for (i = 0; i < 4; i++) w[i] = (unsigned)clamp(p[P_E6 + i], 0, KV_WAVES - 1);
    {
        const int manual_x = clamp(p[P_E0], 0, 127), manual_y = clamp(p[P_E1], 0, 127);
        const int unchanged = q->valid && q->base_x == manual_x && q->base_y == manual_y &&
            q->last_motion == motion && q->last_rate == rate &&
            q->wave[0] == w[0] && q->wave[1] == w[1] &&
            q->wave[2] == w[2] && q->wave[3] == w[3];
        /* Cap motion remixes to one per eight control blocks (~172/s):
         * the phase still advances every block, manual edits apply immediately. */
        if (unchanged && motion && rate && ++q->ticks < 8u) return;
        q->ticks = 0;
        if (unchanged && q->x == x && q->y == y) return;
        q->base_x = (uint8_t)manual_x;
        q->base_y = (uint8_t)manual_y;
        q->last_motion = (uint8_t)motion;
        q->last_rate = (uint8_t)rate;
    }

    /* Positive bilinear weights sum to 127^2 = 16129, near Q14. */
    weight[0] = (127 - x) * (127 - y);  /* top left, A */
    weight[1] = x * (127 - y);          /* top right, B */
    weight[2] = (127 - x) * y;          /* bottom left, C */
    weight[3] = x * y;                  /* bottom right, D */
    for (m = 0; m < KV_MIPS; m++) for (i = 0; i < KV_LEN; i++) {
        int32_t sum = 0;
        for (unsigned k = 0; k < 4; k++)
            sum += ((int32_t)KV_TABLE[m][w[k]][i] * weight[k]) >> 14;
        q->morphed[m][i] = (int16_t)clamp(sum, -30000, 30000);
    }
    q->x = (uint8_t)x; q->y = (uint8_t)y;
    for (i = 0; i < 4; i++) q->wave[i] = (uint8_t)w[i];
    q->valid = 1;
}

static inline int32_t kvector_lookup(const int16_t *tab, uint32_t ph)
{
    uint32_t i = ph >> 25, f = (ph >> 17) & 255u;
    int32_t a = tab[i], b = tab[(i + 1u) & 127u];
    return a + (((b - a) * (int32_t)f) >> 8);
}
static void kvector_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    kv_track_t *q = &kv_t[(unsigned)(t - trk) % NPART];
    const int16_t *p = t->p;
    unsigned mip = m->pitch16 <= 82 * 16 ? 0u : m->pitch16 <= 100 * 16 ? 1u : 2u;
    const int16_t *tab = q->morphed[mip];
    int32_t lvl = clamp(p[P_E5], 0, 127) * 258;
    uint32_t p0 = v->ph[0], p1 = v->ph[1], inc = m->inc;
    /* Two phase accumulators / one wavetable, +/- ~3.4 cents at maximum. */
    uint32_t det = ((inc >> 12) * (uint32_t)clamp(p[P_E3], 0, 127)) >> 4;
    uint32_t inc0 = inc - det, inc1 = inc + det;
    for (uint32_t i = 0; i < n; i++) {
        int32_t a = kvector_lookup(tab, p0), b = kvector_lookup(tab, p1);
        int32_t sample = mulq15((a + b) >> 1, lvl);
        out[i] += voice_amp(sample, m, i);
        p0 += inc0;
        p1 += inc1;
    }
    v->ph[0] = p0; v->ph[1] = p1;
}
static const engine_t ENG_KVECTOR = {
    .name = K_ENGINE_NAME_KVEC,
    .page_title = {"MORPH", "WAVES"},
    .block = kvector_block,
    .render = kvector_render,
    .knob = {P_E0, P_E1, P_E2, P_E3},
};
