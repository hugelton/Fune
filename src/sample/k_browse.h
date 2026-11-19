/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler's file browser (Linux and macOS only, never the FM-1): the samples folder's WAV files as a list over the Sampler's
 * page, for one track. Moving through it gives the track the file in hand at once (k_smp_assign), so the keys play what is under
 * the cursor; closing keeps it, or puts back the file the track had when it opened. Each file's length is read from its header
 * alone when the list is made (no samples loaded for that). The main thread only. */
#ifndef K_BROWSE_H
#define K_BROWSE_H
#include <stdint.h>
#include "k_smp.h"

#define K_BROWSE_MAX 256

int k_browse_open(int track);          /* the folder listed, the track's file in hand (else the first): how many files */
void k_browse_close(int keep);         /* keep 0: the track's file as it was before the browser opened */
int k_browse_is_open(void);
int k_browse_track(void);              /* the track it is for (-1 closed) */
int k_browse_count(void);
int k_browse_sel(void);                /* the row in hand, -1 none (an empty folder) */
const char *k_browse_name(int i);      /* NULL out of range */
uint32_t k_browse_ms(int i);           /* its length in ms, read from its header; 0 when not readable */
int k_browse_move(int d);              /* the row d further (held at the ends), given to the track: 0, or a K_WAV_ERR_* */
int k_browse_pick(int i);              /* row i in hand, given to the track: 0, or a K_WAV_ERR_*, or -1 out of range */
/* the header alone: frames, rate and channels of a WAV file (PCM 16 / 24, float 32, EXTENSIBLE): 0, or a K_WAV_ERR_* */
int k_wav_info(const char *path, uint32_t *frames, uint32_t *rate, int *channels);
#endif
