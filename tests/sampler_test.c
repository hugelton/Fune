/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler (K_WITH_SAMPLES: Linux and macOS): the recorder (a start that waits for a sound, what came before it kept, the
 * level, the gain brought up, the WAV written and read back), the tracks' samples, the engine played at pitch (an octave up is
 * twice as fast, KEY off stays at ROOT's, reversed, a loop that keeps sounding, a one-shot that ends), and a song keeping the path. */
#define _GNU_SOURCE   /* (memmem) */
#include "kreisel.h"
#include "kreisel_audio.h"
#include "kreisel_engine_api.h"
#include "kreisel_song.h"
#include "kreisel_save.h"
#include "sample/k_rec.h"
#include "sample/k_smp.h"
#include "sample/k_wav.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdir_p_ok(const char *d) { return mkdir(d, 0755) == 0; }

static char dir[256];

static void test_recorder(void)
{
    int16_t in[4000];
    char path[400];
    KWav w;
    int i;
    assert(k_rec_init(44100, 3000) == 0);
    assert(k_rec_state() == K_REC_IDLE);
    for (i = 0; i < 4000; i++) in[i] = 0;
    for (i = 2000; i < 4000; i++) in[i] = (int16_t)((i & 1) ? 4000 : -4000);   /* silence, then a sound */
    k_rec_input16(in, 500, 1);                       /* not started: nothing kept, the level seen */
    assert(k_rec_frames() == 0);
    assert(k_rec_start(1) == 0 && k_rec_state() == K_REC_WAIT);
    k_rec_input16(in, 2000, 1);                      /* silence: still waiting */
    assert(k_rec_state() == K_REC_WAIT && k_rec_frames() == 0);
    k_rec_input16(in + 2000, 1000, 1);
    assert(k_rec_state() == K_REC_ON);
    assert(k_rec_frames() == K_REC_PREROLL + 1000);  /* the attack kept: what came just before */
    assert(k_rec_data()[K_REC_PREROLL - 1] == 0 && k_rec_data()[K_REC_PREROLL] == -4000);
    assert(k_rec_level() == 4000 && k_rec_level() == 3000);   /* the peak, then falling back as it is read */
    k_rec_input16(in + 2000, 2000, 1);               /* past the buffer: it stops by itself, full */
    assert(k_rec_state() == K_REC_IDLE && k_rec_frames() == 3000);
    {   /* recorded at once, the silence before the sound cut when kept */
        assert(k_rec_start(0) == 0);
        k_rec_input16(in, 4000, 1);                  /* 2000 frames of silence, then the sound (stops full at 3000) */
        assert(k_rec_frames() == 3000);
        k_rec_stop();
        assert(k_rec_trim(K_REC_THRESHOLD) == 2000 - K_REC_PREROLL && k_rec_frames() == 1000 + K_REC_PREROLL);
        assert(k_rec_data()[K_REC_PREROLL] == -4000);
        assert(k_rec_trim(K_REC_THRESHOLD) == 0);    /* (again: nothing more) */
        assert(k_rec_start(0) == 0);
        k_rec_input16(in, 1500, 1);                  /* silence alone: kept whole */
        k_rec_stop();
        assert(k_rec_trim(K_REC_THRESHOLD) == 0 && k_rec_frames() == 1500);
    }
    {   /* stereo in: mixed down */
        int16_t st[4] = {1000, 3000, -2000, 0};
        assert(k_rec_start(0) == 0 && k_rec_state() == K_REC_ON);
        k_rec_input16(st, 2, 2);
        assert(k_rec_frames() == 2 && k_rec_data()[0] == 2000 && k_rec_data()[1] == -1000);
        k_rec_stop();
    }
    {   /* floats in, then written and read back */
        float f[441];
        for (i = 0; i < 441; i++) f[i] = (float)(0.1 * sin(2 * 3.14159265358979 * i / 441.0 * 4));
        assert(k_rec_start(0) == 0);
        k_rec_input(f, 441, 1);
        k_rec_stop();
        assert(k_rec_frames() == 441);
        {   /* 0.1 brought up to just under -1 dBFS (+18 dB) */
            int pk = 0;
            assert(k_rec_normalize(24) == 18);
            for (i = 0; i < 441; i++) if (abs(k_rec_data()[i]) > pk) pk = abs(k_rec_data()[i]);
            assert(pk <= 29204 && pk > 25000);
        }
        snprintf(path, sizeof path, "%s/rec.wav", dir);
        assert(k_rec_save(path) == K_WAV_OK);
        assert(k_wav_load(path, &w, 0) == K_WAV_OK);
        assert(w.nframes == 441 && w.channels == 1 && w.rate == 44100);
        for (i = 0; i < 441; i++) assert(w.frames[i] == k_rec_data()[i]);
        k_wav_free(&w);
    }
    k_rec_shutdown();
    assert(k_rec_start(0) != 0);                     /* no buffer: refused */
    puts("recorder: ok");
}

