/* SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's plugin SDK: the one header an outside synth engine includes (docs/PLUGIN_SDK.md). It brings the descriptor and the
 * record (KEngine, KPlugin), its screens (KPluginExt, KLayout, KDraw: kreisel_plugin_ui.h), the DSP interface (engine_t, voice_t, vmod_t), the parameter slots (P_E0 ..), a part as an engine
 * sees it (track_t: its `p`), and the host's integer helpers and tables (dsp.c: sine_i, voice_amp, mulq15, softclip, cents_inc
 * ...), compiled into the plugin. A plugin built as a shared library exports one function:
 *
 *     KREISEL_PLUGIN_EXPORT const KPlugin *kreisel_plugin(void) { return &MY_PLUGIN; }
 *
 * and the host (kreisel_plugin_load.c) finds it by that name. */
#ifndef KREISEL_SDK_H
#define KREISEL_SDK_H
#include <stdint.h>
#include "kreisel_dsp.h"
#include "kreisel_plugin.h"
#include "kreisel_engine_api.h"   /* the interface's version and its rules */
#include "fe_tables.h"
#include "kreisel_engine_types.h"
typedef struct track { K_TRACK_PREFIX; } track_t;   /* (the host's part begins so: an engine reads only its parameters) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "kreisel_voice_dsp.c"
#pragma GCC diagnostic pop

#ifdef _WIN32
#define KREISEL_PLUGIN_EXPORT __declspec(dllexport)
#else
#define KREISEL_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#define KREISEL_PLUGIN_ENTRY "kreisel_plugin"   /* the exported function's name */
typedef const KPlugin *(*kreisel_plugin_fn)(void);
#endif
