/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel_clock.h"
#include "kreisel_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static KApp app;
static KAudio aud;
static uint32_t t_ms;   /* the time of the last clock fed */

/* n clocks at `bpm` (24 a quarter), from the ms stamp of the first one; each stamp is floor(i * 2500 / bpm / ... ) so the ms rounding shows */
static void clocks(int src, int n, int bpm, double *phase)
{
    int i;
    for (i = 0; i < n; i++) {
        t_ms = (uint32_t)(*phase);
        (void)k_clock_in(&app, src, 0xF8u, t_ms);
        *phase += 2500.0 / bpm;
    }
}

static void sync_audio(void)   /* what the platform does under its lock */
{
    k_audio_sync(&aud, &app);
    k_audio_clock_tick(&aud, k_clock_take(&app));
}

static void render(void)
{
    float buf[2 * 64];
    k_audio_render(&aud, buf, 64);
}

int main(void)
{
    double ph = 1000;
    int i;

    k_init(&app);
    k_audio_init(&aud, 48000);

    /* INT: nothing is heard, as before */
    assert(!k_clock_in(&app, 1, 0xFAu, 10) && !k_clock_in(&app, 2, 0xFAu, 10) && !app.playing);
    assert(!k_clock_in(&app, 1, 0xF8u, 10) && app.bpm == 120);

    /* USB: a TRS clock is ignored */
    app.clock_src = 1;
    assert(!k_clock_in(&app, 2, 0xFAu, 10) && !app.playing);
    assert(!k_clock_in(&app, 2, 0xF8u, 10) && !k_clock_in(&app, 2, 0xFCu, 10));
    assert(k_clock_in(&app, 1, 0xFAu, ph) && app.playing && app.step == -1);

    /* 24 ppqn at 90 BPM: the tempo is measured */
    clocks(1, 49, 90, &ph);
    assert(app.bpm == 90);
    assert(app.clk.pulses == 49);
    ph = t_ms + 2500.0 / 90;
    clocks(1, 24, 90, &ph);
    assert(app.bpm == 90);
    k_tick(&app, t_ms);
    assert(app.step == (app.clk.pulses - 1) / 6 % 16 && app.playing);

    /* the sequencer: the audio steps every 6 clocks, step 0 on the first one */
    k_init(&app); k_audio_init(&aud, 48000);
    app.clock_src = 2;
    ph = 5000;
    k_roll_fill(&app, 0, 0, 0xFFFF, 60);   /* every step sounds on track 0 */
    assert(k_clock_in(&app, 2, 0xFAu, (uint32_t)ph));
    sync_audio(); render();
    assert(aud.ext && aud.playing && aud.current_step == -1);          /* nothing before the first clock */
    for (i = 0; i < 10; i++) { render(); }
    assert(aud.current_step == -1);
    clocks(2, 1, 120, &ph); sync_audio(); render();
    assert(aud.current_step == 0);
    for (i = 1; i < 6; i++) { clocks(2, 1, 120, &ph); sync_audio(); render(); assert(aud.current_step == 0); }
    clocks(2, 1, 120, &ph); sync_audio(); render();
    assert(aud.current_step == 1);
    for (i = 0; i < 6 * 15; i++) { clocks(2, 1, 120, &ph); sync_audio(); render(); }
    assert(aud.current_step == 0 && aud.triggers[0] >= 17);            /* wraps after 16 steps */
    assert(app.bpm == 120);

    /* the internal tempo is not used: no clocks, no steps (render a second's worth) */
    {
        uint64_t trig = aud.triggers[0];
        for (i = 0; i < 800; i++) render();
        assert(aud.triggers[0] == trig);
    }

    /* Stop, then Continue keeps the step; Start restarts from step zero */
    {
        int s = aud.current_step, next = aud.next_step;
        assert(k_clock_in(&app, 2, 0xFCu, (uint32_t)ph) && !app.playing);
        sync_audio(); render();
        assert(!aud.playing && aud.current_step == -1 && aud.next_step == next);
        clocks(2, 12, 120, &ph);                                        /* clocks while stopped do not step */
        sync_audio(); render();
        assert(aud.current_step == -1);
        assert(k_clock_in(&app, 2, 0xFBu, (uint32_t)ph) && app.playing);
        clocks(2, 1, 120, &ph); sync_audio(); render();
        assert(aud.current_step == next && s == (next + 15) % 16);
        assert(k_clock_in(&app, 2, 0xFAu, (uint32_t)ph));              /* restart while playing */
        clocks(2, 1, 120, &ph); sync_audio(); render();
        assert(aud.current_step == 0);
    }

    /* the clock stops arriving: STOP after 500 ms */
    k_tick(&app, t_ms + 400);
    assert(app.playing);
    k_tick(&app, t_ms + 600);
    assert(!app.playing && app.step == -1);

    /* switching the source: the old one is ignored */
    app.clock_src = 1;
    assert(!k_clock_in(&app, 2, 0xFAu, 9000) && !app.playing);
    app.clock_src = 0;
    assert(!k_clock_in(&app, 1, 0xFAu, 9000) && !app.playing);

    /* INT keeps running by itself: PLAY and the sample clock, as before */
    k_audio_init(&aud, 48000);
    app.playing = 1;
    k_audio_sync(&aud, &app);
    k_audio_clock_tick(&aud, 100);                                      /* ignored */
    render();
    assert(!aud.ext && aud.current_step == 0);
    k_tick(&app, 100000);
    assert(app.playing);                                                /* no clock timeout in INT */

    puts("clock_test: ok");
    return 0;
}