/* a WAV of a sine, `period` frames a cycle, n frames */
static void make_sine(const char *name, int period, int n, int stereo)
{
    char path[400];
    int16_t *f = malloc((size_t)n * 2 * sizeof *f);
    int i;
    for (i = 0; i < n; i++) {
        const int16_t v = (int16_t)(20000 * sin(2 * 3.14159265358979 * i / period));
        if (stereo) { f[2 * i] = v; f[2 * i + 1] = v; } else f[i] = v;
    }
    snprintf(path, sizeof path, "%s/%s", dir, name);
    assert(k_wav_write(path, f, (uint32_t)n, stereo ? 2 : 1, 44100) == K_WAV_OK);
    free(f);
}

static float out[44100];
/* track 0 plays note for n frames (held): the left channel into out */
static void play(const KApp *g, int note, int n)
{
    KAudio a;
    float buf[2 * 441];
    int i, k;
    k_audio_init(&a, 44100);
    k_audio_sync(&a, g);
    k_audio_note(&a, 0, note, 127, 1);
    for (k = 0; k < n / 441; k++) {
        k_audio_render(&a, buf, 441);
        for (i = 0; i < 441; i++) out[k * 441 + i] = buf[2 * i];
    }
}
/* the frequency in out[from .. to): upward zero crossings a second */
static double freq(int from, int to)
{
    int i, c = 0, first = -1, last = -1;
    for (i = from + 1; i < to; i++) if (out[i - 1] < 0 && out[i] >= 0) { c++; if (first < 0) first = i; last = i; }
    return c > 1 ? (c - 1) * 44100.0 / (last - first) : 0;
}
static double rms(int from, int to)
{
    double s = 0;
    int i;
    for (i = from; i < to; i++) s += (double)out[i] * out[i];
    return sqrt(s / (to - from));
}

static void test_engine(void)
{
    static KApp g;
    const int idx = k_engine_find(K_SAMPLER_UID);
    KVal *P;
    double f;
    assert(idx >= K_ENGINE_COUNT);
    assert(k_plugin_check(&K_SAMPLER_PLUGIN, 1) == 0);
    make_sine("sine441.wav", 100, 44100, 0);         /* 441 Hz, a second */
    make_sine("sine_st.wav", 100, 44100, 1);
    k_init(&g);
    g.sound[0] = idx;
    P = g.params[0][K_PAGE_SOUND] + 4;
    {   int k; for (k = 0; k < k_engine_at(idx)->nparams; k++) P[k] = k_engine_at(idx)->edit[k].def; }
    play(&g, 60, 4410);                              /* no sample: silent */
    assert(rms(0, 4410) < 1e-6);
    assert(k_smp_assign(0, "sine441.wav") == K_WAV_OK);
    assert(k_smp_sample(0) && k_smp_sample(0)->nframes == 44100 && !strcmp(k_smp_path(0), "sine441.wav"));
    play(&g, 60, 8820);                              /* at ROOT: the file's own pitch */
    f = freq(2205, 8820);
    printf("root %.1f Hz\n", f);
    assert(fabs(f - 441) < 3);
    play(&g, 72, 8820);                              /* an octave up: twice */
    f = freq(2205, 8820);
    printf("+12 %.1f Hz\n", f);
    assert(fabs(f - 882) < 5);
    play(&g, 55, 8820);                              /* a fourth down */
    f = freq(2205, 8820);
    assert(fabs(f - 441 * pow(2, -5 / 12.0)) < 3);
    P[7] = 0;                                        /* KEY off: every key at ROOT's */
    play(&g, 72, 8820);
    assert(fabs(freq(2205, 8820) - 441) < 3);
    P[7] = 1;
    P[5] = 50;                                       /* TUNE +50 ct */
    play(&g, 60, 8820);
    assert(fabs(freq(2205, 8820) - 441 * pow(2, 50 / 1200.0)) < 3);
    P[5] = 0;
    P[6] = 1;                                        /* reversed: the same tone */
    play(&g, 60, 8820);
    assert(fabs(freq(2205, 8820) - 441) < 3);
    P[6] = 0;
    {   /* a one-shot ends at END; a loop keeps sounding */
        P[1] = 3;                                    /* END at 4/128 of the second: 1378 frames */
        play(&g, 60, 13230);
        assert(rms(441, 1300) > 0.01 && rms(4410, 13230) < 1e-4);
        P[2] = 1;                                    /* LOOP FWD */
        play(&g, 60, 13230);
        assert(rms(4410, 13230) > 0.01);
        P[2] = 2;                                    /* PING */
        play(&g, 60, 13230);
        assert(rms(4410, 13230) > 0.01);
        P[1] = 127; P[2] = 0;
    }
    assert(k_smp_assign(0, "sine_st.wav") == K_WAV_OK);   /* stereo: as mono */
    play(&g, 60, 8820);
    assert(fabs(freq(2205, 8820) - 441) < 3);
    assert(k_smp_assign(0, "nothere.wav") != K_WAV_OK);  /* missing: kept by name, silent */
    assert(!strcmp(k_smp_path(0), "nothere.wav") && !k_smp_sample(0));
    play(&g, 60, 4410);
    assert(rms(0, 4410) < 1e-6);
    puts("engine: ok");
}

