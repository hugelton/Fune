/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Controller profiles for the SDL builds (controllers.h). */
#include "controllers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#endif

#define NPROF 16
static KCtlProfile prof[NPROF];
static const char *prof_text[NPROF];   /* a built-in's text (MIDI learn copies it into a file of the user's) */
static char prof_path[NPROF][1200];    /* a user's file; "" a built-in */
static int nprof;
static char ctl_dir[1024];             /* <data> (sdl_ctl_load's) */

static const char EXAMPLE[] =
    "# A controller profile for Fune (docs/CONTROLLERS.md). Copy this file to <name>.conf, set match to a part of your\n"
    "# controller's MIDI port name, and say what each knob, fader and button does. Fune reads this folder when it starts.\n"
    "name = Example\n"
    "match =                 # empty: never chosen (this file is only an example)\n"
    "cc 74 = knob 1          # the page's first knob, the CC's 0 .. 127 across its range\n"
    "cc 71 = knob 2\n"
    "cc 20 = rel 3           # an endless encoder (1 .. 63 up, 65 .. 127 down)\n"
    "cc 7 = level            # the track in hand's level; level 2: track 2's\n"
    "cc 1 = macro 1          # HOME's macros 1 .. 4\n"
    "cc 115 = play           # buttons: play rec undo tools ok back home menu save settings\n"
    "cc 116 = rec\n"
    "cc 112 = track-         # track+ track- select+ select- pattern+ pattern- value+ value- octave+ octave-\n"
    "cc 113 = track+\n"
    "cc 102 = page seq       # page seq glo synth edit env lfo fx arp\n"
    "note 36 ch 10 = tools   # a pad's note as a button (ch: only that channel's)\n"
    "note any = note         # notes played on the track in hand\n";

static void add(const char *text, const char *from, const char *path)   /* path: a user's file, 0 a built-in */
{
    static KCtlProfile p;
    char err[64];
    int i;
    if (k_ctl_parse(text, &p, err, sizeof err) != 0) { fprintf(stderr, "controllers: %s: %s\n", from, err); return; }
    for (i = 0; i < nprof && strcmp(prof[i].name, p.name); i++) {}   /* (the user's in a built-in's place) */
    if (i == nprof && nprof >= NPROF) { fprintf(stderr, "controllers: %s: more than %d profiles\n", from, NPROF); return; }
    if (i == nprof) nprof++;
    prof[i] = p; prof_text[i] = path ? 0 : text;
    snprintf(prof_path[i], sizeof prof_path[i], "%s", path ? path : "");
}
static void remap(void) { int i; for (i = 0; i < KIO_DEVS; i++) k_io_dev_map(i + 1, i < nprof ? prof[i].midi : 0, i < nprof ? prof[i].n_midi : 0); }
static char *slurp(const char *path)   /* a file's text (under 64 KB), 0 none */
{
    FILE *f = fopen(path, "rb");
    char *buf = 0;
    long len;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len >= 0 && len < 65536 && (buf = malloc((size_t)len + 1))) buf[fread(buf, 1, (size_t)len, f)] = 0;
    fclose(f);
    return buf;
}
int sdl_ctl_load(const char *dir)
{
    int i;
    nprof = 0;
    snprintf(ctl_dir, sizeof ctl_dir, "%s", dir ? dir : "");
    for (i = 0; i < k_ctl_builtin_n; i++) add(k_ctl_builtin[i], "built-in", 0);
#if !defined(_WIN32)
    if (dir && dir[0]) {
        char path[1200];
        DIR *d;
        snprintf(path, sizeof path, "%s/controllers", dir);
        if (!(d = opendir(path))) {   /* the first time: the folder, and an example to copy */
            FILE *f;
            mkdir(path, 0755);
            snprintf(path, sizeof path, "%s/controllers/example.conf", dir);
            if ((f = fopen(path, "w"))) { fputs(EXAMPLE, f); fclose(f); }
        } else {
            struct dirent *e;
            while ((e = readdir(d))) {
                const size_t n = strlen(e->d_name);
                char *buf;
                if (n < 6 || strcmp(e->d_name + n - 5, ".conf") || e->d_name[0] == '.') continue;
                snprintf(path, sizeof path, "%s/controllers/%s", dir, e->d_name);
                if ((buf = slurp(path)) != 0) { if (buf[0]) add(buf, e->d_name, path); free(buf); }
            }
            closedir(d);
        }
    }
#endif
    remap();
    fprintf(stderr, "controllers: %d profile%s\n", nprof, nprof == 1 ? "" : "s");
    return nprof;
}
int sdl_ctl_dev(const char *port_name)
{
    int i;
    for (i = 0; i < nprof && i < KIO_DEVS; i++)
        if (k_ctl_matches(&prof[i], port_name)) { fprintf(stderr, "controllers: %s: %s\n", port_name, prof[i].name); return i + 1; }
    return 0;
}
const KCtlProfile *sdl_ctl_find(const char *name)
{
    int i;
    for (i = 0; i < nprof; i++) {
        const char *a = prof[i].name, *b = name;
        while (*a && *b && ((*a | 32) == (*b | 32))) { a++; b++; }
        if (!*a && !*b) return &prof[i];
    }
    return 0;
}

