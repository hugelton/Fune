/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments */
#ifndef KREISEL_EFFECT_H
#define KREISEL_EFFECT_H
#include <stdint.h>
#include <stddef.h>

/* Host-owned, fixed-point effect ABI. In/out may alias. Q15 in int32_t with
 * headroom; an effect never allocates or blocks while process() is running.
 * Instances get independent state and optional delay memory at construction. */
typedef struct KEffectDSP {
    const char *name;
    unsigned state_size;
    size_t (*mem_size)(uint32_t sample_rate);
    unsigned num_params;
    const char *const *param_names;
    void (*init)(void *state, void *mem, uint32_t sample_rate);
    void (*process)(void *state, const int32_t *in, int32_t *out, unsigned frames);
    void (*reset)(void *state);
    void (*set_param)(void *state, unsigned id, int32_t value); /* 0..32767; outside render */
} KEffectDSP;
#endif
