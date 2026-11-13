/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_DESK_H
#define KREISEL_DESK_H
/* The desktop app's own files: where they live (desk_dir), the session it closed with (the song as it stood, the page and track,
 * the level and octave) and its window. Nothing of this is the core's: the FM-1 has its slots and nothing else. */
#include "kreisel.h"

typedef struct { int x, y, w, h, ok; } DeskWindow;

/* The folder: macOS ~/Library/Application Support/Kreisel, Linux $XDG_DATA_HOME/kreisel (~/.local/share/kreisel), made if missing;
 * an older ~/.kreisel moved there once. 0 when there is no home. */
int desk_dir(char *out, int n);
/* The session: written on quit, read on start. load: 0 when one was there and opened (the song, then the page, track, level,
 * octave over it); the window's place too, ok 0 when none was kept. */
int desk_session_save(const KApp *app, const char *dir, const DeskWindow *win);
int desk_session_load(KApp *app, const char *dir, DeskWindow *win);
/* A song or preset file (.fune: a slot's bytes, as the FM-1's fm1_slots.py pulls them) opened: a song replaces the song, a
 * preset goes onto the track in hand (k_save_open_file). 0 opened; else nothing changed, why on stderr. SAVE then keeps it. */
int desk_open_file(KApp *app, const char *path);

#endif
