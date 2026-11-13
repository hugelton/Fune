/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * The external clock follower, written for Kreisel. Its behaviour follows Felucca 2.0's (start sets step zero, continue keeps
 * the step, stop stops, the clock must keep arriving), but no code is taken from felucca-2.0-snap/firmware/src/midi_clock.c:
 * that file's header names other authors (ChanceTheMaker, keremimo), so it is not ported. */
#include "kreisel_clock.h"

#define K_CLOCK_TIMEOUT_MS 500u   /* no clock for this long while playing: stop */

static void stop(KApp *a)
{
    a->playing = 0;
    a->beat = -1;
    a->step = -1;
    a->clk.pend = 0;
}

/* the tempo: 24 clocks a beat, so bpm = 2500 * clocks / ms over the window; judged every 6 clocks */
static void pulse_tempo(KClock *c, KApp *a, uint32_t ms)
{
    uint32_t n = c->n;
    if (n && (uint32_t)(ms - c->ts[(n - 1u) % 24u]) > K_CLOCK_TIMEOUT_MS)
        n = c->n = 0;                                /* the clock had stopped: measure afresh */
    if (n >= 6u && n % 6u == 0u) {
        uint32_t span = n >= 24u ? 24u : n;
        uint32_t dt = ms - c->ts[n >= 24u ? n % 24u : 0u];
        if (dt) {
            uint32_t bpm = (2500u * span + dt / 2u) / dt;
            if (bpm >= 40u && bpm <= 240u)           /* a burst or a gap is not a tempo */
                a->bpm = (int)bpm;
        }
    }
    c->ts[n % 24u] = ms;
    c->n = n + 1u;
}

int k_clock_in(KApp *a, int source, uint8_t byte, uint32_t ms)
{
    KClock *c = &a->clk;
    if (source < 1 || source != a->clock_src)
        return 0;
    switch (byte) {
    case 0xF8u:
        pulse_tempo(c, a, ms);
        if (a->playing) {
            c->pulses++;
            c->pend++;
            c->seen = 1;
            c->last_ms = ms;
        }
        return 1;
    case 0xFAu:                                      /* start: from the top */
        a->playing = 1;
        a->play_ms = ms;
        a->last_idx = -1;
        c->pulses = c->pend = 0;
        c->seen = c->cont = 0;
        c->n = 0;
        c->gen++;
        return 1;
    case 0xFBu:                                      /* continue: the next step boundary, not step zero */
        if (!a->playing) {
            a->playing = 1;
            a->play_ms = ms;
            a->last_idx = -1;
        }
        c->pulses = (c->pulses + 5u) / 6u * 6u;
        c->pend = 0;
        c->seen = 0;
        c->cont = 1;
        c->n = 0;
        c->gen++;
        return 1;
    case 0xFCu:
        stop(a);
        return 1;
    default:
        return 0;
    }
}

void k_clock_poll(KApp *a, uint32_t ms)
{
    const KClock *c = &a->clk;
    uint32_t ref;
    if (a->clock_src <= 0 || !a->playing)
        return;
    ref = c->seen ? c->last_ms : a->play_ms;
    if ((int32_t)(ms - ref) > (int32_t)K_CLOCK_TIMEOUT_MS)
        stop(a);
}

uint32_t k_clock_take(KApp *a)
{
    uint32_t n = a->clk.pend;
    a->clk.pend = 0;
    return n;
}
