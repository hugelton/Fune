/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The engine interface: what an engine (a sound source, a drum, an effect) gives the host, in one place, with its version.
 * The built-in engines and effects go through it as an outside plugin does (docs/PLUGIN_SDK.md is the reference; this header is
 * the contract it describes). An engine is one record:
 *
 *   KPlugin            (kreisel_plugin.h)   its kind, its own version, what it needs (rate, RAM, time, FPU); points at the rest
 *    .desc  KEngine    (kreisel_engine.h)   its name and id (K_UID), its parameters (KParamDesc: label, format, range, default,
 *                                           value names, unit), and its UI hints: the titles of its rows, where EDIT starts
 *                                           (page2), its four macros, the host modulations it does not read (nomod), an organ's
 *                                           gate, a drum's lanes
 *    .dsp   engine_t   (kreisel_engine_types.h)  a synth's sound: note_on, render (adds a block into the voices' sum), and the
 *                                           optional block / amp hooks; KEffectDSP (kreisel_effect.h) an effect's
 *    .ext   KPluginExt (kreisel_plugin_ui.h)     optional: its own screens (layouts of the host's widgets, a picture's drawing)
 *
 * The built-in synths' records are K_BUILTIN_PLUGINS (kreisel_audio.c, beside their DSP: the engines are built in one unit with
 * the audio); the built-in effects' are kreisel_fx.c's. Both pass k_plugin_check as any plugin must (tests/plugin_test.c).
 *
 * Versions. There are three numbers, and only the first can break anything:
 *   K_ENGINE_API_MAJOR   the layouts: KEngine and KParamDesc whole, engine_t / voice_t / vmod_t / track_t's prefix whole, KPlugin's
 *                        first K_PLUGIN_SIZE_1 bytes. It is K_PLUGIN_ABI, stored in each record (abi); a record of another one is
 *                        refused (K_PLUG_E_ABI). It changes only when one of those layouts must.
 *   K_ENGINE_API_MINOR   what has been appended since: KPlugin, KPluginExt and KHostApi grow at their end only, a field's 0 meaning
 *                        what the interface did without it, and each carries its size: an older plugin on a newer host reads its
 *                        new fields as 0; a newer plugin on an older host has the fields it does not know ignored (and a widget of
 *                        a kind the host does not know is skipped). 1: the needs (fs, ram, cost, needs); 2: lane_notes; 3: ext;
 *                        4: an outside drum's values for each lane (no layout changed: a synth that is a drum finds, in its part's
 *                        p, the lane's values of its first ten parameters while the host calls it for a voice of that lane, its
 *                        note_on, amp and render; lane 1's in block and post. A plugin of an older minor gets them the same way).
 *   KPlugin.version      the engine's own: 1 .. K_ENGINE_VERSION_MAX. A song keeps it beside the id (one byte). Raise it when
 *                        the engine's parameters change; keep the rules below and an old song still plays as it did.
 *
 * Compatibility of an engine with the songs that name it (kreisel_song.c load_trak_to):
 *   - A song names an engine by its id (K_UID) and keeps its version and its values by parameter id (k_param_id of the label),
 *     never by place. Values are read by id: a parameter added later takes its default, one taken away is dropped, each value
 *     is brought into its range (k_param_clamp).
 *   - So an engine's new version may add parameters, reorder them, move them between pages, widen a range; it must not give a
 *     label another meaning (rename the label: the old value is then dropped), nor change what a stored number sounds like.
 *     Two parameters of one engine never share a label's id (k_plugin_check).
 *   - The id is not here (a plugin missing): the track is kept as it was saved (id, version, values), plays a sine, says NO
 *     PLUGIN, and a save writes it back untouched (KApp.missing).
 *   - The id is here at an older version than the song's (the song was made with a newer engine): kept the same way, untouched,
 *     for the newer engine may have meant its values otherwise (k_track_newer says it is that case, not a missing one).
 *   - The id is here at the song's version or a newer one: played, values by id.
 *   - A built-in engine's id (upper case and digits) is never given to a plugin; a plugin's needs a lower-case letter. */
#ifndef KREISEL_ENGINE_API_H
#define KREISEL_ENGINE_API_H
#include <stdint.h>
#include "kreisel_engine.h"
#include "kreisel_plugin.h"

#define K_ENGINE_API_MAJOR K_PLUGIN_ABI
#define K_ENGINE_API_MINOR 4
#define K_ENGINE_API_VERSION ((K_ENGINE_API_MAJOR << 8) | K_ENGINE_API_MINOR)
#define K_ENGINE_VERSION_MAX 255   /* a song keeps an engine's version in a byte */

/* is the record sound, as k_plugin_register asks (not the device's room, nor whether its id is free): 0, or a K_PLUG_E_*.
 * builtin 1: an id of Hügelton's own is right (upper case and digits), a plugin's is not */
int k_plugin_check(const KPlugin *plugin, int builtin);
/* a parameter's id in a song: of its label (FNV-1a folded to 16 bits, never 0) */
uint16_t k_param_id(const char *label);
#endif
