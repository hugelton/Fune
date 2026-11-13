/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_SDL_IOCONF_H
#define KREISEL_SDL_IOCONF_H
#include "kreisel.h"
/* io.conf (): the inputs' settings in a file the owner edits,
 * in the app's data folder. The Connect tab is a window on its [midi] part; a change there is written back into the file in place
 * (its comments and other lines kept).
 *   [midi]  in = all | none | a part of a port's name     channel = all | 1 .. 16     clock = internal | follow     out = on | off
 *   [keys]  map = default | none                           (default: the device's own table, kreisel_io.c)
 *   [audio] audio_out = a device's name (empty: the system's)   audio_rate = Hz (0: the device's)   audio_buffer = frames (2^n)
 *           audio_in = a capture device's name (empty: none; kept for recording, not opened yet)
 *   sdl_ioconf_load(app, dir)    read dir/io.conf (written with its defaults, commented, when there is none); 0 when read
 *   sdl_ioconf_sync(app, dir)    each frame: the Connect tab's rows changed: the file follows
 *   sdl_ioconf_audio(dir, out)   the [audio] part (before the device opens; no settings row for it), its keys added when missing */
int sdl_ioconf_load(KApp *app, const char *dir);
void sdl_ioconf_sync(const KApp *app, const char *dir);
typedef struct { char out[128], in[128]; int rate, buffer; } SdlAudioConf;
void sdl_ioconf_audio(const char *dir, SdlAudioConf *out);
int sdl_ioconf_midi_out(void);   /* [midi] out = on | off (no line: on) */
#endif
