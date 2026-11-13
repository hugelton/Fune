/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel_audio.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(KAudio *a, size_t frames)
{
    float buf[2 * 257];
    while(frames) {
        size_t n = frames > 257 ? 257 : frames;
        k_audio_render(a, buf, n);
        frames -= n;
    }
}


/* ---- helpers for the modulation tests: a held note on track 0 (ANALOG), the left channel, 44100 Hz ---- */
#define MR 44100
#define MSECS 3
static float g_mod[MR * MSECS];
static void mod_setup(KApp *g, int wave)
{
    k_init(g);
    g->sound[0] = 0;
    g->params[0][K_PAGE_SOUND][8] = wave;                    /* ANALOG WAVE1: 0 SAW, 3 SIN */
    g->params[0][K_PAGE_SOUND][16] = 127;                    /* CUT open */
    g->params[0][K_PAGE_SOUND][9] = 127;                     /* LVL1 full */
    g->params[0][K_PAGE_SOUND][11] = 0;                      /* LVL2: OSC 2 silent (it would beat with the first) */
    g->params[0][K_PAGE_SOUND][14] = 0;                      /* NOIS: none */
}
/* a note held, and at `at` seconds another one on before it is let go (a sequencer's notes, end to end): legato */
static void mod_play_over(const KApp *g, int first, int second, double at)
{
    KAudio a;
    float buf[2 * 441];
    int i, n;
    k_audio_init(&a, MR);
    k_audio_sync(&a, g);
    k_audio_note(&a, 0, first, 127, 1);
    for(n = 0; n < MR * MSECS / 441; ++n) {
        if(n == (int)(at * MR / 441)) { k_audio_note(&a, 0, second, 127, 1); k_audio_note(&a, 0, first, 0, 0); }
        k_audio_render(&a, buf, 441);
        for(i = 0; i < 441; ++i) g_mod[n * 441 + i] = buf[2 * i];
    }
}
static void mod_play(const KApp *g, int note)
{
    KAudio a;
    float buf[2 * 441];
    int i, n;
    k_audio_init(&a, MR);
    k_audio_sync(&a, g);
    k_audio_note(&a, 0, note, 127, 1);
    for(n = 0; n < MR * MSECS / 441; ++n) {
        k_audio_render(&a, buf, 441);
        for(i = 0; i < 441; ++i) g_mod[n * 441 + i] = buf[2 * i];
    }
}
/* the frequency between t0 and t1 (s): positive-going zero crossings (re-armed below -0.01, so the last bit of quantisation does not
 * count twice), the first to the last, each placed by linear interpolation */
static double mod_freq(double t0, double t1)
{
    int i, n = 0, armed = 0, a = (int)(t0 * MR), b = (int)(t1 * MR);
    double first = 0, last = 0;
    for(i = a + 1; i < b; ++i) {
        if(g_mod[i] < -.01f) armed = 1;
        if(armed && g_mod[i - 1] < 0.f && g_mod[i] >= 0.f) {
            double x = i - 1 + (double)(-g_mod[i - 1]) / ((double)g_mod[i] - g_mod[i - 1]);
            if(!n) first = x;
            last = x;
            ++n;
            armed = 0;
        }
    }
    return n > 1 ? (n - 1) * (double)MR / (last - first) : 0.;
}
static double mod_rms(double t0, double t1)
{
    int i, a = (int)(t0 * MR), b = (int)(t1 * MR);
    double s = 0;
    for(i = a; i < b; ++i) s += (double)g_mod[i] * g_mod[i];
    return sqrt(s / (b - a));
}
/* brightness: the RMS of the second difference over the RMS (a low-passed saw has little high end) */
static double mod_bright(double t0, double t1)
{
    int i, a = (int)(t0 * MR), b = (int)(t1 * MR);
    double d = 0;
    for(i = a + 2; i < b; ++i) { double x = (double)g_mod[i] - 2. * g_mod[i - 1] + g_mod[i - 2]; d += x * x; }
    return sqrt(d / (b - a)) / (mod_rms(t0, t1) + 1e-9);
}
/* the extremes of f over 20 ms windows between t0 and t1 */
static void mod_range(double (*f)(double, double), double t0, double t1, double *lo, double *hi)
{
    double t;
    *lo = 1e30; *hi = -1e30;
    for(t = t0; t + .02 <= t1; t += .02) { double v = f(t, t + .02); if(v < *lo) *lo = v; if(v > *hi) *hi = v; }
}

/* MASTER outside the render (the FM-1: its USB audio at full level, the knob on the headphones only): with it, the render at
 * MASTER 0 is the render at MASTER 100, and k_audio_master_q12 still says 0 for the platform to apply */
static void test_master_outside(void)
{
    static KApp g;
    KAudio a, b;
    int32_t x[2 * 256], y[2 * 256];
    int i, loud = 0;
    k_init(&g);   /* (one KAudio at a time: the engines' state is the file's) */
    g.master = 0;
    k_audio_init(&a, 44100); k_audio_master_outside(&a, 1); k_audio_sync(&a, &g);
    k_audio_note(&a, 0, 60, 100, 1);
    for (i = 0; i < 8; i++) k_audio_render_s32(&a, x, 256);
    g.master = 100;
    k_audio_init(&b, 44100); k_audio_sync(&b, &g);
    k_audio_note(&b, 0, 60, 100, 1);
    for (i = 0; i < 8; i++) k_audio_render_s32(&b, y, 256);
    assert(!memcmp(x, y, sizeof x));
    for (i = 0; i < 2 * 256; i++) if (x[i] > (1 << 14) || x[i] < -(1 << 14)) loud = 1;
    assert(loud && k_audio_master_q12(&a) == 0 && k_audio_master_q12(&b) == 4096);
}

