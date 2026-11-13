/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_PANEL_H
#define KREISEL_PANEL_H
#include <SDL.h>
#include "kreisel.h"

/* A drawn control surface beside the screen, for testing the UI against a device's real controls: knobs (turned
 * with the mouse wheel), buttons (pressed and held with the mouse) and a key bed. It is an input adapter: every
 * control turns into the same semantic actions the keyboard does (k_dispatch); the core knows nothing of it.
 * The only surface so far is the FM-1's. */
int panel_init(SDL_Renderer *renderer, const char *name, int x);   /* at x in logical coordinates; 0 if the name is unknown */
void panel_shutdown(void);
int panel_origin(void);   /* its x, or -1 when there is no panel */
int panel_w(void);
int panel_h(void);
void panel_draw(SDL_Renderer *renderer, const KApp *app);
/* x, y relative to the panel; wheel: dy > 0 turns up (clockwise). Each returns 1 if the panel took it. */
int panel_press(KApp *app, int x, int y, uint32_t ms);
void panel_release(KApp *app, uint32_t ms);
int panel_wheel(KApp *app, int x, int y, int dy, int fine, uint32_t ms);
void panel_hover(int x, int y);
int panel_find(const char *label, int *x, int *y);   /* the centre of a control by its label ("MASTER", "ENV", "KEY 3"), for tests */
int panel_save(const char *path);
#endif