static void test_song(void)
{
    static KApp g, h;
    static uint8_t buf[K_SAVE_BUF];
    char name[K_SMP_NAME];
    size_t n;
    assert(k_song_max_size() <= K_SAVE_BUF);
    k_init(&g);
    g.sound[1] = k_engine_find(K_SAMPLER_UID);
    k_smp_clear_all();
    assert(k_smp_assign(1, "sine441.wav") == K_WAV_OK);
    n = k_song_save(&g, buf, sizeof buf);
    assert(n > 0);
    k_smp_clear_all();
    assert(!k_smp_sample(1));
    k_init(&h);
    assert(k_song_load(&h, buf, n) == K_SONG_OK);
    assert(h.sound[1] == g.sound[1]);
    assert(!strcmp(k_smp_path(1), "sine441.wav") && k_smp_sample(1) && !k_smp_path(0)[0]);
    {   /* the files in the folder, by name, and PRESETS' steps through them */
        char names[8][K_SMP_NAME];
        assert(k_smp_list(names, 8) == 3 && !strcmp(names[0], "rec.wav") && !strcmp(names[2], "sine_st.wav"));
        assert(k_smp_step(1, 1, name, sizeof name) == 0 && !strcmp(name, "sine_st.wav"));
        assert(k_smp_step(1, -1, name, sizeof name) == 0 && !strcmp(name, "rec.wav"));
        assert(k_smp_step(3, 1, name, sizeof name) == 0 && !strcmp(name, "rec.wav"));   /* (none yet: the first) */
    }
    k_smp_clear_all();
    n = k_song_save(&g, buf, sizeof buf);            /* no sample: no SMPL chunk, the song as before */
    assert(!memmem(buf, n, "SMPL", 4));
    puts("song: ok");
}

static void test_preset(void)   /* a Sampler's user preset keeps its file: loaded onto another track, that track plays it */
{
    static KApp g;
    static uint8_t buf[K_SAVE_BUF];
    size_t n;
    assert(k_preset_max_size() <= K_SAVE_BUF);
    k_init(&g);
    g.sound[0] = k_engine_find(K_SAMPLER_UID);
    k_smp_clear_all();
    assert(k_smp_assign(0, "sine_st.wav") == K_WAV_OK);
    n = k_preset_save(&g, 0, "AH", buf, sizeof buf);
    assert(n > 0 && n <= k_preset_max_size() && memmem(buf, n, "SMPL", 4));
    assert(k_preset_load(&g, 2, buf, n) == K_SONG_OK);
    assert(g.sound[2] == g.sound[0] && !strcmp(k_smp_path(2), "sine_st.wav") && k_smp_sample(2));
    g.sound[1] = 0;   /* (another engine's preset: no file in it, and the track's sample left alone) */
    assert(k_smp_assign(3, "rec.wav") == K_WAV_OK);
    n = k_preset_save(&g, 1, "RT", buf, sizeof buf);
    assert(n > 0 && !memmem(buf, n, "SMPL", 4));
    assert(k_preset_load(&g, 3, buf, n) == K_SONG_OK && !strcmp(k_smp_path(3), "rec.wav"));
    k_smp_clear_all();
    puts("preset: ok");
}

int main(void)
{
    snprintf(dir, sizeof dir, "/tmp/kreisel_sampler_test_%d", (int)getpid());
    assert(mkdir_p_ok(dir));
    k_smp_set_dir(dir);
    assert(k_plugin_register_host(&K_SAMPLER_PLUGIN) >= K_ENGINE_COUNT);
    assert(k_plugin_register_host(&K_SAMPLER_PLUGIN) == K_PLUG_E_TAKEN);
    test_recorder();
    test_engine();
    test_song();
    test_preset();
    {   char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir); if (system(cmd) != 0) return 1; }
    puts("sampler_test: ok");
    return 0;
}
