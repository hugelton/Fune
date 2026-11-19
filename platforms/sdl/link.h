/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef SDL_LINK_H
#define SDL_LINK_H
/* Ableton Link for the SDL builds (link.cpp, built with KREISEL_LINK: third_party/link, GPL-2.0-or-later, so a build with it is
 * GPL as a whole: LICENSING.md). Without it these do nothing and the settings' Link row stays hidden.
 *
 *   sdl_link_init(app)                 once: the session at the app's BPM, off; the Link row shown (app->link_peers 0)
 *   sdl_link_poll(app)                 the main loop, before k_audio_sync: the settings' Link row on or off (and not while the
 *                                      clock follows MIDI), the peers to app->link_peers, the session's tempo to the BPM (a whole
 *                                      number) and a BPM turned here to the session, PLAY and STOP both ways
 *   sdl_link_audio(audio, frames)      the audio callback, before the render: the sequencer to the session's bar
 *   sdl_link_format(rate, latency)     the audio's rate and its frames from the render to the speaker (the buffer's)
 * A build without it (cmake -DKREISEL_LINK=OFF, or no third_party/link): these are empty and the program stays C. */
#ifdef __cplusplus
#ifndef _Static_assert
#define _Static_assert static_assert   /* (the core's C11 checks, as C++ has them: g++ knows no _Static_assert) */
#endif
extern "C" {   /* (the core is C) */
#endif
#include "kreisel.h"
#include "kreisel_audio.h"
#ifdef __cplusplus
}
#endif
#if !defined(KREISEL_LINK) && !defined(__cplusplus)
static inline void sdl_link_init(KApp *app) { (void)app; }
static inline void sdl_link_poll(KApp *app) { (void)app; }
static inline void sdl_link_format(uint32_t rate, uint32_t latency_frames) { (void)rate; (void)latency_frames; }
static inline void sdl_link_audio(KAudio *audio, uint32_t frames) { (void)audio; (void)frames; }
static inline void sdl_link_close(void) {}
#else
#ifdef __cplusplus
extern "C" {
#endif
void sdl_link_init(KApp *app);
void sdl_link_poll(KApp *app);
void sdl_link_format(uint32_t rate, uint32_t latency_frames);
void sdl_link_audio(KAudio *audio, uint32_t frames);
void sdl_link_close(void);
#ifdef __cplusplus
}
#endif
#endif
#endif
