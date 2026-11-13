/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_INPUT_H
#define KREISEL_INPUT_H
#include <SDL.h>
#include "kreisel.h"
/* which controls the device has (K_IN_*): the adapter gives the app only those */
void sdl_input_set(unsigned mask);
void sdl_process_event(KApp *app,const SDL_Event *event,SDL_Renderer *renderer,int *running);
#endif