/* MIDI learn (kreisel_io.h k_learn_*): the row into the profile of the port it came from, that profile read again in its place */
int sdl_ctl_learn(const char *port, const char *row)
{
#if !defined(_WIN32)
    static char out[65536 + 128];
    char name[32], file[1200], *old = 0;
    const char *from = 0;
    int i, k, at = -1, rc;
    FILE *f;
    if (!ctl_dir[0] || !port || !row) return -1;
    for (i = 0; i < nprof && i < KIO_DEVS; i++) if (k_ctl_matches(&prof[i], port)) { at = i; break; }
    name[0] = 0;
    if (at >= 0 && prof_path[at][0]) {   /* a file of the user's: the row into it */
        snprintf(file, sizeof file, "%s", prof_path[at]);
        if (!(old = slurp(file))) return -1;
    } else {   /* a built-in (its text into a file of the user's, which takes its place), or none: a new profile named after the port */
        struct stat st;
        char base[40];
        if (at >= 0) { snprintf(name, sizeof name, "%s", prof[at].name); from = prof_text[at]; }
        else {   /* (the port's name up to a character a profile cannot hold: the match is still a part of the name) */
            for (k = 0; port[k] && k < (int)sizeof name - 1 && port[k] >= ' ' && port[k] < 127 && port[k] != '#'; k++) name[k] = port[k];
            while (k && name[k - 1] == ' ') k--;
            name[k] = 0;
            for (k = 0; name[k] == ' '; k++) {}
            if (!name[k]) return -1;
        }
        for (k = 0; name[k] && k < (int)sizeof base - 1; k++) {
            const char c = name[k];
            base[k] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
        }
        base[k] = 0;
        snprintf(file, sizeof file, "%s/controllers", ctl_dir);
        mkdir(file, 0755);   /* (there already: nothing) */
        snprintf(file, sizeof file, "%s/controllers/%s.conf", ctl_dir, base);
        for (k = 2; stat(file, &st) == 0 && k < 100; k++) snprintf(file, sizeof file, "%s/controllers/%s-%d.conf", ctl_dir, base, k);   /* (never over another file) */
    }
    rc = k_ctl_learn_text(old ? old : from, name, name, row, out, sizeof out);
    free(old);
    if (rc != 0 || !(f = fopen(file, "w"))) return -1;
    rc = fputs(out, f) < 0;
    if (fclose(f) != 0 || rc) return -1;
    fprintf(stderr, "controllers: %s: %s (%s)\n", port, row, file);
    add(out, file, file);   /* (in its place: the others keep theirs, and their places, a keys profile too) */
    remap();
    return 0;
#else
    (void)port; (void)row;
    return -1;
#endif
}
