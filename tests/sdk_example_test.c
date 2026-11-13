/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The SDK's example (plugins/beep, docs/PLUGIN_SDK.md: A minimal engine) builds, loads and plays as the page says: it fits the
 * FM-1's room, registers after the built-in engines, a track plays its sine at the note's pitch, its noise when NOIS is up,
 * nothing at LVL 0; a song names it by id and finds it again. */
#include "kreisel.h"
#include "kreisel_audio.h"
#include "kreisel_engine_api.h"
#include "kreisel_plugin_load.h"
#include "kreisel_song.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int beep;

static double play(KApp *g, int note, int *crossings)   /* a note on track 1: the energy of its second tenth of a second, and the
                                                         * left channel's upward zero crossings there */
{
    static KAudio a;
    static float buf[2 * 4410];
    double sum = 0;
    size_t i;
    k_audio_init(&a, 44100);
    k_audio_sync(&a, g);
    k_audio_note(&a, 0, note, 100, 1);
    k_audio_render(&a, buf, 4410);
    k_audio_render(&a, buf, 4410);
    *crossings = 0;
    for (i = 0; i < 4410; i++) {
        assert(fabsf(buf[2 * i]) <= 1.f);
        sum += (double)buf[2 * i] * buf[2 * i];
        if (i && buf[2 * i - 2] < 0.f && buf[2 * i] >= 0.f) ++*crossings;
    }
    return sum;
}
static void take(KApp *g, int noise, int level)   /* track 1 plays BEEP at its defaults, but NOIS and LVL */
{
    int i;
    k_init(g);
    g->sound[0] = beep;
    for (i = 0; i < k_engine_at(beep)->nparams; i++) g->params[0][K_PAGE_SOUND][4 + i] = k_engine_at(beep)->edit[i].def;
    g->params[0][K_PAGE_SOUND][4 + 0] = noise;
    g->params[0][K_PAGE_SOUND][4 + 1] = level;
}

int main(void)
{
    static const KDeviceCaps fm1 = {44100u, 16u * 1024u, 120u, 6u, 0u};   /* the FM-1's room (PLUGIN_SDK.md): no FPU */
    static uint8_t song[4096];
    KApp g;
    int x_sine, x_noise, x_quiet;
    double e_sine, e_noise, e_quiet;
    size_t n;
    k_plugin_set_caps(&fm1);
    beep = k_plugin_load_file(KREISEL_SDK_EXAMPLE);
    assert(beep >= K_ENGINE_COUNT && k_engine_find(K_UID('b', 'e', 'e', 'p')) == beep);
    assert(k_plugin_check(k_plugin_at(beep), 0) == 0 && k_plugin_at(beep)->version == 1);
    assert(k_engine_at(beep)->nparams == 3 && k_engine_at(beep)->page2 == 2);

    take(&g, 0, 110);   /* A4: a sine of 440 Hz, 44 rising crossings a tenth of a second (an octave off: 22 or 88) */
    e_sine = play(&g, 69, &x_sine);
    assert(e_sine > 1e-2 && x_sine >= 42 && x_sine <= 46);
    take(&g, 127, 110);   /* all noise: far more crossings */
    e_noise = play(&g, 69, &x_noise);
    assert(e_noise > 1e-3 && x_noise > 4 * x_sine);
    take(&g, 0, 0);   /* LVL 0: nothing from the engine */
    e_quiet = play(&g, 69, &x_quiet);
    assert(e_quiet < 1e-9);

    take(&g, 40, 100);   /* a song names it by id, and its values come back */
    n = k_song_save(&g, song, sizeof song);
    assert(n > 0);
    k_init(&g);
    assert(k_song_load(&g, song, n) == 0 && !k_track_missing(&g, 0) && g.sound[0] == beep);
    assert(g.params[0][K_PAGE_SOUND][4 + 0] == 40 && g.params[0][K_PAGE_SOUND][4 + 1] == 100);

    printf("sdk example: BEEP at A4 %d crossings (noise %d), energy %.4f\nkreisel sdk example test: OK\n", x_sine, x_noise, e_sine);
    return 0;
}
