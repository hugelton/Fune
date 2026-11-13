/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_ARP_H
#define KREISEL_ARP_H
#include <stdint.h>

/* The ARP page's rate names, expressed as MIDI clocks (24 per beat).
 * Integer clocks let external USB/TRS MIDI drive straight and triplet rates
 * never depend on the platform's audio buffer size. */
static const uint16_t K_ARP_CLOCKS[10] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384};

/* Choose one pitch in a sorted input sequence. ORD's caller supplies
 * note-on order instead. No floating point, heap or global random state.
 * For UP+8 extra_octave tells the host to add a further octave. */
static inline int k_arp_pick(unsigned mode, uint32_t step, unsigned count,
                             uint32_t *rng, unsigned *walk,
                             unsigned *extra_octave)
{
    uint32_t at = step;
    uint32_t x;
    if (extra_octave) *extra_octave = 0;
    if (!count || !mode || mode > 14) return -1;
    switch (mode) {
    case 1: case 5: return (int)(at % count);                    /* UP / ORD */
    case 2: return (int)(count - 1u - at % count);             /* DN */
    case 3: {                                                 /* UPDN, without repeating endpoints */
        unsigned cycle = count > 1 ? count * 2u - 2u : 1u;
        unsigned n = at % cycle;
        return (int)(n < count ? n : cycle - n);
    }
    case 4:                                                    /* RND */
        x = *rng ? *rng : 0x9E3779B9u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        *rng = x;
        return (int)(x % count);
    case 6: return (int)((at / 2u) % count);                  /* REPEAT each note twice */
    case 7: {                                                 /* DNUP */
        unsigned cycle = count > 1 ? count * 2u - 2u : 1u;
        unsigned n = at % cycle;
        return (int)(count - 1u - (n < count ? n : cycle - n));
    }
    case 8:                                                    /* UP+8, double each note at +12 */
        if (extra_octave) *extra_octave = at & 1u;
        return (int)((at / 2u) % count);
    case 9: {                                                  /* CONV: edges inward */
        unsigned n = at % count;
        return (int)((n & 1u) ? count - 1u - n / 2u : n / 2u);
    }
    case 10: {                                                 /* DIVG: centre outward */
        unsigned n = count - 1u - at % count;
        return (int)((n & 1u) ? count - 1u - n / 2u : n / 2u);
    }
    case 11: return (int)((at & 1u) ? count - 1u : (at / 2u) % count); /* PINKY */
    case 12: return (int)((at & 1u) ? 0 : (at / 2u) % count);           /* THUMB */
    case 13: {                                                 /* WALK: random up, stay or down */
        x = *rng ? *rng : 0xD1B54A35u;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        *rng = x;
        if (*walk >= count) *walk = 0;
        if (x % 3u == 0 && *walk > 0) --*walk;
        if (x % 3u == 2 && *walk + 1u < count) ++*walk;
        return (int)*walk;
    }
    case 14: return 0;                                         /* CHORD: host fires all */
    default: return -1;
    }
}
#endif
