/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* io.conf (ioconf.h). */
#include "controllers.h"
#include "ioconf.h"
#include "kreisel_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define AUDIO_CONF \
    "\n" \
    "[audio]\n" \
    "audio_out =         # a device's name (kreisel --audio-devices lists them); empty: the system's own\n" \
    "audio_rate = 0      # Hz (48000, 44100); 0: the device's own\n" \
    "audio_buffer = 512  # frames, a power of two (256 .. 2048: lower is quicker, higher is safer)\n" \
    "audio_in =          # a capture device's name, for recording later; empty: none\n"

static const char DEFAULT_CONF[] =
    "# Fune: inputs. Edited by hand, or by the settings' Connect tab (it rewrites the values only).\n"
    "\n"
    "[midi]\n"
    "in = all          # all, none, or a part of a port's name (\"IAC\", \"KeyStep\")\n"
    "channel = all     # all, or 1 .. 16\n"
    "clock = internal  # internal, or follow (the MIDI clock in)\n"
    "out = on          # on (the notes, the clock out to the \"Fune\" port), or off\n"
    "\n"
    "[keys]\n"
    "map = default     # default (the device's own keys), none, or a controller profile's name (its key rows)\n"
    AUDIO_CONF;

static int midi_out = 1;
int sdl_ioconf_midi_out(void) { return midi_out; }
static char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
static void path_of(char *out, size_t n, const char *dir) { snprintf(out, n, "%s/io.conf", dir); }

