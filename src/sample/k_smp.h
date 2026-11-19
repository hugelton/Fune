/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The tracks' samples (Linux and macOS only, never the FM-1): which sample each track's Sampler plays. A track keeps a path,
 * relative to the samples folder when the file is in it (a song keeps that path, never the frames: kreisel_song.c SMPL), and
 * the store's id of what it loaded. The main thread changes them; the audio takes them over in k_audio_sync (under the
 * platform's audio lock), and only then may a sample a track let go be freed (k_smp_collect). */
#ifndef K_SMP_H
#define K_SMP_H
#include <stdint.h>
#include "k_sample.h"

#define K_SMP_TRACKS 8
#define K_SMP_NAME 128           /* a path a song keeps: at most this, less one */

/* the samples folder (the platform's: ~/.local/share/kreisel/samples, macOS Application Support/Kreisel/samples) */
void k_smp_set_dir(const char *dir);
const char *k_smp_dir(void);
/* the full path of a kept one (relative: in the folder; absolute: as it is); 0 when it does not fit */
int k_smp_full(const char *path, char *out, int n);
/* give track t this file (a path in the folder, or absolute; "" none): 0, or a K_WAV_ERR_* (it could not be read: the track keeps
 * the path, and plays nothing, as a song with a file missing opens; a path too long: the track as it was) */
int k_smp_assign(int t, const char *path);
void k_smp_clear_all(void);
const char *k_smp_path(int t);             /* as kept, "" none */
const KSample *k_smp_sample(int t);        /* 0 none */
uint32_t k_smp_gen(void);                  /* changes with every assignment: the audio takes them over when it differs */
void k_smp_collect(void);                  /* free what the tracks let go (only once the audio no longer plays it) */
/* the WAV files in the folder, sorted by name (the folder's own, no deeper): how many; names[i] up to K_SMP_NAME */
int k_smp_list(char (*names)[K_SMP_NAME], int max);
/* the next / previous file in the folder from track t's (d +1 / -1), its name in out: 0, or -1 none */
int k_smp_step(int t, int d, char *out, int n);

#endif