int main(void)
{
    test_master_outside();
    KApp project;
    KAudio a, b;
    float test[2 * 512];
    int audible = 0;
    size_t i;

    k_init(&project);
    k_audio_init(&a, 48000);
    k_audio_sync(&a, &project);
    k_audio_render(&a, test, 512);
    for(i = 0; i < 1024; ++i) assert(test[i] == 0.f);
    assert(a.current_step == -1 && a.triggers[0] == 0);

    /* First event begins at the first audio frame, not at a UI or SDL timer tick.
     * At 120 BPM a sixteenth = 6,000 samples and the default hits are 0/4/8/12. */
    project.playing = 1;
    k_audio_sync(&a, &project);
    k_audio_render(&a, test, 1);
    assert(a.current_step == 0 && a.triggers[0] == 1 && a.last_trigger_frame[0] == 512);
    advance(&a, 5999);
    assert(a.current_step == 0 && a.triggers[0] == 1);
    k_audio_render(&a, test, 1);
    assert(a.current_step == 1 && a.triggers[0] == 1);
    advance(&a, 18000 - 1);
    assert(a.triggers[0] == 1);
    k_audio_render(&a, test, 1);
    assert(a.current_step == 4 && a.triggers[0] == 2);
    assert(a.last_trigger_frame[0] == 512 + 24000);
    k_audio_render(&a, test, 512);
    for(i = 0; i < 1024; ++i)
        if(fabsf(test[i]) > .00001f) { audible = 1; break; }
    assert(audible);

    /* No new notes after STOP, and the voices fade instead of being chopped. */
    project.playing = 0;
    k_audio_sync(&a, &project);
    assert(a.current_step == -1);
    i = a.triggers[0];
    advance(&a, 48000);
    assert(a.triggers[0] == i && !a.playing);

    /* Chunking and callback size cannot change timing or output. */
    {
        const size_t frames = 25123;
        float *all = (float *)malloc(sizeof(float) * 2 * frames);
        float *chunks = (float *)malloc(sizeof(float) * 2 * frames);
        size_t offset = 0;
        assert(all && chunks);
        k_audio_init(&a, 48000); k_audio_init(&b, 48000);
        project.playing = 1;
        k_audio_sync(&a, &project); k_audio_sync(&b, &project);
        k_audio_render(&a, all, frames);
        while(offset < frames) {
            size_t n = (offset % 337) + 1;
            if(n > frames - offset) n = frames - offset;
            k_audio_render(&b, chunks + 2 * offset, n);
            offset += n;
        }
        assert(memcmp(all, chunks, sizeof(float) * 2 * frames) == 0);
        assert(a.frame == b.frame && a.triggers[0] == 2 && b.triggers[0] == 2);
        free(all); free(chunks);
    }

    /* A tempo change is taken at the next step boundary; swing preserves a pair's duration. */
    {
        uint64_t last;
        k_audio_init(&a, 48000);
        project.bpm = 120; project.swing = 50; k_roll_fill(&project, 0, 0, 0xFFFFu, 60);
        k_audio_sync(&a, &project);
        k_audio_render(&a, test, 1);
        advance(&a, 7200 - 1);   /* 6,000 * (1 + 0.2) */
        k_audio_render(&a, test, 1);
        assert(a.current_step == 1 && a.last_trigger_frame[0] == 7200);
        advance(&a, 4800 - 1);   /* 6,000 * (1 - 0.2) */
        k_audio_render(&a, test, 1);
        assert(a.current_step == 2 && a.last_trigger_frame[0] == 12000);
        last = a.triggers[0];
        project.bpm = 240; project.swing = 0;
        k_audio_sync(&a, &project);
        advance(&a, 7200 - 1);   /* step 2's already scheduled 7,200-frame length */
        k_audio_render(&a, test, 1);
        assert(a.current_step == 3 && a.triggers[0] == last + 1);
        advance(&a, 3000 - 1);
        assert(a.current_step == 3);
        k_audio_render(&a, test, 1);
        assert(a.current_step == 4);
    }

    /* A drum step emits every enabled lane, with no note-on for disabled lanes. */
    {
        int lane;
        k_audio_init(&a, 48000);
        project.sound[0] = k_engine_find(K_UID('D','R','U','M'));
        k_roll_fill(&project, 0, 0, 0, 60);
        for(lane = 0; lane < K_LANES; ++lane) project.lane_on[0][lane] = 0;
        project.lane_on[0][0] = 1;
        project.lane_on[0][1] = 1;
        k_audio_sync(&a, &project);
        k_audio_render(&a, test, 1);
        assert(a.current_step == 0 && a.triggers[0] == 2);
        assert(a.voices[0] == 2);                          /* both lanes sound */
    }

    /* Every synth engine plays through its own DSP, with its SOUND parameters; the sound differs between engines. */
    {
        int e, t;
        double energy[K_ENGINE_COUNT];
        for(e = 0; e < K_ENGINE_COUNT; ++e) {
            KApp g;
            float buf[2 * 4096];
            double sum = 0;
            int n;
            k_init(&g);
            for(t = 0; t < K_TRACKS; ++t) g.sound[t] = e;
            for(t = 0; t < K_TRACKS; ++t) { int i; for(i = 0; i < k_engine_at(e)->nparams; ++i) g.params[t][K_PAGE_SOUND][4 + i] = k_engine_at(e)->edit[i].def; }
            g.playing = 1;
            k_roll_fill(&g, 0, 0, 0x1, 60);
            g.lane_on[0][0] = 1;
            k_audio_init(&a, 44100);
            k_audio_sync(&a, &g);
            for(n = 0; n < 8; ++n) {
                size_t i;
                k_audio_render(&a, buf, 4096);
                for(i = 0; i < 8192; ++i) { assert(fabsf(buf[i]) <= 1.f); sum += (double)buf[i] * buf[i]; }
            }
            energy[e] = sum;
            assert(sum > 1e-3);                              /* it sounds */
        }
        for(e = 1; e < K_ENGINE_COUNT; ++e) assert(fabs(energy[e] - energy[e - 1]) > 1e-6);
    }

    /* a chord on a step: its notes all sound (three of them: more than one note's energy, the pitches apart) */
    {
        double en[2];
        int c;
        for(c = 0; c < 2; ++c) {
            KApp g;
            float buf[2 * 4096];
            double sum = 0;
            int n;
            k_init(&g);
            g.sound[0] = k_engine_find(K_UID('K', 'A', 'N', 'L'));
            k_roll_fill(&g, 0, 0, 0, 60);
            k_roll_add(&g, 0, 0, 0, 48, 4, 100);
            if(c) { k_roll_add(&g, 0, 0, 0, 55, 4, 100); k_roll_add(&g, 0, 0, 0, 64, 4, 100); }
            assert(k_roll_count(&g, 0, 0, 0) == (c ? 3 : 1));
            g.playing = 1;
            k_audio_init(&a, 44100);
            k_audio_sync(&a, &g);
            for(n = 0; n < 4; ++n) {
                size_t i;
                k_audio_render(&a, buf, 4096);
                for(i = 0; i < 8192; ++i) { assert(fabsf(buf[i]) <= 1.f); sum += (double)buf[i] * buf[i]; }
            }
            en[c] = sum;
        }
        printf("chord: one note %.3f, three %.3f\n", en[0], en[1]);
        assert(en[0] > 1e-3 && en[1] > en[0] * 1.8);
    }

    /* a song (PLAY on SONG): a bar a scene, the patterns its cells say (4: silent), followed on the screen, stopped after the last */
    {
        KApp g;
        float buf[2 * 4410];
        int i, t, saw_b = 0;
        k_init(&g);
        for(t = 0; t < K_TRACKS; t++) { k_roll_fill(&g, t, 0, 0, 60); g.sound[t] = k_engine_find(K_UID('K', 'A', 'N', 'L')); }
        k_roll_fill(&g, 0, 0, 0x0001, 60);   /* pattern A: a note on step 1 */
        k_roll_fill(&g, 0, 1, 0x0101, 72);   /* pattern B: two */
        memset(g.song, 4, sizeof g.song);     /* every cell silent but: */
        g.song[0][0] = 0; g.song[1][0] = 1;   /* scene 1 pattern A, scene 2 pattern B: two bars */
        g.bpm = 120; k_open_page(&g, K_PAGE_SONG);
        k_dispatch(&g, (KAction){K_ACT_PLAY, 0, 0, 1000});
        assert(g.playing && g.song_play && g.song_scene == 0);
        k_audio_init(&a, 44100);
        for(i = 0; i < 60 && g.playing; i++) {   /* 0.1 s a block: two bars at 120 are 4 s */
            k_audio_sync(&a, &g);
            k_audio_render(&a, buf, 4410);
            k_audio_song_follow(&a, &g);
            if(g.song_scene == 1 && g.pattern[0] == 1) saw_b = 1;
        }
        printf("song: two scenes, stopped after %.1f s, triggers %llu\n", i * 0.1, (unsigned long long)a.triggers[0]);
        assert(saw_b && !g.playing && !g.song_play && i >= 39 && i <= 42 && a.triggers[0] == 3);   /* A's one note, B's two */
        k_open_page(&g, K_PAGE_HOME); k_dispatch(&g, (KAction){K_ACT_PLAY, 0, 0, 9000});   /* elsewhere: the pattern loops, no song */
        assert(g.playing && !g.song_play);
    }

    /* the system's voices: four tracks of three-note chords are twelve; capped at five, five sound */
    {
        KApp g;
        float buf[2 * 2048];
        int t, n;
        k_init(&g);
        for(t = 0; t < 4 && t < K_TRACKS; t++) {
            g.sound[t] = k_engine_find(K_UID('K', 'A', 'N', 'L'));
            k_roll_fill(&g, t, 0, 0, 60);
            for(n = 0; n < 3; n++) k_roll_add(&g, t, 0, 0, 48 + t * 2 + n * 4, 8, 100);
        }
        g.playing = 1;
        k_audio_init(&a, 44100);
        k_audio_sys_voices(5);
        k_audio_sync(&a, &g);
        k_audio_render(&a, buf, 2048);
        printf("system voices: %d sounding of 12 (cap 5)\n", k_audio_voices_on());
        assert(k_audio_voices_on() == 5);
        k_audio_sys_voices(K_SYS_VOICES);
        k_audio_init(&a, 44100); k_audio_sync(&a, &g); k_audio_render(&a, buf, 2048);
        assert(k_audio_voices_on() == (K_SYS_VOICES < 12 ? K_SYS_VOICES : 12));
        {   /* a voice given way fades out over a few blocks (halved each), not cut: a held note, then a note (almost silent) on another track takes it */
            KApp h; KAudio b; float o[2 * 256]; double early = 0, late = 0; int i;
            k_init(&h); h.sound[0] = h.sound[1] = k_engine_find(K_UID('K', 'A', 'N', 'L'));
            k_roll_fill(&h, 0, 0, 0, 60);
            k_audio_init(&b, 44100); k_audio_sys_voices(1); k_audio_sync(&b, &h);
            k_audio_note(&b, 0, 48, 127, 1);
            k_audio_render(&b, buf, 2048);
            k_audio_note(&b, 1, 72, 1, 1);
            assert(k_audio_voices_on() == 1);   /* (the one fading is not counted) */
            k_audio_render(&b, o, 256);
            for(i = 0; i < 8; i++) early += fabs(o[2 * i]);
            for(i = 128; i < 136; i++) late += fabs(o[2 * i]);
            printf("steal: |out| first 8 frames %.4f, frames 128.. %.4f\n", early / 8, late / 8);
            {   double second = 0; for(i = 40; i < 48; i++) second += fabs(o[2 * i]);
                printf("steal: frames 40.. %.4f (the second block: still fading)\n", second / 8);
                assert(second > 1e-4 && second < early); }
            assert(early > 4 * late && early > 1e-3);   /* still sounding as it fades, gone after */
            k_audio_sys_voices(K_SYS_VOICES);
        }
    }

    /* ENSEMBLE: chorus is post-polyphonic and must audibly change the sound
     * while keeping samples finite. Reinitialising restores the same phases. (Only where it is built in.) */
#if K_WITH_ENSEMBLE
    {
        KApp g;
        float dry[2 * 256], buf[2 * 256];
        double diff = 0, power = 0;
        int e = k_engine_find(K_UID('K', 'E', 'N', 'S')), c, j;
        assert(e >= 0);
        k_init(&g);
        g.sound[0] = e;
        for(j = 0; j < k_engine_at(e)->nparams; j++)
            g.params[0][K_PAGE_SOUND][4 + j] = k_engine_at(e)->edit[j].def;
        for(c = 0; c < 2; c++) {
            g.params[0][K_PAGE_SOUND][4 + 6] = c ? 127 : 0; /* ENS off/on */
            k_audio_init(&a, 44100);
            k_audio_sync(&a, &g);
            k_audio_note(&a, 0, 60, 127, 1);
            advance(&a, 2048);
            k_audio_render(&a, buf, 256);
            for(j = 0; j < 512; j++) {
                assert(isfinite(buf[j]) && fabsf(buf[j]) <= 1.f);
                if(c) {
                    diff += fabs((double)dry[j] - buf[j]);
                    power += (double)buf[j] * buf[j];
                } else dry[j] = buf[j];
            }
        }
        assert(diff > 0.01 && power > 1e-5);
    }
#else
    assert(k_engine_find(K_UID('K', 'E', 'N', 'S')) == -1);   /* out of the MVP: not here */
#endif
    /* FM2: nonzero index changes the carrier, and output LEVEL=0 is silent. */
    {
        KApp g;
        KAudio fm;
        float pure[2 * 2048], modulated[2 * 2048], quiet[2 * 2048];
        double diff = 0., loud = 0.;
        int e = k_engine_find(K_UID('K', 'F', 'M', '2'));
        assert(e >= 0 && k_engine_at(e)->nparams == 12);
        k_init(&g);
        g.sound[0] = e;
        for (int j = 0; j < 12; j++) g.params[0][K_PAGE_SOUND][4 + j] = k_engine_at(e)->edit[j].def;
        g.params[0][K_PAGE_SOUND][4 + 4] = 0;   /* INDEX */
        k_audio_init(&fm, 44100);
        k_audio_sync(&fm, &g);
        k_audio_note(&fm, 0, 60, 127, 1);
        k_audio_render(&fm, pure, 2048);
        g.params[0][K_PAGE_SOUND][4 + 4] = 100;
        k_audio_init(&fm, 44100);
        k_audio_sync(&fm, &g);
        k_audio_note(&fm, 0, 60, 127, 1);
        k_audio_render(&fm, modulated, 2048);
        for (size_t j = 0; j < 4096; j++) {
            assert(isfinite(modulated[j]) && fabsf(modulated[j]) <= 1.f);
            diff += fabs((double)pure[j] - modulated[j]);
            loud += fabs((double)modulated[j]);
        }
        assert(diff > .1 && loud > .1);
        g.params[0][K_PAGE_SOUND][4 + 10] = 0;   /* LEVEL */
        k_audio_init(&fm, 44100);
        k_audio_sync(&fm, &g);
        k_audio_note(&fm, 0, 60, 127, 1);
        k_audio_render(&fm, quiet, 2048);
        for (size_t j = 0; j < 4096; j++) assert(quiet[j] == 0.f);
    }

    /* A drum lane's PAN: hard left leaves the right side silent; at the centre both sides as loud as each other */
    {
        KApp g;
        KAudio da;
        float buf[2 * 2048];
        double l = 0, r = 0;
        int d = k_engine_find(K_UID('D', 'R', 'U', 'M')), j;
        for (int side = 0; side < 2; side++) {
            k_init(&g); g.sound[0] = d; g.params[0][K_PAGE_MIXER][1] = 50;   /* (the track itself in the middle) */
            g.lane_p[0][0][K_LANE_PAN] = side ? 50 : 0;
            k_audio_init(&da, 44100); k_audio_sync(&da, &g);
            k_audio_note(&da, 0, 0, 127, 1);   /* lane 1: the kick */
            k_audio_render(&da, buf, 2048);
            l = r = 0;
            for (j = 0; j < 2048; j++) { l += fabs(buf[2 * j]); r += fabs(buf[2 * j + 1]); }
            if (!side) assert(l > 1.0 && r < l * 0.01);
            else assert(l > 1.0 && fabs(l - r) < l * 0.02);
        }
    }

    /* Every drum piece: its name is the DRUM engine's PIECE name, it sounds when struck, stays in range, and ends (open hats and
     * cymbals within a few seconds) */
    {
        const KEngine *dr = &K_ENGINE[k_engine_find(K_UID('D','R','U','M'))];
        int pc;
        assert(dr->edit[0].max == K_DRUM_PIECES - 1);
        for(pc = 0; pc < K_DRUM_PIECES; ++pc) {
            KApp g;
            float buf[2 * 4096];
            double sum = 0, tail = 0;
            int n;
            assert(!strcmp(dr->edit[0].names[pc], k_audio_drum_piece(pc)));
            k_init(&g);
            g.sound[0] = k_engine_find(K_UID('D','R','U','M'));
            g.lane_p[0][0][0] = pc;
            k_audio_init(&a, 44100);
            k_audio_sync(&a, &g);
            k_audio_note(&a, 0, 0, 100, 1);
            for(n = 0; n < 40; ++n) {   /* 3.7 s */
                size_t i;
                k_audio_render(&a, buf, 4096);
                for(i = 0; i < 8192; ++i) {
                    assert(fabsf(buf[i]) <= 1.f);
                    if(n < 4) sum += (double)buf[i] * buf[i]; else if(n >= 36) tail += (double)buf[i] * buf[i];
                }
            }
            if(!(sum > 1e-4 && tail < sum * 1e-3)) printf("drum piece %s: first %g, tail %g\n", k_audio_drum_piece(pc), sum, tail);
            assert(sum > 1e-4);
            assert(tail < sum * 1e-3);
        }
    }

    /* A drum lane's ENV (attack, a deep long sweep, a click), the kit's HUMN and DCAY, the LFO on the pitch: in range, and ending */
    {
        KApp g;
        float buf[2 * 4096];
        double sum = 0, tail = 0;
        int n;
        k_init(&g);
        g.sound[0] = k_engine_find(K_UID('D','R','U','M'));
        g.lane_p[0][0][K_LANE_ENV] = 100; g.lane_p[0][0][K_LANE_ENV + 1] = 100; g.lane_p[0][0][K_LANE_ENV + 2] = 100; g.lane_p[0][0][K_LANE_ENV + 3] = 100;
        g.params[0][K_PAGE_SOUND][4 + 13] = 127; g.params[0][K_PAGE_SOUND][4 + 11] = 127;   /* HUMN, KDCY */
        g.params[0][K_PAGE_LFO][4] = 63;   /* LFO -> pitch, all lanes */
        k_audio_init(&a, 44100);
        k_audio_sync(&a, &g);
        k_audio_note(&a, 0, 0, 100, 1);
        for(n = 0; n < 60; ++n) {
            size_t i;
            k_audio_render(&a, buf, 4096);
            for(i = 0; i < 8192; ++i) { assert(fabsf(buf[i]) <= 1.f); if(n < 4) sum += (double)buf[i] * buf[i]; else if(n >= 56) tail += (double)buf[i] * buf[i]; }
        }
        assert(sum > 1e-4 && tail < sum * 1e-3);
    }

    /* A drum hit that finds the pool full takes the oldest voice's slot: that voice falls away (its tail) instead of being cut,
     * so the output takes no step. Twelve long congas (the pool), then a thirteenth at LVL 0 (it adds nothing): the difference
     * from the same run without it is the stolen voice going, which starts near 0 and moves no faster than the conga itself. */
    {
        static KApp g; static KAudio sa; static float sb[2][2 * 2048], pre[2 * 4400];
        int cng = -1, pc, run, lane, tk, i;
        double dmax = 0, step = 0, prev = 0;
        for(pc = 0; pc < K_DRUM_PIECES; ++pc) if(!strcmp(k_audio_drum_piece(pc), "T8 CNG")) cng = pc;
        assert(cng >= 0);
        for(run = 0; run < 2; ++run) {
            k_init(&g);
            for(tk = 0; tk < 2; ++tk) {
                g.sound[tk] = k_engine_find(K_UID('D','R','U','M'));
                g.params[tk][K_PAGE_SOUND][4 + 11] = 127;   /* KDCY: long */
                for(lane = 0; lane < K_LANES; ++lane) { g.lane_p[tk][lane][0] = cng; g.lane_p[tk][lane][3] = 127; g.lane_p[tk][lane][4] = 30; }
            }
            g.lane_p[1][4][4] = 0;
            k_audio_init(&sa, 44100); k_audio_sync(&sa, &g);
            for(lane = 0; lane < K_LANES; ++lane) k_audio_note(&sa, 0, lane, 100, 1);
            for(lane = 0; lane < 4; ++lane) k_audio_note(&sa, 1, lane, 100, 1);
            k_audio_render(&sa, pre, 4400);   /* (a whole number of blocks; its own buffer: sb holds 2048) */
            if(run) k_audio_note(&sa, 1, 4, 100, 1);
            k_audio_render(&sa, sb[run], 2048);
        }
        for(i = 0; i < 2048; ++i) {
            const double d = (double)sb[1][2 * i] - sb[0][2 * i];
            if(fabs(d) > dmax) dmax = fabs(d);
            if(fabs(d - prev) > step) step = fabs(d - prev);
            prev = d;
        }
        if(!(dmax > 1e-3 && step < dmax * .15)) printf("drum steal: the stolen voice %g, its largest step %g\n", dmax, step);
        assert(dmax > 1e-3 && step < dmax * .15);
    }

    /* PHASE: a retrigger of a sounding note (the same voice) keeps its DCW where it is and attacks from there; it does not fall
     * to 0 (a sine) and rise again. A slow DCW attack, a saw opened all the way: just after the retrigger its brightness is as
     * before (with the DCW thrown back to a sine it was 3.5 times this measure's value) */
    {
        static KApp g; static KAudio pa; static float pb[2 * 441];
        int e = k_engine_find(K_UID('K', 'P', 'H', 'S')), j, n;
        double br[2];
        assert(e >= 0);
        k_init(&g); g.sound[0] = e;
        for(j = 0; j < k_engine_at(e)->nparams; j++) g.params[0][K_PAGE_SOUND][4 + j] = k_engine_at(e)->edit[j].def;
        g.params[0][K_PAGE_SOUND][4 + 4] = 0; g.params[0][K_PAGE_SOUND][4 + 6] = 127;    /* WAVE1 SAW, DCW1 full */
        g.params[0][K_PAGE_SOUND][4 + 11] = 0; g.params[0][K_PAGE_SOUND][4 + 16] = 0;    /* DCA2: line 2 silent; KTR none */
        g.params[0][K_PAGE_ENV1][0] = 60; g.params[0][K_PAGE_ENV1][2] = 100;            /* DCW: a slow attack, full sustain */
        g.params[0][K_PAGE_ENV2][0] = 0; g.params[0][K_PAGE_ENV2][2] = 100;             /* DCA: at once, full */
        k_audio_init(&pa, MR); k_audio_sync(&pa, &g);
        k_audio_note(&pa, 0, 45, 127, 1);
        for(n = 0; n < 300; ++n) {   /* 3 s: the DCW open */
            k_audio_render(&pa, pb, 441);
            if(n >= 296) for(j = 0; j < 441; ++j) g_mod[(n - 296) * 441 + j] = pb[2 * j];
        }
        br[0] = mod_bright(0, 4 * 441.0 / MR);
        k_audio_note(&pa, 0, 45, 127, 1);   /* the same note again: the same voice */
        for(n = 0; n < 4; ++n) { k_audio_render(&pa, pb, 441); for(j = 0; j < 441; ++j) g_mod[n * 441 + j] = pb[2 * j]; }
        br[1] = mod_bright(0, 4 * 441.0 / MR);
        printf("phase: brightness %.4f before a retrigger, %.4f just after\n", br[0], br[1]);
        assert(br[0] > 0 && fabs(br[1] / br[0] - 1.) < .25);
    }

    /* PHASE's fold at its extremes (both lines full, SUB, NOIS, FOLD all the way): in range and sounding */
    {
        static KApp g; static KAudio pa; static float pb[2 * 4096];
        int e = k_engine_find(K_UID('K', 'P', 'H', 'S')), j, n;
        double sum = 0;
        k_init(&g); g.sound[0] = e;
        for(j = 0; j < k_engine_at(e)->nparams; j++) g.params[0][K_PAGE_SOUND][4 + j] = k_engine_at(e)->edit[j].def;
        g.params[0][K_PAGE_SOUND][4 + 7] = g.params[0][K_PAGE_SOUND][4 + 11] = 127;   /* DCA1, DCA2 */
        g.params[0][K_PAGE_SOUND][4 + 12] = g.params[0][K_PAGE_SOUND][4 + 13] = g.params[0][K_PAGE_SOUND][4 + 14] = 127;   /* SUB FOLD NOIS */
        g.params[0][K_PAGE_SOUND][4 + 6] = g.params[0][K_PAGE_SOUND][4 + 10] = 127;
        k_audio_init(&pa, MR); k_audio_sync(&pa, &g);
        k_audio_note(&pa, 0, 60, 127, 1);
        for(n = 0; n < 8; ++n) {
            k_audio_render(&pa, pb, 4096);
            for(j = 0; j < 2 * 4096; ++j) { assert(isfinite(pb[j]) && fabsf(pb[j]) <= 1.f); sum += fabs(pb[j]); }
        }
        assert(sum > 1.0);
    }

    /* LOFI's vibrato: ~6 Hz. The pitch, over 10 ms windows, rises through its middle six times a second */
    {
        static KApp g;
        int e = k_engine_find(K_UID('K', 'L', 'O', 'F')), j, ups = 0;
        double f[200], mid = 0;
        assert(e >= 0); {
            k_init(&g); g.sound[0] = e;
            for(j = 0; j < k_engine_at(e)->nparams; j++) g.params[0][K_PAGE_SOUND][4 + j] = k_engine_at(e)->edit[j].def;
            g.params[0][K_PAGE_SOUND][4 + 0] = 1; g.params[0][K_PAGE_SOUND][4 + 5] = 15;   /* the triangle, VIB full */
            g.params[0][K_PAGE_SOUND][4 + 4] = 0; g.params[0][K_PAGE_SOUND][4 + 6] = 0; g.params[0][K_PAGE_SOUND][4 + 9] = 0;   /* no sweep, arp, noise */
            mod_play(&g, 69);
            for(j = 0; j < 200; ++j) { f[j] = mod_freq(.5 + j * .01, .5 + j * .01 + .01); mid += f[j] / 200; }
            for(j = 1; j < 200; ++j) ups += f[j - 1] < mid && f[j] >= mid;
            printf("lofi: vibrato %d rises in 2 s\n", ups);
            assert(ups >= 10 && ups <= 14);
        }
    }

    /* A track whose plugin is missing plays a plain sine (its step and a live note alike), its values kept: not ANALOG's sound */
    {
        double sum[2];
        int miss;
        for(miss = 0; miss < 2; ++miss) {
            KApp g;
            float buf[2 * 4096];
            int n;
            k_init(&g);
            g.playing = 1;
            k_roll_fill(&g, 0, 0, 0xFFFF, 60);
            if(miss) g.missing[0].uid = K_UID('f', 'u', 'z', 'z');
            k_audio_init(&a, 44100);
            k_audio_sync(&a, &g);
            k_audio_note(&a, 0, 60, 100, 1);
            sum[miss] = 0;
            for(n = 0; n < 8; ++n) {
                size_t i;
                k_audio_render(&a, buf, 4096);
                for(i = 0; i < 8192; ++i) { assert(fabsf(buf[i]) <= 1.f); sum[miss] += (double)buf[i] * buf[i]; }
            }
        }
        assert(sum[1] > 1e-3 && fabs(sum[1] - sum[0]) > 1e-3);
    }

    /* A live note sounds until its off, then releases; and the integer renderer is the float one, scaled. */
    {
        KApp g;
        float f[2 * 2048];
        int32_t q[2 * 2048];
        double peak = 0;
        size_t i;
        k_init(&g);
        g.sound[0] = 0;
        k_audio_init(&a, 44100); k_audio_init(&b, 44100);
        k_audio_sync(&a, &g);
        k_audio_note(&a, 0, 57, 100, 1);
        k_audio_render(&a, f, 2048);
        for(i = 0; i < 4096; ++i) if(fabs(f[i]) > peak) peak = fabs(f[i]);
        assert(peak > .02 && a.voices[0] == 1);
        k_audio_render(&a, f, 2048);
        assert(a.voices[0] == 1);                            /* held */
        k_audio_note(&a, 0, 57, 0, 0);
        for(i = 0; i < 40; ++i) k_audio_render(&a, f, 2048);
        assert(a.voices[0] == 0);
        k_audio_sync(&b, &g);
        k_audio_note(&b, 0, 57, 100, 1);
        k_audio_render_s32(&b, q, 2048);
        k_audio_init(&a, 44100);
        k_audio_sync(&a, &g);
        k_audio_note(&a, 0, 57, 100, 1);
        k_audio_render(&a, f, 2048);
        for(i = 0; i < 4096; ++i) assert(q[i] == (int32_t)lrintf(f[i] * 32768.f) * 128);
    }

    /* A rate other than the tables' keeps the pitch: the same note, counted in zero crossings per second. */
    {
        KApp g;
        float f[2 * 48000];
        int crossings[2], r;
        size_t i;
        k_init(&g);
        g.params[0][K_PAGE_SOUND][8] = 3;                    /* ANALOG: OSC 1 a sine ... */
        g.params[0][K_PAGE_SOUND][11] = 0;                   /* ... OSC 2 silent */
        g.params[0][K_PAGE_SOUND][16] = 127;                 /* open filter */
        for(r = 0; r < 2; ++r) {
            const uint32_t rate = r ? 48000u : 44100u;
            int n = 0;
            k_audio_init(&a, rate);
            k_audio_sync(&a, &g);
            k_audio_note(&a, 0, 69, 127, 1);
            k_audio_render(&a, f, rate);
            for(i = 1; i < rate; ++i) if(f[2 * i - 2] < 0.f && f[2 * i] >= 0.f) ++n;
            crossings[r] = n;
        }
        assert(crossings[0] > 430 && crossings[0] < 450 && abs(crossings[0] - crossings[1]) <= 2);
    }


    /* ENV 2 (DLY HOLD CRV AMT), to pitch: with AMT at 50 it does nothing; at 100 it lifts the note (ENV DEST PIT +100 is ~ +12 st at
     * full) while it holds, not before its delay, and lets go after its fall. */
    {   /* MIDI pitch bend (k_bend_in): up full is two semitones, half up one, centre none */
        KApp g;
        double f0, f2, f1;
        mod_setup(&g, 3);
        mod_play(&g, 57); f0 = mod_freq(.5, 1.0);
        k_bend_in(&g, 16383); mod_play(&g, 57); f2 = mod_freq(.5, 1.0);
        k_bend_in(&g, 8192 + 4096); mod_play(&g, 57); f1 = mod_freq(.5, 1.0);
        printf("bend: %.1f Hz, +2 st %.1f, +1 st %.1f\n", f0, f2, f1);
        assert(fabs(f2 / f0 - 1.12246) < .003 && fabs(f1 / f0 - 1.05946) < .003);
    }
    {
        KApp g;
        double fn, fh, fd, ff, fe, fl;
        mod_setup(&g, 3);
        g.params[0][K_PAGE_ENV_DEST][0] = 100;               /* PIT */
        mod_play(&g, 57);
        fn = mod_freq(.3, .6);                               /* ENV 2 off (ENV 1's own term is in all of them) */
        fe = mod_freq(.02, .08);
        fl = mod_freq(1.0, 1.3);
        g.params[0][K_PAGE_ENV2][3] = 100;                   /* AMT */
        g.params[0][K_PAGE_ENV2][0] = 0;                     /* DLY: none */
        g.params[0][K_PAGE_ENV2][1] = 100;                   /* HOLD: as long as the key is down */
        mod_play(&g, 57);
        fh = mod_freq(.3, .6);
        g.params[0][K_PAGE_ENV2][0] = 100;                   /* DLY: 2 s */
        mod_play(&g, 57);
        fd = mod_freq(.3, .6);
        ff = mod_freq(2.3, 2.6);                             /* after the delay it holds */
        printf("env2 pitch: off %.0f Hz, held %.0f, delayed %.0f (later %.0f)\n", fn, fh, fd, ff);
        assert(fh > fn * 1.8);                               /* an octave up (and a bit) */
        assert(fabs(fd - fn) < fn * .03);                    /* the delay keeps it off */
        assert(ff > fn * 1.8);
        g.params[0][K_PAGE_ENV2][0] = 0;
        g.params[0][K_PAGE_ENV2][1] = 0;                     /* HOLD: none, CRV: a fast fall */
        g.params[0][K_PAGE_ENV2][2] = 60;                    /* CRV: a fall of ~0.25 s */
        mod_play(&g, 57);
        assert(mod_freq(.02, .08) > fe * 1.15);              /* it starts high, */
        assert(fabs(mod_freq(1.0, 1.3) - fl) < fl * .03);    /* and falls back to ENV 1's own */
    }

    /* ENV 2 to the filter: the note's spectrum moves with it (bright while it holds, dark after its fall); with AMT 50 it stays. */
    {
        KApp g;
        double b_off, b_off_early, b_early, b_late, b_dly_early;
        mod_setup(&g, 0);                                    /* SAW */
        g.params[0][K_PAGE_SOUND][16] = 20;                  /* CUT: low */
        g.params[0][K_PAGE_ENV_DEST][1] = 100;               /* FLT */
        g.params[0][K_PAGE_ENV1][2] = 30;                    /* ENV 1 settles low (its own term is small) */
        mod_play(&g, 45);
        b_off = mod_bright(1.2, 1.4);
        b_off_early = mod_bright(.25, .4);
        g.params[0][K_PAGE_ENV2][3] = 100;
        g.params[0][K_PAGE_ENV2][0] = 0;
        g.params[0][K_PAGE_ENV2][1] = 60;                    /* HOLD ~ 0.43 s, then a fall (CRV 50: ~0.1 s) */
        mod_play(&g, 45);
        b_early = mod_bright(.25, .4);
        b_late = mod_bright(1.2, 1.4);
        g.params[0][K_PAGE_ENV2][0] = 100;                   /* DLY: 2 s */
        mod_play(&g, 45);
        b_dly_early = mod_bright(.25, .4);
        printf("env2 filter: bright early %.3f late %.3f (no env2: early %.3f late %.3f, delayed %.3f)\n", b_early, b_late, b_off_early, b_off, b_dly_early);
        assert(b_early > b_off_early * 1.3);
        assert(fabs(b_late - b_off) < b_off * .1);
        assert(fabs(b_dly_early - b_off_early) < b_off_early * .1);
    }

    /* ENV 2 to the amplitude (ENV DEST AMP): + opens the note with ENV 2 (so a delay keeps it quiet), - ducks it. */
    {
        KApp g;
        double plain, quiet, open, duck;
        mod_setup(&g, 3);
        mod_play(&g, 57);
        plain = mod_rms(.3, .5);
        g.params[0][K_PAGE_ENV_DEST][3] = 100;
        g.params[0][K_PAGE_ENV2][3] = 100;
        g.params[0][K_PAGE_ENV2][0] = 100;                   /* DLY 2 s: ENV 2 is 0 */
        mod_play(&g, 57);
        quiet = mod_rms(.3, .5);
        g.params[0][K_PAGE_ENV2][0] = 0;
        g.params[0][K_PAGE_ENV2][1] = 100;                   /* held at 1: the note is open */
        mod_play(&g, 57);
        open = mod_rms(.3, .5);
        g.params[0][K_PAGE_ENV_DEST][3] = 0;                 /* - : ENV 2 ducks it */
        mod_play(&g, 57);
        duck = mod_rms(.3, .5);
        printf("env2 amp: plain %.4f, delayed-open %.4f, held-open %.4f, held-duck %.4f\n", plain, quiet, open, duck);
        assert(quiet < plain * .1);
        assert(fabs(open - plain) < plain * .05);
        assert(duck < plain * .1);
    }

    /* The LFO (RATE WAVE PHS FADE) with its routings (items 4..7 of the page: PIT FLT SHP AMP, 50 = none). To pitch: the period of the
     * note moves at the LFO's rate; with no routing it does not. */
    {
        KApp g;
        double lo, hi, lo0, hi0;
        mod_setup(&g, 3);
        g.params[0][K_PAGE_LFO][0] = 96;                     /* RATE ~ 8 Hz */
        mod_play(&g, 57);
        mod_range(mod_freq, .3, 2.0, &lo0, &hi0);
        g.params[0][K_PAGE_LFO][4] = 25;                     /* PIT +40 % */
        mod_play(&g, 57);
        mod_range(mod_freq, .3, 2.0, &lo, &hi);
        printf("lfo pitch: steady %.0f..%.0f Hz, modulated %.0f..%.0f Hz\n", lo0, hi0, lo, hi);
        assert(hi0 < lo0 * 1.03);
        assert(hi > lo * 1.25);
        /* the rate: a faster LFO crosses its mean more often (the frequency's own zero crossings, 20 ms windows) */
        {
            double mean = (lo + hi) / 2;
            int i, flips = 0, up = 0;
            for(i = 0; i < 80; ++i) {
                int u = mod_freq(.3 + .02 * i, .32 + .02 * i) > mean;
                if(i && u != up) ++flips;
                up = u;
            }
            assert(flips >= 20 && flips <= 30);              /* 1.6 s at 7.9 Hz: ~25 crossings of the mean */
        }
    }

    /* LFO WAVE SQR: the pitch takes two values, one each half cycle; PHS starts a note at a quarter of the cycle (a sine at its top). */
    {
        KApp g;
        double lo, hi, f0, f1;
        mod_setup(&g, 3);
        g.params[0][K_PAGE_LFO][0] = 70;                     /* RATE ~ 2.2 Hz */
        g.params[0][K_PAGE_LFO][1] = 3;                      /* SQR */
        g.params[0][K_PAGE_LFO][4] = 32;
        mod_play(&g, 57);
        mod_range(mod_freq, .1, .2, &lo, &hi);               /* inside one half */
        assert(hi < lo * 1.03 && hi > 0);
        f0 = mod_freq(.05, .15);
        f1 = mod_freq(.30, .40);                             /* (half a cycle is ~ 0.23 s) */
        assert(f1 < f0 * .8 || f1 > f0 * 1.25);
        g.params[0][K_PAGE_LFO][1] = 0;                      /* SIN */
        g.params[0][K_PAGE_LFO][0] = 40;                     /* RATE ~ 0.4 Hz */
        g.params[0][K_PAGE_LFO][2] = 0;
        mod_play(&g, 57);
        f0 = mod_freq(.02, .06);
        g.params[0][K_PAGE_LFO][2] = 32;                     /* PHS: a quarter */
        mod_play(&g, 57);
        f1 = mod_freq(.02, .06);
        printf("lfo phase: PHS 0 %.0f Hz, PHS 32 %.0f Hz\n", f0, f1);
        assert(f1 > f0 * 1.25);
    }

    /* LFO FADE: the routing grows in over the FADE's time after a note. */
    {
        KApp g;
        double lo, hi, elo, ehi;
        mod_setup(&g, 3);
        g.params[0][K_PAGE_LFO][0] = 96;
        g.params[0][K_PAGE_LFO][4] = 63;
        g.params[0][K_PAGE_LFO][3] = 100;                    /* FADE ~ 2 s */
        mod_play(&g, 57);
        mod_range(mod_freq, .02, .12, &elo, &ehi);
        mod_range(mod_freq, 2.4, 2.9, &lo, &hi);
        printf("lfo fade: first %.0f..%.0f Hz, after %.0f..%.0f Hz\n", elo, ehi, lo, hi);
        assert(ehi / elo < 1.15);
        assert(hi / lo > 1.5);
    }
    /* FADE again on a note that comes before the last is let go (a sequence's notes never faded) */
    {
        KApp g;
        double lo, hi, elo, ehi;
        mod_setup(&g, 3);
        g.params[0][K_PAGE_LFO][0] = 96;
        g.params[0][K_PAGE_LFO][4] = 63;
        g.params[0][K_PAGE_LFO][3] = 100;                    /* FADE ~ 2 s */
        g.params[0][K_PAGE_ENV1][3] = 0;                     /* R: the first note gone at once */
        mod_play_over(&g, 45, 57, 1.6);
        mod_range(mod_freq, 2.6, 2.95, &lo, &hi);            /* the second, later: faded in */
        mod_range(mod_freq, 1.62, 1.72, &elo, &ehi);         /* the second, just after: faded out again */
        printf("lfo fade again: after the next note %.0f..%.0f Hz, later %.0f..%.0f Hz\n", elo, ehi, lo, hi);
        assert(hi / lo > 1.5);
        assert(ehi / elo < 1.15);
    }

    /* LFO to the filter and to the amplitude: brightness and level swing with it. */
    {
        KApp g;
        double lo, hi, lo0, hi0;
        mod_setup(&g, 0);
        g.params[0][K_PAGE_SOUND][16] = 50;                  /* CUT */
        g.params[0][K_PAGE_ENV1][2] = 100;
        g.params[0][K_PAGE_LFO][0] = 60;                     /* ~1.2 Hz */
        mod_play(&g, 45);
        mod_range(mod_bright, .3, 2.5, &lo0, &hi0);
        g.params[0][K_PAGE_LFO][5] = 63;                    /* FLT */
        mod_play(&g, 45);
        mod_range(mod_bright, .3, 2.5, &lo, &hi);
        printf("lfo filter: brightness steady %.3f..%.3f, modulated %.3f..%.3f\n", lo0, hi0, lo, hi);
        assert(hi0 < lo0 * 1.3);   /* (a lone saw low in the filter: its 20 ms windows differ a little) */
        assert(hi > lo * 2.0);
        g.params[0][K_PAGE_LFO][5] = 0;
        g.params[0][K_PAGE_LFO][7] = 63;                    /* AMP */
        mod_play(&g, 45);
        mod_range(mod_rms, .3, 2.5, &lo, &hi);
        mod_play(&g, 45);
        printf("lfo amp: level %.4f..%.4f\n", lo, hi);
        assert(hi > lo * 3.0);
        g.params[0][K_PAGE_LFO][7] = 0;
        g.params[0][K_PAGE_LFO][6] = 63;                    /* SHP (SAW's has no shape: no change is allowed, no crash) */
        mod_play(&g, 45);
    }

    /* Kreisel's ANALOG, its EDIT half and the rest of SOUND: LEVEL 0 is silent; HP is brighter than LP at the same cutoff; the sub
     * oscillator adds to the sound; with GLIDE the pitch of a second note starts near the first's. Storage: params[SOUND][4 + k]. */
    {
        KApp g;
        double lp, hp, full, none, plain, subbed, f0, f1;
        mod_setup(&g, 0);
        g.params[0][K_PAGE_SOUND][4 + 12] = 60;                /* CUT */
        mod_play(&g, 57); lp = mod_bright(.2, .5); full = mod_rms(.2, .5);
        g.params[0][K_PAGE_SOUND][4 + 16] = 2;                 /* TYPE HP */
        mod_play(&g, 57); hp = mod_bright(.2, .5);
        g.params[0][K_PAGE_SOUND][4 + 16] = 0;
        g.params[0][K_PAGE_SOUND][4 + 18] = 0;                 /* LEVEL 0 */
        mod_play(&g, 57); none = mod_rms(.2, .5);
        g.params[0][K_PAGE_SOUND][4 + 18] = 127;
        g.params[0][K_PAGE_SOUND][4 + 12] = 127;
        mod_play(&g, 57); plain = mod_rms(.2, .5);
        g.params[0][K_PAGE_SOUND][4 + 8] = 127;                /* SUB */
        mod_play(&g, 57); subbed = mod_rms(.2, .5);
        printf("analog: LP %.3f HP %.3f brightness, level %.4f -> %.4f, sub %.4f -> %.4f\n", lp, hp, full, none, plain, subbed);
        assert(hp > lp * 1.5 && none < full * 1e-3 && subbed > plain * 1.05);
        (void)f0; (void)f1;
    }
    {   /* the buses: a track sent to bus 1 (the reverb) or bus 2 (the delay) rings on after its note; not sent, it does not */
        static KApp g; static KAudio ba; static float bb[2 * 4410];
        for (int bus = 0; bus < 2; bus++) {
            double tail[2];
            for (int send = 0; send < 2; send++) {
                double e = 0;
                k_init(&g); g.params[0][K_PAGE_MIXER][2 + bus] = send ? 100 : 0;
                g.params[0][K_PAGE_ENV1][3] = 0;   /* (a short release) */
                k_audio_init(&ba, 44100); k_audio_sync(&ba, &g);
                k_audio_note(&ba, 0, 60, 120, 1); k_audio_render(&ba, bb, 4410);
                k_audio_note(&ba, 0, 60, 0, 0);
                for (int blk = 0; blk < 4; blk++) k_audio_render(&ba, bb, 4410);   /* 0.4 s after */
                k_audio_render(&ba, bb, 4410);
                for (int j = 0; j < 2 * 4410; j++) e += fabs(bb[j]);
                tail[send] = e;
            }
            if (!(tail[1] > 10.0 * tail[0] + 1e-3)) printf("bus %d: tail %g sent, %g not\n", bus + 1, tail[1], tail[0]);
            assert(tail[1] > 10.0 * tail[0] + 1e-3);
        }
    }
    {   /* the master limiter: a hot mix (every track a loud chord, the faders and the master up) never passes -1 dBFS (nor full
         * scale); a quiet one goes through untouched (no reduction, ever: the samples as they were) */
        static KApp g; static KAudio la; static int32_t lb[2 * 4410];
        for (int hot = 0; hot < 2; hot++) {
            int32_t peak = 0;
            uint32_t red = 0;
            k_init(&g);
            g.master = 100;
            for (int t = 0; t < K_TRACKS; t++) {
                g.sound[t] = 0;   /* ANALOG */
                g.params[t][K_PAGE_MIXER][0] = hot ? 100 : 40;
                g.params[t][K_PAGE_SOUND][16] = 127;   /* CUT open */
            }
            k_audio_init(&la, 44100); k_audio_sync(&la, &g);
            for (int t = 0; t < K_TRACKS; t++) for (int n = 0; n < 4; n++) k_audio_note(&la, t, 36 + 7 * n + t, hot ? 127 : 60, 1);
            for (int blk = 0; blk < 20; blk++) {
                k_audio_render_s32(&la, lb, 4410);
                for (int j = 0; j < 2 * 4410; j++) { const int32_t v = (lb[j] < 0 ? -lb[j] : lb[j]) >> 7; if (v > peak) peak = v; }
                if (la.lim_red > red) red = la.lim_red;
            }
            printf("limiter: %s mix, peak %d (ceiling 29204), most reduction %.1f dB\n", hot ? "hot" : "quiet", (int)peak,
                   red ? 20.0 * log10(65536.0 / (65536.0 - red)) : 0.0);
            if (hot) assert(red > 0 && peak <= 29204 + 1);
            else assert(red == 0 && peak > 1000 && peak < 29204);
        }
    }
    {   /* the metronome: an empty song plays silent with MET off; with MET on a click comes on each beat (the bar's first louder),
         * none between them, nothing to MIDI out. Counted in (KApp.count_in): a bar of clicks, then the first step, and MIDI's
         * Start only then */
        static KApp g; static KAudio ma; static int32_t mb[2 * 6000];
        int32_t pk[8];
        for (int on = 0; on < 2; on++) {
            k_init(&g);
            for (int t = 0; t < K_TRACKS; t++) { k_roll_fill(&g, t, 0, 0, 60); for (int l = 0; l < K_LANES; l++) g.lane_on[t][l] = 0; memset(g.lane_pat[t], 0, sizeof g.lane_pat[t]); }
            g.master = 100; g.bpm = 120; g.set_v[K_SET_METRO] = 1; g.recording = on; g.playing = 1;   /* (the settings' Metronome: only while REC) */
            k_audio_init(&ma, 48000); k_audio_sync(&ma, &g); ma.mo_on = 1;
            for (int st = 0; st < 8; st++) {   /* a 16th is 6000 frames */
                pk[st] = 0;
                k_audio_render_s32(&ma, mb, 6000);
                for (int j = 0; j < 2 * 6000; j++) { const int32_t v = (mb[j] < 0 ? -mb[j] : mb[j]) >> 7; if (v > pk[st]) pk[st] = v; }
            }
            if (!on) for (int st = 0; st < 8; st++) assert(pk[st] == 0);
            else {
                printf("metronome: the bar %d, a beat %d, between %d %d\n", (int)pk[0], (int)pk[4], (int)pk[2], (int)pk[6]);
                assert(pk[0] > 8000 && pk[4] > 4000 && pk[0] > pk[4] && pk[2] <= 2 && pk[6] <= 2);   /* (between: the DC blocker's last bit) */
            }
            {   /* MIDI out: the transport's bytes only (Start, the clock), no note */
                uint8_t m[3];
                while (k_audio_midi_pop(&ma, m)) assert(m[0] >= 0xF8);
            }
        }
        {
            uint8_t m[3];
            int start = 0;
            k_init(&g);
            g.bpm = 120; g.set_v[K_SET_COUNT] = 1; g.recording = 1; g.playing = 1; g.count_in = 1; g.master = 100;
            k_roll_fill(&g, 0, 0, 0xFFFFu, 60);
            k_audio_init(&ma, 48000); k_audio_sync(&ma, &g); ma.mo_on = 1;
            for (int st = 0; st < 16; st++) {
                k_audio_render_s32(&ma, mb, 6000);
                assert(ma.triggers[0] == 0 && ma.current_step == -1);
                while (k_audio_midi_pop(&ma, m)) start |= m[0] == 0xFA;
            }
            assert(!start);
            k_audio_render_s32(&ma, mb, 1);
            assert(ma.triggers[0] == 1 && ma.current_step == 0);
            while (k_audio_midi_pop(&ma, m)) start |= m[0] == 0xFA;
            assert(start);
        }
    }
    {   /* the steps' chances: a synth track's 16 notes and a drum track's 16 hits, 64 bars at 100 %, 75 %, 50 %, 25 %: about so many
         * sound, and the same again from the start (the dice from a fixed seed) */
        static KApp g; static KAudio ca; static int32_t cb[2 * 1500];
        for (int drum = 0; drum < 2; drum++) {
            uint64_t got[4], again = 0;
            for (int c = 0; c < 5; c++) {
                const int cc = c == 4 ? 2 : c;   /* (the fifth: 50 % again, for the same dice) */
                k_init(&g);
                g.bpm = 240; g.playing = 1;
                for (int t = 0; t < K_TRACKS; t++) { k_roll_fill(&g, t, 0, 0, 60); memset(g.lane_on[t], 0, sizeof g.lane_on[t]); }
                if (drum) { g.sound[0] = k_engine_find(K_UID('D','R','U','M')); g.lane_on[0][0] = 0xFFFFu; }
                else k_roll_fill(&g, 0, 0, 0xFFFFu, 60);
                for (int st = 0; st < K_STEPS; st++) k_chance_set(&g, 0, 0, st, cc);
                k_audio_init(&ca, 48000); k_audio_sync(&ca, &g);
                for (int n = 0; n < 64 * 16 * 2; n++) k_audio_render_s32(&ca, cb, 1500);   /* (a 16th at 240 BPM: 3000 frames) */
                if (c < 4) got[c] = ca.triggers[0]; else again = ca.triggers[0];
            }
            printf("chance (%s): 100%% %d, 75%% %d, 50%% %d, 25%% %d of 1024\n", drum ? "drum" : "synth", (int)got[0], (int)got[1], (int)got[2], (int)got[3]);
            assert(got[0] == 1024 && got[1] > 700 && got[1] < 840 && got[2] > 440 && got[2] < 580 && got[3] > 190 && got[3] < 330);
            assert(again == got[2]);
        }
    }
    puts("kreisel audio tests: OK");
    return 0;
}
