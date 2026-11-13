/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_DRUM_SAMPLES_H
#define KREISEL_DRUM_SAMPLES_H
/* DRUM's sample pieces (src/drum_samples.c, made by tools/gen_drum_samples.py from assets/drums): mono int16 at 44.1 kHz, in the
 * order of the pieces SP BD, SP CH, SP OH. */
#include <stdint.h>
#define K_DRUM_SAMPLES 3
typedef struct { const int16_t *d; uint32_t n; } KDrumSample;
extern const KDrumSample K_DRUM_SAMPLE[K_DRUM_SAMPLES];
#endif
