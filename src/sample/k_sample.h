/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The sample store (Linux and macOS only, never the FM-1): WAV files loaded into memory, each under a small integer id. A song
 * keeps the id's path (relative, as given), never the frames. Loading the same path again returns the same id and counts a
 * reference; k_sample_free drops one. All the frames together stay under a memory cap. Not thread-safe (one caller for now). */
#ifndef K_SAMPLE_H
#define K_SAMPLE_H
#include <stddef.h>
#include <stdint.h>

#ifndef K_SAMPLE_MEM_MAX
#define K_SAMPLE_MEM_MAX (64u * 1024u * 1024u)   /* bytes of frames, all samples together */
#endif
#ifndef K_SAMPLE_MAX
#define K_SAMPLE_MAX 128                         /* samples loaded at once */
#endif
#define K_SAMPLE_PATH_MAX 1024

typedef struct {
    const char *path;            /* as given to k_sample_load */
    const char *name;            /* the file's base name, within path */
    const int16_t *frames;       /* interleaved, channels * nframes */
    uint32_t nframes, rate;
    int channels;                /* 1 or 2 */
    int has_loop;
    uint32_t loop_start, loop_end;   /* frames; loop_end one past the last */
    size_t bytes;                /* the frames' memory */
    int refs;
} KSample;

/* Load a WAV file (or count one more reference to it): its id, or -1 (k_sample_last_error says why, a K_WAV_ERR_* code). */
int k_sample_load(const char *path);
int k_sample_last_error(void);
/* The sample of an id, or NULL when the id is free or out of range. */
const KSample *k_sample_get(int id);
/* The id loaded from this exact path, or -1. */
int k_sample_find(const char *path);
/* Drop one reference; the last one releases the frames and frees the id. */
void k_sample_free(int id);
void k_sample_free_all(void);
/* The frames' memory in use, and the cap (lowered at run time with k_sample_set_mem_max, never above K_SAMPLE_MEM_MAX). */
size_t k_sample_mem_used(void);
size_t k_sample_mem_max(void);
void k_sample_set_mem_max(size_t bytes);

#endif
