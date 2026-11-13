/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_PLUGIN_H
#define KREISEL_PLUGIN_H
#include <stddef.h>
#include <stdint.h>
#include "kreisel_engine.h"
#include "kreisel_plugin_ui.h"

/* Plugins supply DSP and a descriptor, and may lay out their own screens with the host's widgets (KPluginExt, kreisel_plugin_ui.h):
 * the host draws them on every device; a plugin without a layout for a screen gets the portable vanilla view.
 *
 * The record is frozen by K_PLUGIN_ABI. It grows in one way only: a field added at the end, whose 0 means what the interface
 * did without it; `size` says how much of it the plugin knows, and the host reads no further. */
#define K_PLUGIN_ABI 1

typedef enum {
    K_PLUG_SYNTH,     /* plays notes: a voice per note, the host's envelope (DSP: a Felucca-Engines engine_t). One that says it is a
                       * drum (desc->drum, desc->lanes) is played as one: lanes, a note each (lane_notes) */
    K_PLUG_DRUM,      /* plays hits: a lane per piece (the lanes of its descriptor), no pitch */
    K_PLUG_EFFECT     /* processes mono Q15 audio in an insert; effects have a separate registry */
} KPluginKind;        /* a hint for the UI and the routing; not a limit on what the plugin may do */

typedef struct {
    uint32_t size;          /* sizeof(KPlugin) as the plugin was built */
    uint16_t abi;           /* K_PLUGIN_ABI */
    uint16_t version;       /* the plugin's own version (a song keeps it beside the id) */
    uint8_t kind;           /* KPluginKind */
    const KEngine *desc;    /* what it shows; desc->uid is its identity */
    const void *dsp;        /* how it sounds: a synth's `const engine_t *` (kreisel_engine_types.h).
                             * 0 for a built-in engine: the host knows its DSP by its id. The core never looks inside. */
    /* What it needs (added at the end: a record of K_PLUGIN_SIZE_1 bytes has none, read as 0). A plugin says what it needs, never
     * which devices it is for: the installer and the host hold each device's KDeviceCaps and decide (k_plugin_fits). 0: not said. */
    uint32_t fs;            /* the sample rate its DSP was built for (its tables); 0: any (it reads the host's) */
    uint32_t ram;           /* bytes of state it keeps of its own, all parts and voices together */
    uint16_t cost;          /* CPU: microseconds one voice takes for a block of 32 samples on the reference device (the FM-1) */
    uint16_t needs;         /* K_NEED_*: what the device must have */
    /* A synth that is a drum (kind SYNTH, desc->drum 1, desc->lanes named; added at the end, 0 when not said): the track plays its
     * lanes as notes of the synth, the note of each lane here (K_LANES of them); 0: General MIDI's (k_plugin_lane_note) */
    const uint8_t *lane_notes;
    /* Its screens and its view of the host (added at the end; 0: none, the vanilla view): kreisel_plugin_ui.h */
    const struct KPluginExt *ext;
} KPlugin;
#define K_PLUGIN_SIZE_1 ((uint32_t)offsetof(KPlugin, fs))   /* the record before the needs: still taken */
#define K_NEED_FLOAT 1u     /* it uses floating point (a device without a fast FPU refuses it) */

/* A device's room for plugins: set by the platform at start (k_plugin_set_caps), held by the installer for each device it knows */
typedef struct {
    uint32_t fs;            /* its sample rate */
    uint32_t ram;           /* bytes the registered plugins may keep, together */
    uint16_t cost;          /* microseconds (at the reference's speed) one track may take a block, its voices together */
    uint16_t voices;        /* voices a track plays */
    uint16_t has;           /* K_NEED_*: what it has */
} KDeviceCaps;

