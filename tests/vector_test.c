/* SPDX-License-Identifier: MPL-2.0
 * VECTOR WT: identity, corner morphing, parameter editing, MIDI range,
 * level mute, live modulation and song persistence. */
#include "kreisel_audio.h"
#include "kreisel_engine.h"
#include "kreisel_plugin.h"
#include "kreisel_song.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define VFRAMES 2048
static float captures[4][2 * VFRAMES];

static void setup(KApp *a, int engine, int x, int y)
{
    k_init(a);
    a->sound[0] = engine;
    for (int p = 0; p < K_ENGINE[engine].nparams; p++)
        a->params[0][K_PAGE_SOUND][4 + p] = K_ENGINE[engine].edit[p].def;
    a->params[0][K_PAGE_SOUND][4] = x;
    a->params[0][K_PAGE_SOUND][5] = y;
    a->params[0][K_PAGE_SOUND][6] = 0;  /* manual position for corner comparisons */
    a->params[0][K_PAGE_SOUND][7] = 0;  /* no detune in the static comparison */
    a->params[0][K_PAGE_SOUND][9] = 127; /* full level */
    for (int t = 1; t < K_TRACKS; t++) a->params[t][K_PAGE_MIXER][0] = 0;
}
static double capture(const KApp *p, int note, float *out)
{
    KAudio a;
    double sum = 0;
    k_audio_init(&a, K_AUDIO_FS);
    k_audio_sync(&a, p);
    k_audio_note(&a, 0, note, 115, 1);
    k_audio_render(&a, out, VFRAMES);
    for (int i = 0; i < VFRAMES * 2; i++) {
        assert(isfinite(out[i]) && fabsf(out[i]) <= 1.f);
        sum += (double)out[i] * out[i];
    }
    return sum;
}
int main(void)
{
#if !K_WITH_VECTOR
    puts("VECTOR is not built in (K_WITH_VECTOR 0): skipped");
    return 0;
#endif

    int id = k_engine_find(K_UID('K','V','E','C'));
    KApp a, b;
    uint8_t song[16384];
    size_t n;
    assert(id >= 0 && id < K_ENGINE_COUNT - 1);
    assert(k_engine_find(K_UID('D','R','U','M')) == K_ENGINE_COUNT - 1);
    assert(k_plugin_at(id)->kind == K_PLUG_SYNTH);
    assert(!strcmp(k_engine_at(id)->name, "Vector"));
    assert(k_engine_at(id)->nparams == 10 && k_engine_at(id)->page2 == 6);
    for (int m = 0; m < 4; m++) assert(k_engine_macro(id, m) == m);
    for (int k = 6; k < 10; k++) assert(k_engine_at(id)->edit[k].max == 7);
    for (int corner = 0; corner < 4; corner++) {
        setup(&a, id, (corner & 1) ? 127 : 0, (corner & 2) ? 127 : 0);
        assert(capture(&a, 60, captures[corner]) > 1e-3);
    }
    for (int corner = 1; corner < 4; corner++) {
        double difference = 0;
        for (int i = 0; i < VFRAMES * 2; i++) {
            double delta = captures[0][i] - captures[corner][i];
            difference += delta * delta;
        }
        assert(difference > 1e-3); /* all four corners play distinct timbres */
    }
    setup(&a, id, 64, 64);
    for (int note = 36; note <= 127; note += 13)
        assert(capture(&a, note, captures[0]) > 1e-4);
    setup(&a, id, 64, 64);
    a.params[0][K_PAGE_SOUND][9] = 0; /* LEVEL */
    assert(capture(&a, 60, captures[0]) == 0);
    setup(&a, id, 64, 64);
    {
        KAudio audio;
        float buf[2 * VFRAMES];
        double sum = 0;
        a.params[0][K_PAGE_SOUND][6] = 110; /* MOVE */
        a.params[0][K_PAGE_SOUND][8] = 96;  /* RATE */
        k_audio_init(&audio, K_AUDIO_FS);
        k_audio_sync(&audio, &a);
        k_audio_note(&audio, 0, 60, 120, 1);
        for (int j = 0; j < 5; j++) {
            k_audio_render(&audio, buf, VFRAMES);
            for (int i = 0; i < VFRAMES * 2; i++) {
                assert(isfinite(buf[i]) && fabsf(buf[i]) <= 1.f);
                sum += (double)buf[i] * buf[i];
            }
        }
        assert(sum > 0.01);
        a.params[0][K_PAGE_SOUND][4] = 127;
        a.params[0][K_PAGE_SOUND][5] = 0;
        k_audio_sync(&audio, &a);
        k_audio_render(&audio, buf, VFRAMES);
        for (int i = 0; i < VFRAMES * 2; i++) assert(isfinite(buf[i]) && fabsf(buf[i]) <= 1.f);
        k_audio_note(&audio, 0, 60, 0, 0);
    }
    setup(&a, id, 21, 108);
    a.params[0][K_PAGE_SOUND][10] = 6; /* A: STRING */
    a.params[0][K_PAGE_SOUND][13] = 7; /* D: BELL */
    n = k_song_save(&a, song, sizeof song);
    assert(n > 0);
    k_init(&b);
    assert(k_song_load(&b, song, n) == K_SONG_OK);
    assert(b.sound[0] == id);
    assert(b.params[0][K_PAGE_SOUND][4] == 21 && b.params[0][K_PAGE_SOUND][5] == 108);
    assert(b.params[0][K_PAGE_SOUND][10] == 6 && b.params[0][K_PAGE_SOUND][13] == 7);
    puts("VECTOR WT: four corners, high notes, live motion, mute, persistence OK");
    return 0;
}
