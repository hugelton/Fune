/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The browser's sound: Kreisel's audio host as its own small WebAssembly module (no JS glue, no SharedArrayBuffer), run
 * inside an AudioWorklet (worklet.js) and, for the smoke test, under Node. The page posts it the core's KApp as bytes
 * (kw_app_ptr / kw_app_size of the UI module: both are built from the same headers, so the layout is the same) and the live
 * notes; it renders 128-frame quanta. Plain exported functions, so the module needs no imports but the memory it owns. */
#include <stdint.h>
#include <string.h>
#include "kreisel.h"
#include "kreisel_audio.h"

#define KA_EXPORT __attribute__((used, visibility("default")))
#define KA_MAX_FRAMES 1024

static KAudio audio;
static KApp app;
static float out[KA_MAX_FRAMES * 2];

KA_EXPORT void ka_init(uint32_t rate) { k_audio_init(&audio, rate); memset(&app, 0, sizeof app); }
KA_EXPORT uint32_t ka_app_size(void) { return (uint32_t)sizeof app; }
KA_EXPORT uint8_t *ka_app_ptr(void) { return (uint8_t *)&app; }   /* the page copies the KApp bytes here, then calls ka_sync */
KA_EXPORT void ka_sync(void) { k_audio_sync(&audio, &app); }
KA_EXPORT void ka_note(int track, int note, int vel, int on) { k_audio_note(&audio, track, note, vel, on); }
/* renders frames (<= KA_MAX_FRAMES) interleaved into the buffer ka_out() returns */
KA_EXPORT float *ka_render(int frames)
{
    if (frames > KA_MAX_FRAMES) frames = KA_MAX_FRAMES;
    k_audio_render(&audio, out, (size_t)frames);
    return out;
}
KA_EXPORT int ka_step(void) { return audio.current_step; }
KA_EXPORT int ka_playing(void) { return audio.playing; }