#define K_EFFECT_PLUGIN_SLOTS 8
#define K_PLUGIN_SLOTS 8                               /* outside plugins the registry holds beside the built-in engines (KApp.eng_mem: + 8) */
#define K_ENGINE_MAX (K_ENGINE_COUNT + K_PLUGIN_SLOTS)  /* (K_ENGINE_COUNT: the built-in ones, first, in a fixed order) */
/* (the host's) the built-in engines' records, first in the registry: kreisel_audio.c, beside their DSP */
extern const KPlugin K_BUILTIN_PLUGINS[K_ENGINE_COUNT];
_Static_assert(K_PLUGIN_SLOTS == 8, "KApp.eng_mem holds K_ENGINE_COUNT + 8 engines");

/* What registering can say (k_plugin_register): an index >= 0, or one of these */
enum {
    K_PLUG_E_NULL = -1,     /* no record, no descriptor */
    K_PLUG_E_ABI = -2,      /* another interface (abi), or a record too short */
    K_PLUG_E_UID = -3,      /* an id that is not four printable characters, or one of Hügelton's (upper case and digits only) */
    K_PLUG_E_TAKEN = -4,    /* the id is there already (a built-in engine's ids are never given to a plugin) */
    K_PLUG_E_KIND = -5,     /* a kind the host cannot play yet, or a synth with no DSP, or a drum with no lanes */
    K_PLUG_E_FULL = -6,     /* no free slot */
    K_PLUG_E_DESC = -7,     /* a descriptor that is not sound: a parameter's range, a macro outside its eight */
    K_PLUG_E_RATE = -8,     /* built for another sample rate */
    K_PLUG_E_RAM = -9,      /* its state does not fit beside the others' */
    K_PLUG_E_CPU = -10,     /* its voices take more than a track's time */
    K_PLUG_E_NEED = -11,    /* it needs what the device has not (K_NEED_*) */
    K_PLUG_E_VERSION = -12  /* its own version is not 1 .. K_ENGINE_VERSION_MAX (a song keeps it in a byte: kreisel_engine_api.h) */
};

int k_plugin_count(void);
int k_plugin_lane_note(int index, int lane);      /* the note a synth that is a drum plays for a lane (BD 36, SD 38, CH 42, OH 46 ...) */
/* a synth that is a drum: how many of its parameters each lane has its own of (its first ones, ten at most: K_LANE_VALS); 0 for
 * any other engine (the built-in DRUM keeps its lanes its own way) */
int k_plugin_lane_params(int index);                         /* the engines a track can have: the built-in ones, then the registered */
const KPlugin *k_plugin_at(int index);            /* the plugin at an index; out of range: the first (never 0) */
int k_plugin_register(const KPlugin *plugin);     /* checks it and adds it; its index, or a K_PLUG_E_* */
/* Effect ids: 0 = NONE, 1..15 built-ins (1..4 remain legacy DIST/CHO/DLY/REV),
 * then installed effects; kept separate from the synth engine registry. */
int k_effect_count(void);
const KPlugin *k_effect_at(int type); /* NULL for NONE/invalid */
int k_effect_find(uint32_t uid); /* effect type, or 0 if absent */
void k_plugin_reset(void);                        /* forget the registered plugins (the built-in engines stay) */
const KDeviceCaps *k_plugin_caps(void); /* NULL if no device limits are declared */
void k_plugin_set_caps(const KDeviceCaps *caps);  /* the device's room (copied); 0: not known, nothing is checked */
/* does it fit the device, beside `used` bytes of other plugins' state: 0, or K_PLUG_E_RATE / RAM / CPU / NEED. Not said (0): fits.
 * The installer asks it before it sends a plugin; k_plugin_register asks it again against the caps set. */
int k_plugin_fits(const KPlugin *plugin, const KDeviceCaps *caps, uint32_t used);
/* draw a plugin's picture (K_W_PICTURE) through its KPluginExt.draw; nothing if it has none */
void k_plugin_draw(int engine, const KDraw *g, int id, const int16_t *values, int n);
void k_effect_draw(int type, const KDraw *g, int id, const int16_t *values, int n);   /* the same for an effect (by its type) */
#endif
