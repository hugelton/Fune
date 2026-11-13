/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The sample store (k_sample.h): a fixed table of slots, a slot free while its path is NULL. */
#include "k_sample.h"
#include "k_wav.h"
#include <stdlib.h>
#include <string.h>

static KSample slots[K_SAMPLE_MAX];
static size_t used, cap = K_SAMPLE_MEM_MAX;
static int last_err;

static const char *base_name(const char *p)
{
    const char *s = p, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') s = q + 1;
    return s;
}

int k_sample_find(const char *path)
{
    int i;
    if (!path) return -1;
    for (i = 0; i < K_SAMPLE_MAX; i++) if (slots[i].path && strcmp(slots[i].path, path) == 0) return i;
    return -1;
}

int k_sample_load(const char *path)
{
    KWav w;
    KSample *s;
    size_t len;
    char *p;
    int id, r;

    last_err = K_WAV_OK;
    if (!path || !*path || (len = strlen(path)) >= K_SAMPLE_PATH_MAX) { last_err = K_WAV_ERR_IO; return -1; }
    id = k_sample_find(path);
    if (id >= 0) { slots[id].refs++; return id; }
    for (id = 0; id < K_SAMPLE_MAX && slots[id].path; id++) {}
    if (id == K_SAMPLE_MAX) { last_err = K_WAV_ERR_TOO_BIG; return -1; }
    if (used >= cap) { last_err = K_WAV_ERR_TOO_BIG; return -1; }
    r = k_wav_load(path, &w, cap - used);
    if (r != K_WAV_OK) { last_err = r; return -1; }
    p = malloc(len + 1);
    if (!p) { k_wav_free(&w); last_err = K_WAV_ERR_NOMEM; return -1; }
    memcpy(p, path, len + 1);
    s = &slots[id];
    s->path = p;
    s->name = base_name(p);
    s->frames = w.frames;
    s->nframes = w.nframes;
    s->rate = w.rate;
    s->channels = w.channels;
    s->has_loop = w.has_loop;
    s->loop_start = w.loop_start;
    s->loop_end = w.loop_end;
    s->bytes = (size_t)w.nframes * (size_t)w.channels * sizeof(int16_t);
    s->refs = 1;
    used += s->bytes;
    return id;
}

int k_sample_last_error(void) { return last_err; }

const KSample *k_sample_get(int id)
{
    if (id < 0 || id >= K_SAMPLE_MAX || !slots[id].path) return NULL;
    return &slots[id];
}

void k_sample_free(int id)
{
    KSample *s;
    if (id < 0 || id >= K_SAMPLE_MAX || !slots[id].path) return;
    s = &slots[id];
    if (--s->refs > 0) return;
    used -= s->bytes;
    free((void *)s->frames);
    free((void *)s->path);
    memset(s, 0, sizeof *s);
}

void k_sample_free_all(void)
{
    int i;
    for (i = 0; i < K_SAMPLE_MAX; i++) if (slots[i].path) { slots[i].refs = 1; k_sample_free(i); }
}

size_t k_sample_mem_used(void) { return used; }
size_t k_sample_mem_max(void) { return cap; }
void k_sample_set_mem_max(size_t bytes) { cap = bytes < K_SAMPLE_MEM_MAX ? bytes : K_SAMPLE_MEM_MAX; }
