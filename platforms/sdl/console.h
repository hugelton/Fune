/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef SDL_CONSOLE_H
#define SDL_CONSOLE_H
/* The FM-1's console (platforms/fm1hw/fm1_console.c) on the SDL builds, for a script that drives Fune: the demo video
 * (platforms/fm1hw/fm1_demo.py, docs/DEMO_VIDEO.md) on the Cardputer Zero and on a computer, with the same timeline as the
 * FM-1. `kreisel --console PORT`: a TCP port on 127.0.0.1 only (another machine reaches it through ssh -L), one client at a
 * time, a line a command, a line a reply. The commands the FM-1's have for driving it:
 *   stat                 page, track, playing, step, scene, bpm (key=value)
 *   ver, slots           the version; the song slots s1..s6 (NAME, '_' for a space, '*' unnamed, '-' empty)
 *   load N | demo        a song slot, or the built-in demo song (MURIYA)
 *   page N|NAME, track N, play, btn NAME, knob K D, note N VEL, key ID down|up   as the FM-1's panel and keys
 * Nothing that writes (save, erase) is there. */
#include <stdint.h>
#include "kreisel.h"
#include "kreisel_audio.h"
int sdl_console_open(int port);   /* 0, or -1 (the port taken, no sockets) */
void sdl_console_poll(KApp *app, const KAudio *audio, uint32_t ms);   /* the main loop: a client, its lines, the replies */
void sdl_console_close(void);
#endif
