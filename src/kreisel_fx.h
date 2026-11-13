/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments */
#ifndef KREISEL_FX_H
#define KREISEL_FX_H
#include "kreisel.h"
#include "kreisel_plugin.h"
#include "kreisel_effect.h"

#define K_FX_BUILTINS 15u
#ifndef K_FX_POOL_BYTES
#define K_FX_POOL_BYTES (49152u * K_TRACKS) /* the inserts' arena: 192 KB at four tracks (a 1 s delay takes 88 KB); a platform
                                             * sets its own (the FM-1 40 KB, the Switch 1.5 MB: platforms/platforms.toml) */
#endif
/* FM-1 overrides the pool to its KDeviceCaps RAM budget. */
const KPlugin *k_fx_builtin_at(unsigned index);
void k_fx_forget(void);
void k_fx_bind(const void *owner);
void k_fx_sync(const KApp *app);
void k_fx_process_track(unsigned track, int32_t *samples, unsigned n);
int k_fx_track_active(unsigned track);
/* 1: the track is balanced after its inserts (GAIN's PAN), *l and *r its left and right gains (Q12, 4096 unity); 0: centred */
int k_fx_track_balance(unsigned track, int32_t *l, int32_t *r);
int k_fx_bus_active(unsigned bus);   /* bus 0 (the reverb) or 1 (the delay) runs: a track sends to it */
void k_fx_process_bus(unsigned bus, int32_t *samples, unsigned n);   /* its sum in, its effect out (wet), in place */   /* an insert of the track's is on (it takes the track as one mono signal) */
#endif
