/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAVE's store as files (kreisel_store_file.h): the desktop builds. <dir>/songs/song1.krs .. song6.krs and
 * <dir>/presets/preset01.krp .. preset24.krp; the song written last is the newest file. A song saved before the slots
 * (<dir>/song.krs, the SDL build's F9) reads as slot 1 until slot 1 is written. */
#include "kreisel_store_file.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0755)
#endif

static char base[900];

static int path_of(char *out, size_t cap, int kind, int slot, int legacy)
{
    if (legacy) return snprintf(out, cap, "%s/song.krs", base) < (int)cap;
    if (kind == K_STORE_SONG) return snprintf(out, cap, "%s/songs/song%d.krs", base, slot + 1) < (int)cap;
    return snprintf(out, cap, "%s/presets/preset%02d.krp", base, slot + 1) < (int)cap;
}
static long f_read(void *c, int kind, int slot, uint8_t *buf, size_t cap)
{
    char p[1024];
    FILE *f;
    size_t n;
    (void)c;
    if (!path_of(p, sizeof p, kind, slot, 0)) return -1;
    f = fopen(p, "rb");
    if (!f && kind == K_STORE_SONG && slot == 0 && path_of(p, sizeof p, kind, slot, 1)) f = fopen(p, "rb");
    if (!f) return 0;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}
static int f_write(void *c, int kind, int slot, const uint8_t *buf, size_t n)
{
    char p[1024], tmp[1040];
    FILE *f;
    (void)c;
    if (!path_of(p, sizeof p, kind, slot, 0) || snprintf(tmp, sizeof tmp, "%s.tmp", p) >= (int)sizeof tmp) return -1;
    {   /* the folders, as needed */
        char d[1024];
        snprintf(d, sizeof d, "%s", base); mkdir_one(d);
        snprintf(d, sizeof d, "%s/%s", base, kind == K_STORE_SONG ? "songs" : "presets"); mkdir_one(d);
    }
    if (!(f = fopen(tmp, "wb"))) return -1;
    if (fwrite(buf, 1, n, f) != n) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) != 0) { remove(tmp); return -1; }
    remove(p);   /* (Windows' rename does not replace) */
    return rename(tmp, p) == 0 ? 0 : -1;   /* whole, or the old one: never half */
}
static int f_erase(void *c, int kind, int slot)
{
    char p[1024];
    (void)c;
    if (!path_of(p, sizeof p, kind, slot, 0)) return -1;
    remove(p);
    if (kind == K_STORE_SONG && slot == 0 && path_of(p, sizeof p, kind, slot, 1)) remove(p);
    return 0;
}
static int f_latest(void *c)
{
    char p[1024];
    struct stat st;
    long best = -1;
    int i, at = -1;
    (void)c;
    for (i = 0; i < K_SONG_SLOTS; i++)
        if (path_of(p, sizeof p, K_STORE_SONG, i, 0) && stat(p, &st) == 0 && (long)st.st_mtime >= best) { best = (long)st.st_mtime; at = i; }
    if (at < 0 && path_of(p, sizeof p, K_STORE_SONG, 0, 1) && stat(p, &st) == 0) at = 0;
    return at;
}
static long f_read_set(void *c, uint8_t *buf, size_t cap)   /* <dir>/settings.krt */
{
    char p[1024];
    FILE *f;
    size_t n;
    (void)c;
    if (snprintf(p, sizeof p, "%s/settings.krt", base) >= (int)sizeof p || !(f = fopen(p, "rb"))) return 0;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return (long)n;
}
static int f_write_set(void *c, const uint8_t *buf, size_t n)
{
    char p[1024], tmp[1040];
    FILE *f;
    (void)c;
    if (snprintf(p, sizeof p, "%s/settings.krt", base) >= (int)sizeof p || snprintf(tmp, sizeof tmp, "%s.tmp", p) >= (int)sizeof tmp) return -1;
    mkdir_one(base);
    if (!(f = fopen(tmp, "wb"))) return -1;
    if (fwrite(buf, 1, n, f) != n) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) != 0) { remove(tmp); return -1; }
    remove(p);
    return rename(tmp, p) == 0 ? 0 : -1;
}
static const KStore FILE_STORE = {f_read, f_write, f_erase, f_latest, 0, f_read_set, f_write_set};

const KStore *k_store_file(const char *dir)
{
    if (!dir || snprintf(base, sizeof base, "%s", dir) >= (int)sizeof base) return 0;
    return &FILE_STORE;
}
