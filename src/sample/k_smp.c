/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The tracks' samples (k_smp.h). */
#include "k_smp.h"
#include "k_wav.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char dir[K_SAMPLE_PATH_MAX];
static struct { char path[K_SMP_NAME]; int id; } trk[K_SMP_TRACKS];
static int let_go[K_SMP_TRACKS * 4], nlet;
static uint32_t gen;
static int ready;

static void setup(void)
{
    int t;
    if (ready) return;
    for (t = 0; t < K_SMP_TRACKS; t++) trk[t].id = -1;
    ready = 1;
}

void k_smp_set_dir(const char *d) { snprintf(dir, sizeof dir, "%s", d ? d : ""); }
const char *k_smp_dir(void) { return dir; }

int k_smp_full(const char *path, char *out, int n)
{
    int r;
    if (!path || !path[0] || n < 1) return 0;
    r = path[0] == '/' || !dir[0] ? snprintf(out, (size_t)n, "%s", path) : snprintf(out, (size_t)n, "%s/%s", dir, path);
    return r > 0 && r < n;
}

static void release(int id)
{
    if (id < 0) return;
    if (nlet < (int)(sizeof let_go / sizeof let_go[0])) let_go[nlet++] = id;
    else k_sample_free(id);   /* (never: collected every frame) */
}

int k_smp_assign(int t, const char *path)
{
    char full[K_SAMPLE_PATH_MAX];
    int id;
    setup();
    if (t < 0 || t >= K_SMP_TRACKS) return K_WAV_ERR_IO;
    if (!path || !path[0]) {
        release(trk[t].id);
        trk[t].id = -1;
        trk[t].path[0] = 0;
        gen++;
        return K_WAV_OK;
    }
    if (strlen(path) >= K_SMP_NAME || !k_smp_full(path, full, sizeof full)) return K_WAV_ERR_IO;
    id = k_sample_load(full);
    release(trk[t].id);   /* (a file not there: the track keeps its path, silent, and a song saved keeps it) */
    trk[t].id = id;
    snprintf(trk[t].path, sizeof trk[t].path, "%s", path);
    gen++;
    return id < 0 ? k_sample_last_error() : K_WAV_OK;
}

void k_smp_clear_all(void)
{
    int t;
    for (t = 0; t < K_SMP_TRACKS; t++) if (trk[t].path[0]) k_smp_assign(t, "");
}

const char *k_smp_path(int t) { return t >= 0 && t < K_SMP_TRACKS ? trk[t].path : ""; }
const KSample *k_smp_sample(int t) { setup(); return t >= 0 && t < K_SMP_TRACKS ? k_sample_get(trk[t].id) : 0; }
uint32_t k_smp_gen(void) { return gen; }

void k_smp_collect(void)
{
    while (nlet) k_sample_free(let_go[--nlet]);
}

static int wav_name(const char *s)
{
    const size_t n = strlen(s);
    return n > 4 && s[0] != '.' && (!strcmp(s + n - 4, ".wav") || !strcmp(s + n - 4, ".WAV") || !strcmp(s + n - 4, ".Wav"));
}
static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int k_smp_list(char (*names)[K_SMP_NAME], int max)
{
    DIR *d;
    struct dirent *e;
    int n = 0;
    if (!dir[0] || max < 1 || !(d = opendir(dir))) return 0;
    while ((e = readdir(d)) != 0 && n < max)
        if (wav_name(e->d_name) && strlen(e->d_name) < K_SMP_NAME) snprintf(names[n++], K_SMP_NAME, "%s", e->d_name);
    closedir(d);
    qsort(names, (size_t)n, K_SMP_NAME, by_name);
    return n;
}

int k_smp_step(int t, int d, char *out, int n)
{
    static char names[256][K_SMP_NAME];
    const char *cur = k_smp_path(t);
    int c = k_smp_list(names, 256), i, at = -1;
    if (!c) return -1;
    for (i = 0; i < c; i++) if (!strcmp(names[i], cur)) at = i;
    at = at < 0 ? (d < 0 ? c - 1 : 0) : ((at + d) % c + c) % c;
    snprintf(out, (size_t)n, "%s", names[at]);
    return 0;
}
