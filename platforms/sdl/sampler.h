/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler on SDL (Linux and macOS, K_WITH_SAMPLES): the engine registered, the samples folder, the input's capture (opened
 * while a Sampler track's page is up, io.conf audio_in), REC and PRESETS on that page, and WAV files dropped on the window. */
#ifndef KREISEL_SDL_SAMPLER_H
#define KREISEL_SDL_SAMPLER_H
#include "kreisel.h"
/* at start, before a song opens (it may name the Sampler and its files): data_dir "" when there is none */
void sdl_sampler_init(const char *data_dir);
/* the capture's device (io.conf [audio] audio_in: "" the system's, "none" no input), once the audio has started */
void sdl_sampler_input(const char *name);
/* each frame: the input opened or closed with the page, a recording that ended taken into the track */
void sdl_sampler_tick(KApp *app);
/* a .wav dropped on the window: 1 when taken (copied into the folder, given to the track in hand if it plays the Sampler) */
int sdl_sampler_drop(KApp *app, const char *path);
void sdl_sampler_shutdown(void);
#endif
