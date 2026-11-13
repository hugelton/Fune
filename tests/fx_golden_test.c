/* SPDX-License-Identifier: MPL-2.0
 * The built-in effects sound as they did before their third values (2026-11-12): each is rendered with only its first values
 * set (as a song from before them has it, the new value left at its default) and its output hashed against the hashes taken
 * from the code before the change. `kreisel_fx_golden_test print` prints the hashes of the code as it is. */
#include "kreisel_audio.h"
#include "kreisel_fx.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 32u
#define BLOCKS 2400u
#define SETS 5
#define OLD_PARAMS(type) ((type) == 1 ? 3 : (type) == 6 ? 3 : (type) == 15 ? 7 : (type) == 5 ? 1 : 2)   /* before 2026-11-12 */

static uint64_t fnv(uint64_t h, int32_t v)
{
    for (int k = 0; k < 4; k++) { h ^= (uint8_t)((uint32_t)v >> (8 * k)); h *= 1099511628211ull; }
    return h;
}
/* a test signal: a saw, a louder square burst, a noise, silence at the end (the tails) */
static int32_t input(uint32_t i, uint32_t *rng)
{
    *rng = *rng * 1664525u + 1013904223u;
    if (i >= BLOCK * BLOCKS * 3 / 4) return 0;
    {
        const int32_t saw = (int32_t)((i * 300u) & 0xFFFFu) - 32768;
        const int32_t sq = (i / 50u) & 1u ? 40000 : -40000;
        const int32_t nz = (int32_t)(*rng >> 17) - 16384;
        return (i / 4000u) % 3u == 0 ? saw / 2 : (i / 4000u) % 3u == 1 ? sq : nz;
    }
}
static uint64_t render(int type, int set, int extra)
{
    static uint8_t mem[1 << 18] __attribute__((aligned(8)));
    static uint8_t state[1024] __attribute__((aligned(8)));
    static const int32_t SET[SETS][7] = {
        {-1, -1, -1, -1, -1, -1, -1},   /* the effect's own start */
        {16384, 16384, 16384, 16384, 16384, 16384, 16384},
        {0, 0, 0, 0, 0, 0, 0},
        {32767, 32767, 32767, 32767, 32767, 32767, 32767},
        {7000, 26000, 12000, 30000, 3000, 20000, 25000},
    };
    const KPlugin *p = k_effect_at(type);
    const KEffectDSP *d = p->dsp;
    const size_t m = d->mem_size ? d->mem_size(K_AUDIO_FS) : 0;
    uint64_t h = 1469598103934665603ull;
    uint32_t rng = 12345u, i = 0;
    int32_t buf[BLOCK];
    assert(d->state_size <= sizeof state && m <= sizeof mem);
    memset(state, 0, sizeof state); memset(mem, 0, sizeof mem);
    d->init(state, m ? mem : 0, K_AUDIO_FS);
    for (int k = 0; k < OLD_PARAMS(type); k++) if (SET[set][k] >= 0) d->set_param(state, (unsigned)k, SET[set][k]);
    if (extra) for (int k = OLD_PARAMS(type); k < (int)d->num_params; k++) d->set_param(state, (unsigned)k, 16384);   /* the new ones at their middle */
    for (unsigned b = 0; b < BLOCKS; b++) {
        for (unsigned j = 0; j < BLOCK; j++) buf[j] = input(i++, &rng);
        d->process(state, buf, buf, BLOCK);
        for (unsigned j = 0; j < BLOCK; j++) h = fnv(h, buf[j]);
    }
    return h;
}

static const uint64_t GOLD[K_FX_BUILTINS][SETS] = {
#include "fx_golden.h"
};

int main(int argc, char **argv)
{
    const int print = argc > 1 && !strcmp(argv[1], "print");
    int bad = 0;
    for (int type = 1; type <= (int)K_FX_BUILTINS; type++)
        for (int s = 0; s < SETS; s++) {
            const uint64_t h = render(type, s, 0);
            if (print) { printf("%s0x%016llxull%s", s ? " " : "    {", (unsigned long long)h, s == SETS - 1 ? "},\n" : ","); continue; }
            if (h != GOLD[type - 1][s]) { fprintf(stderr, "%s set %d: sounds otherwise than before\n", k_effect_at(type)->desc->name, s); bad = 1; }
            if (s && render(type, s, 1) != h) { fprintf(stderr, "%s set %d: the new values at the middle change it\n", k_effect_at(type)->desc->name, s); bad = 1; }
        }
    assert(!bad);
    return 0;
}
