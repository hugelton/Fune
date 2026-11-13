/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_BLIT_H
#define KREISEL_BLIT_H
#include <SDL.h>
#include "gfx.h"

/* The SDL side of the canvas: a streaming texture the canvas is copied into, and a BMP writer. */
int sdl_blit_init(SDL_Renderer *renderer, int w, int h);
void sdl_blit(SDL_Renderer *renderer, const GCanvas *canvas);   /* draws it at (0, 0) */
int sdl_blit_save(const char *path, const GCanvas *canvas);     /* 0 on success */
void sdl_blit_shutdown(void);
#endif
