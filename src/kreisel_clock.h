/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_CLOCK_H
#define KREISEL_CLOCK_H
#include "kreisel.h"

/* The external MIDI clock follower. Portable: a platform feeds it the realtime bytes it receives, with the port they came on.
 *
 *   k_clock_in(app, source, byte, ms)   source 1 = USB, 2 = TRS (the values of KApp.clock_src: INT 0, USB 1, TRS 2).
 *                                       Only the source clock_src names is heard; with INT nothing is. Returns 1 when heard.
 *     0xF8 clock     24 a quarter. Measures the tempo into app->bpm (a window of up to 24 clocks, so the millisecond
 *                    stamps do not show); while playing, counts the clocks for the sequencer (6 a 16th).
 *     0xFA start     PLAY from step zero; the first clock after it is step 0.
 *     0xFB continue  PLAY from where STOP left the sequencer.
 *     0xFC stop      STOP.
 *   k_clock_poll(app, ms)               from k_tick: the clock stopped arriving (500 ms) while playing: STOP (a pulled cable
 *                                       must not leave the sequencer running).
 *   k_clock_take(app)                   the clocks accepted since the last call. The platform hands them to the audio under its
 *                                       lock after k_audio_sync: k_audio_clock_tick(audio, n) (kreisel_audio.h). */
int k_clock_in(KApp *app, int source, uint8_t byte, uint32_t ms);
void k_clock_poll(KApp *app, uint32_t ms);
uint32_t k_clock_take(KApp *app);

#endif
