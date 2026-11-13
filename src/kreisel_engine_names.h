/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_ENGINE_NAMES_H
#define KREISEL_ENGINE_NAMES_H
/* The built-in engines' names as shown (the engine's bar, HOME, the mixer, PRESETS, a preset's default name): one place, by each
 * engine's id. A name is only what the screen says: songs, presets and the skins know an engine by its id (K_UID), never by
 * its name, so renaming one here breaks nothing saved. tools/gen_engines.py reads this file for the tables it writes
 * (src/engines_data.c: run it again after a change); each eng_*.c names its DSP by the same macro.
 * (2026-11-07: the owner is renaming them; so far ANALOG is Retro and FM2 is Digital. 2026-11-12: written as words, not in capitals.) */
#define K_ENGINE_NAME_KANL "Retro"
#define K_ENGINE_NAME_KPHS "Phase"
#define K_ENGINE_NAME_KTRI "Trio"
#define K_ENGINE_NAME_KLOF "Lofi"
#define K_ENGINE_NAME_KOGN "Organ"
#define K_ENGINE_NAME_KENS "Ensemble"
#define K_ENGINE_NAME_KVEC "Vector"
#define K_ENGINE_NAME_KFM2 "Digital"
#define K_ENGINE_NAME_DRUM "Drum"
#endif
