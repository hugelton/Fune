/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_MUL_H
#define KREISEL_MUL_H
/* 32 x 32 -> 64-bit multiplies for the audio path. On the FM-1 (AC79, pi32v2) a 64-bit multiply inlined among 16-bit loads
 * now and then leaves a register a bit off and the firmware crashes (third_party/felucca-bsp/docs/CPU.md): there each is a
 * function of its own (K_MUL_OUTLINE, platforms/fm1hw/build.sh), whose product is used before any load follows. Elsewhere they
 * inline. Every 64-bit product the engines, DRUM, the FX and the scheduler take goes through these (tools/fm1_mul_scan.py
 * lists any left in the FM-1 image). */
#include <stdint.h>
#if defined(K_MUL_OUTLINE) && K_MUL_OUTLINE
#define K_MUL_FN static __attribute__((noinline, unused))
#else
#define K_MUL_FN static inline
#endif
K_MUL_FN int64_t k_smul(int32_t a, int32_t b) { return (int64_t)a * b; }
K_MUL_FN uint64_t k_umul(uint32_t a, uint32_t b) { return (uint64_t)a * b; }
K_MUL_FN int32_t k_mulsh(int32_t a, int32_t b, int sh) { return (int32_t)(((int64_t)a * b) >> sh); }   /* (a b) >> sh, signed */
K_MUL_FN uint32_t k_umulsh(uint32_t a, uint32_t b, int sh) { return (uint32_t)(((uint64_t)a * b) >> sh); }
#endif
