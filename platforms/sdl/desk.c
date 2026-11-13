/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "desk.h"
#include "kreisel_song.h"
#include "kreisel_save.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int mkdirs(const char *path)   /* mkdir -p */
{
    char p[1024];
    size_t i, n = strlen(path);
    if (n >= sizeof p) return -1;
    memcpy(p, path, n + 1);
    for (i = 1; i <= n; i++)
        if (p[i] == '/' || p[i] == 0) {
            const char c = p[i];
            p[i] = 0;
            if (mkdir(p, 0755) != 0 && errno != EEXIST) return -1;
            p[i] = c;
        }
    return 0;
}
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

int desk_dir(char *out, int n)
{
    const char *home = getenv("HOME");
    char old[1024];
    if (!home || !*home) return 0;
#ifdef __APPLE__
    if (snprintf(out, (size_t)n, "%s/Library/Application Support/Kreisel", home) >= n) return 0;
#else
    {   const char *xdg = getenv("XDG_DATA_HOME");
        if ((xdg && *xdg ? snprintf(out, (size_t)n, "%s/kreisel", xdg) : snprintf(out, (size_t)n, "%s/.local/share/kreisel", home)) >= n) return 0; }
#endif
    if (snprintf(old, sizeof old, "%s/.kreisel", home) < (int)sizeof old && !is_dir(out) && is_dir(old)) {   /* the prototype's folder: moved once */
        char parent[1024];
        char *slash;
        snprintf(parent, sizeof parent, "%s", out);
        slash = strrchr(parent, '/');
        if (slash) { *slash = 0; mkdirs(parent); }
        if (rename(old, out) == 0) fprintf(stderr, "kreisel: %s moved to %s\n", old, out);
    }
    if (mkdirs(out) != 0) return 0;
    return 1;
}

static void path_of(char *p, size_t n, const char *dir, const char *name) { snprintf(p, n, "%s/%s", dir, name); }

int desk_session_save(const KApp *a, const char *dir, const DeskWindow *win)
{
    static uint8_t buf[16384];
    char p[1100], tmp[1110];
    size_t len;
    FILE *f;
    if (k_song_max_size() > sizeof buf) return -1;
    len = k_song_save(a, buf, sizeof buf);
    if (!len) return -1;
    path_of(p, sizeof p, dir, "session.krsg");
    snprintf(tmp, sizeof tmp, "%s.new", p);   /* written whole, then put in place (a crash mid-write keeps the last one) */
    f = fopen(tmp, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
    if (fclose(f) != 0 || rename(tmp, p) != 0) { remove(tmp); return -1; }
    path_of(p, sizeof p, dir, "session.txt");
    f = fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "track %d\nmaster %d\noctave %d\nsong_from %d\npage %d\n", a->track, a->master, a->octave, a->song_from, (int)a->page);   /* (the page last: it opens on the track) */
    if (win && win->ok) fprintf(f, "window %d %d %d %d\n", win->x, win->y, win->w, win->h);
    return fclose(f) == 0 ? 0 : -1;
}

int desk_session_load(KApp *a, const char *dir, DeskWindow *win)
{
    static uint8_t buf[16384];
    char p[1100], line[128];
    size_t len;
    FILE *f;
    int v, x, y, w, h, loaded = 0;
    if (win) win->ok = 0;
    path_of(p, sizeof p, dir, "session.krsg");
    f = fopen(p, "rb");
    if (f) {
        len = fread(buf, 1, sizeof buf, f);
        fclose(f);
        loaded = len && k_song_load(a, buf, len) == K_SONG_OK;
    }
    path_of(p, sizeof p, dir, "session.txt");
    f = fopen(p, "r");
    if (!f) return loaded ? 0 : -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "window %d %d %d %d", &x, &y, &w, &h) == 4 && win && w >= 160 && h >= 90) { win->x = x; win->y = y; win->w = w; win->h = h; win->ok = 1; }
        else if (sscanf(line, "master %d", &v) == 1) a->master = v < 0 ? 0 : v > 100 ? 100 : v;
        else if (sscanf(line, "octave %d", &v) == 1) a->octave = v < -3 ? -3 : v > 3 ? 3 : v;
        else if (!loaded) continue;   /* (the song's own: only over the song it went with) */
        else if (sscanf(line, "track %d", &v) == 1 && v >= 0 && v < K_TRACKS) a->track = v;
        else if (sscanf(line, "song_from %d", &v) == 1 && v >= 0 && v < 256) a->song_from = (uint8_t)v;
        else if (sscanf(line, "page %d", &v) == 1 && v >= 0 && v < K_PAGE_COUNT) k_open_page(a, (KPage)v);
    }
    fclose(f);
    return loaded ? 0 : -1;
}

int desk_open_file(KApp *a, const char *path)
{
    static uint8_t buf[K_SAVE_BUF + 1];
    char name[K_NAME_LEN + 1];
    size_t len;
    int rc;
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "open: %s: cannot read\n", path); return -1; }
    len = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (len == sizeof buf) { fprintf(stderr, "open: %s: too big for a song or a preset\n", path); return -1; }
    if ((rc = k_save_open_file(a, buf, len)) != K_SONG_OK) { fprintf(stderr, "open: %s: refused (%d), nothing changed\n", path, rc); return -1; }
    k_file_info(buf, len, name, 0);
    fprintf(stderr, "open: %s: %s %s\n", path, buf[3] == 'P' ? "a preset onto the track in hand," : "the song", name);
    return 0;
}