/* [audio] (any build, Connect or not): read before the output opens; a file without the part gets it appended */
void sdl_ioconf_audio(const char *dir, SdlAudioConf *o)
{
    char path[1100], line[256], sec[32] = "";
    int seen = 0;
    FILE *f;
    memset(o, 0, sizeof *o);
    o->buffer = 512;
    if (!dir || !dir[0]) return;
    path_of(path, sizeof path, dir);
    if (!(f = fopen(path, "r"))) {
        if ((f = fopen(path, "w"))) { fputs(DEFAULT_CONF, f); fclose(f); }
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *s, *eq, *k, *v, *hash = strchr(line, '#');
        if (hash) *hash = 0;
        s = trim(line);
        if (*s == '[') { char *e = strchr(s, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", trim(s + 1)); } continue; }
        if (strcmp(sec, "audio") || !(eq = strchr(s, '='))) continue;
        *eq = 0;
        k = trim(s); v = trim(eq + 1);
        if (!strcmp(k, "audio_out")) { snprintf(o->out, sizeof o->out, "%s", v); seen |= 1; }
        else if (!strcmp(k, "audio_rate")) { o->rate = atoi(v); seen |= 2; }
        else if (!strcmp(k, "audio_buffer")) { o->buffer = atoi(v); seen |= 4; }
        else if (!strcmp(k, "audio_in")) { snprintf(o->in, sizeof o->in, "%s", v); seen |= 8; }
    }
    fclose(f);
    if (o->rate < 0 || o->rate > 384000) o->rate = 0;
    if (o->buffer < 64) o->buffer = 64;
    if (o->buffer > 8192) o->buffer = 8192;
    { int p = 64; while (p * 2 <= o->buffer) p *= 2; o->buffer = p; }   /* (a power of two, the one at or below) */
    if (!seen && (f = fopen(path, "a"))) { fputs(AUDIO_CONF, f); fclose(f); }   /* (an older file: its [audio] part, with the defaults) */
}

#if K_WITH_CONNECT

/* one value read: [section] key = value */
static void apply(KApp *a, const char *sec, const char *key, const char *val)
{
    if (!strcmp(sec, "midi") && !strcmp(key, "in")) {
        if (!strcmp(val, "all")) { a->set_v[K_SET_MIDI_IN] = 0; k_io_set_in_name(""); }
        else if (!strcmp(val, "none")) { a->set_v[K_SET_MIDI_IN] = 1; k_io_set_in_name(""); }
        else { a->set_v[K_SET_MIDI_IN] = 2; k_io_set_in_name(val); }
    } else if (!strcmp(sec, "midi") && !strcmp(key, "channel")) {
        const int c = atoi(val);
        a->set_v[K_SET_MIDI_CH] = (uint8_t)(c >= 1 && c <= 16 ? c : 0);
    } else if (!strcmp(sec, "midi") && !strcmp(key, "clock")) k_param_set(a, K_PAGE_CLOCK, 2, !strcmp(val, "follow") ? 1 : 0);   /* (the song's clock source: through the values' table) */
    else if (!strcmp(sec, "midi") && !strcmp(key, "out")) midi_out = strcmp(val, "off") != 0;   /* (here, not a settings row) */
    else if (!strcmp(sec, "keys") && !strcmp(key, "map") && !strcmp(val, "none")) k_io_key_map(0, 0);
    else if (!strcmp(sec, "keys") && !strcmp(key, "map") && strcmp(val, "default")) {   /* a profile's keys by its name (controllers.h) */
        const KCtlProfile *p = sdl_ctl_find(val);
        if (p && p->n_keys) k_io_key_map(p->keys, p->n_keys); else fprintf(stderr, "io.conf: [keys] map = %s: no profile with keys of that name\n", val);
    }
}
int sdl_ioconf_load(KApp *a, const char *dir)
{
    char path[1100], line[256], sec[32] = "";
    FILE *f;
    path_of(path, sizeof path, dir);
    if (!(f = fopen(path, "r"))) {   /* none yet: the defaults, written down to be edited */
        if ((f = fopen(path, "w"))) { fputs(DEFAULT_CONF, f); fclose(f); }
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *s, *eq, *hash = strchr(line, '#');
        if (hash) *hash = 0;
        s = trim(line);
        if (*s == '[') { char *e = strchr(s, ']'); if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", trim(s + 1)); } continue; }
        if (!(eq = strchr(s, '='))) continue;
        *eq = 0;
        apply(a, sec, trim(s), trim(eq + 1));
    }
    fclose(f);
    fprintf(stderr, "io.conf: read %s\n", path);
    return 0;
}

/* the file again, each known value where it stands (its comment kept), a missing one added under its section */
static void write_back(const char *dir, const char *const keys[][2], const char *const vals[], int n)
{
    char path[1100], tmp[1110], line[256], sec[32] = "";
    int done[8] = {0}, i;
    FILE *in, *out;
    path_of(path, sizeof path, dir);
    snprintf(tmp, sizeof tmp, "%s.new", path);
    in = fopen(path, "r");
    if (!(out = fopen(tmp, "w"))) { if (in) fclose(in); return; }
    if (!in) fputs(DEFAULT_CONF, out);   /* (gone meanwhile: the defaults, then the values below) */
    while (in && fgets(line, sizeof line, in)) {
        char copy[256], *s, *eq;
        int hit = -1;
        snprintf(copy, sizeof copy, "%s", line);
        { char *h = strchr(copy, '#'); if (h) *h = 0; }
        s = trim(copy);
        if (*s == '[') {
            char *e = strchr(s, ']');
            for (i = 0; i < n; i++) if (!done[i] && !strcmp(keys[i][0], sec)) { fprintf(out, "%s = %s\n", keys[i][1], vals[i]); done[i] = 1; }   /* (missing in the last one) */
            if (e) { *e = 0; snprintf(sec, sizeof sec, "%s", trim(s + 1)); }
            fputs(line, out);
            continue;
        }
        if ((eq = strchr(s, '='))) { *eq = 0; for (i = 0; i < n; i++) if (!strcmp(keys[i][0], sec) && !strcmp(keys[i][1], trim(s))) hit = i; }
        if (hit >= 0) {
            const char *c = strchr(line, '#');
            char v[96];
            snprintf(v, sizeof v, "%s = %s", keys[hit][1], vals[hit]);
            if (c) fprintf(out, "%-17s %s", v, c); else fprintf(out, "%s\n", v);
            done[hit] = 1;
        } else fputs(line, out);
    }
    for (i = 0; i < n; i++) if (!done[i]) fprintf(out, "[%s]\n%s = %s\n", keys[i][0], keys[i][1], vals[i]);   /* (a section it never had) */
    if (in) fclose(in);
    fclose(out);
    rename(tmp, path);
}
void sdl_ioconf_sync(const KApp *a, const char *dir)
{
    static int seen = -1;
    static char seen_name[32];
    static const char *const KEYS[3][2] = {{"midi", "in"}, {"midi", "channel"}, {"midi", "clock"}};
    const int now = a->set_v[K_SET_MIDI_IN] | a->set_v[K_SET_MIDI_CH] << 2 | (a->clock_src ? 1 : 0) << 7;
    char ch[8];
    const char *vals[3];
    if (!dir || !dir[0]) return;
    if (seen < 0 || (now == seen && !strcmp(seen_name, k_io_in_name()))) { seen = now; snprintf(seen_name, sizeof seen_name, "%s", k_io_in_name()); return; }
    seen = now; snprintf(seen_name, sizeof seen_name, "%s", k_io_in_name());
    snprintf(ch, sizeof ch, "%d", a->set_v[K_SET_MIDI_CH]);
    vals[0] = a->set_v[K_SET_MIDI_IN] == 1 ? "none" : a->set_v[K_SET_MIDI_IN] == 2 && k_io_in_name()[0] ? k_io_in_name() : "all";
    vals[1] = a->set_v[K_SET_MIDI_CH] ? ch : "all";
    vals[2] = a->clock_src ? "follow" : "internal";
    write_back(dir, KEYS, vals, 3);
}
#else
int sdl_ioconf_load(KApp *a, const char *dir) { (void)a; (void)dir; return -1; }
void sdl_ioconf_sync(const KApp *a, const char *dir) { (void)a; (void)dir; }
#endif
