/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler on SDL (sampler.h). The input is opened only while a Sampler track's SOUND or EDIT page is up (a microphone is
 * not held open for nothing; macOS asks for it the first time), at the engines' rate, mono: SDL converts. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "sampler.h"
#include "kreisel_audio.h"
#include "kreisel_plugin.h"
#include "sample/k_rec.h"
#include "sample/k_smp.h"
#include "sample/k_browse.h"
#include "sample/k_wav.h"

#define REC_SECONDS 60
#define REC_RATE 44100

static SDL_AudioDeviceID cap;
static char in_name[128];
static int in_none, rec_track = -1, last_state, refused;
static uint32_t closed_at;
static int eat_home_up;   /* (HOME closed the browser: its release is not BACK too) */

static void capture_cb(void *u, Uint8 *bytes, int len)
{
    (void)u;
    k_rec_input((const float *)bytes, (uint32_t)len / sizeof(float), 1);
}

static int sampler_page(const KApp *a)
{
    return (a->page == K_PAGE_SOUND || a->page == K_PAGE_EDIT) && !k_track_missing(a, a->track) &&
           k_engine_uid(a->sound[a->track]) == K_SAMPLER_UID;
}

static int open_input(void)
{
    SDL_AudioSpec want = {0}, have = {0};
    if (cap) return 1;
    if (in_none || refused || !SDL_WasInit(SDL_INIT_AUDIO)) return 0;
    want.freq = REC_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = capture_cb;
    cap = SDL_OpenAudioDevice(in_name[0] ? in_name : NULL, 1, &want, &have, 0);
    if (!cap && in_name[0]) {
        fprintf(stderr, "sampler: input \"%s\" did not open (%s); the system's instead\n", in_name, SDL_GetError());
        cap = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
    }
    if (!cap) { fprintf(stderr, "sampler: no input (%s)\n", SDL_GetError()); refused = 1; return 0; }
    if (k_rec_init(REC_RATE, REC_RATE * REC_SECONDS) != 0) { SDL_CloseAudioDevice(cap); cap = 0; refused = 1; return 0; }
    fprintf(stderr, "sampler: input open (%s, %d Hz)\n", in_name[0] ? in_name : "the system's", have.freq);
    SDL_PauseAudioDevice(cap, 0);
    k_rec_set_live(1);
    return 1;
}

static void close_input(void)
{
    if (!cap) return;
    SDL_CloseAudioDevice(cap);
    cap = 0;
    k_rec_set_live(0);
}

/* a recording that ended: written into the folder as rec-DATE-TIME.wav, given to the track it was made on */
static void take_recording(KApp *a)
{
    char name[64], full[1100];
    time_t now = time(0);
    struct tm tmv;
    int db, r;
    (void)a;
    if (rec_track < 0 || !k_rec_frames()) { if (rec_track >= 0) fprintf(stderr, "sampler: nothing heard, nothing kept (the input's level %d of 32767)\n", k_rec_level()); rec_track = -1; return; }
    if (cap) SDL_LockAudioDevice(cap);
    k_rec_trim(K_REC_THRESHOLD);   /* (the silence before the sound: off) */
    db = k_rec_normalize(24);
    if (cap) SDL_UnlockAudioDevice(cap);
    localtime_r(&now, &tmv);
    strftime(name, sizeof name, "rec-%Y%m%d-%H%M%S.wav", &tmv);
    if (!k_smp_full(name, full, sizeof full) || (r = k_rec_save(full)) != K_WAV_OK) {
        fprintf(stderr, "sampler: the recording was not written (%s)\n", k_smp_dir());
        rec_track = -1;
        return;
    }
    fprintf(stderr, "sampler: %s, %.2f s (+%d dB), on track %d\n", full, (double)k_rec_frames() / REC_RATE, db, rec_track + 1);
    if ((r = k_smp_assign(rec_track, name)) != K_WAV_OK) fprintf(stderr, "sampler: %s: %s\n", name, k_wav_error(r));
    rec_track = -1;
}

static int sign(int v) { return (v > 0) - (v < 0); }
/* the file browser open (k_browse.h): the cursor, the knobs and PRESETS move through the files (each given to the track at once:
 * the keys play it), OK and TOOLS keep it, BACK and HOME put the track's file back; the keys, PLAY and the pages pass */
static int browse_hook(KApp *a, const KAction *e)
{
    (void)a;
    switch (e->type) {
    case K_ACT_UP: k_browse_move(-1); return 1;
    case K_ACT_DOWN: k_browse_move(1); return 1;
    case K_ACT_LEFT: k_browse_move(-5); return 1;
    case K_ACT_RIGHT: k_browse_move(5); return 1;
    case K_ACT_SELECT_DELTA: case K_ACT_VALUE_DELTA: case K_ACT_SOUND_DELTA: case K_ACT_TYPE_DELTA: if (e->a) k_browse_move(sign(e->a)); return 1;
    case K_ACT_PARAM_DELTA: if (e->b) k_browse_move(sign(e->b)); return 1;
    case K_ACT_ACCEPT: case K_ACT_TOOLS: k_browse_close(1); return 1;
    case K_ACT_BACK: k_browse_close(0); return 1;
    case K_ACT_HOME_DOWN: k_browse_close(0); eat_home_up = 1; return 1;
    case K_ACT_REC: case K_ACT_SAVE: case K_ACT_FOCUS_NEXT: case K_ACT_FOCUS_PREV: return 1;
    default: return 0;
    }
}

static int hook(KApp *a, const KAction *e)
{
    if (e->type == K_ACT_HOME_UP && eat_home_up) { eat_home_up = 0; return 1; }
    if (k_browse_is_open()) {
        if (!sampler_page(a) || a->track != k_browse_track()) k_browse_close(1);   /* (left the page or the track: what is in hand stays) */
        else return browse_hook(a, e);
    }
    if (!sampler_page(a) || a->home_held) return 0;   /* (HOME + REC: undo, as everywhere) */
    if (e->type == K_ACT_TOOLS && a->page == K_PAGE_SOUND) {   /* TOOLS on the Sampler: its files */
        if (k_rec_state() == K_REC_IDLE) k_browse_open(a->track);
        return 1;
    }
    if (e->type == K_ACT_REC) {
        const int st = k_rec_state();
        if (st == K_REC_IDLE) {
            if (!open_input()) return 1;
            SDL_LockAudioDevice(cap);
            k_rec_start(0);   /* (at once; the silence before the first sound is cut when it is kept: a quiet input still records) */
            SDL_UnlockAudioDevice(cap);
            rec_track = a->track;
            last_state = K_REC_ON;
            fprintf(stderr, "sampler: recording on track %d\n", rec_track + 1);
        } else {
            SDL_LockAudioDevice(cap);
            k_rec_stop();
            SDL_UnlockAudioDevice(cap);
            last_state = K_REC_IDLE;
            take_recording(a);
        }
        return 1;
    }
    if (e->type == K_ACT_SOUND_DELTA && e->a) {   /* PRESETS: the next file in the folder */
        char name[K_SMP_NAME];
        if (k_rec_state() == K_REC_IDLE && k_smp_step(a->track, e->a > 0 ? 1 : -1, name, sizeof name) == 0) {
            const int r = k_smp_assign(a->track, name);
            if (r != K_WAV_OK) fprintf(stderr, "sampler: %s: %s\n", name, k_wav_error(r));
        }
        return 1;
    }
    return 0;
}

static int mkdirs(const char *p)
{
    char b[1100];
    size_t i;
    snprintf(b, sizeof b, "%s", p);
    for (i = 1; b[i]; i++) if (b[i] == '/') { b[i] = 0; mkdir(b, 0755); b[i] = '/'; }
    mkdir(b, 0755);
    { struct stat st; return stat(b, &st) == 0 && S_ISDIR(st.st_mode); }
}

void sdl_sampler_init(const char *data_dir)
{
    int r = k_plugin_register_host(&K_SAMPLER_PLUGIN);
    if (r < 0) fprintf(stderr, "sampler: not registered (%d)\n", r);
    if (data_dir && data_dir[0]) {
        char d[1100];
        snprintf(d, sizeof d, "%s/samples", data_dir);
        mkdirs(d);
        k_smp_set_dir(d);
    }
    k_action_hook = hook;
}

void sdl_sampler_input(const char *name)
{
    in_none = name && !strcmp(name, "none");
    snprintf(in_name, sizeof in_name, "%s", name && !in_none ? name : "");
}

void sdl_sampler_tick(KApp *a)
{
    const int st = k_rec_state();
    if (k_browse_is_open() && (!sampler_page(a) || a->page != K_PAGE_SOUND || a->track != k_browse_track())) k_browse_close(1);
    if (last_state != K_REC_IDLE && st == K_REC_IDLE) { last_state = st; take_recording(a); }   /* (it filled up: stopped by itself) */
    else last_state = st;
    if (sampler_page(a)) { open_input(); closed_at = 0; }
    else if (cap && st == K_REC_IDLE) {   /* the page left: the input closed a little later (back and forth between pages: kept) */
        if (!closed_at) closed_at = SDL_GetTicks();
        else if (SDL_GetTicks() - closed_at > 3000u) { close_input(); closed_at = 0; }
    }
}

static int is_wav(const char *p)
{
    const size_t n = strlen(p);
    return n > 4 && (!strcmp(p + n - 4, ".wav") || !strcmp(p + n - 4, ".WAV"));
}

int sdl_sampler_drop(KApp *a, const char *path)
{
    const char *base = strrchr(path, '/');
    char dst[1100];
    int r;
    if (!is_wav(path)) return 0;
    base = base ? base + 1 : path;
    if (k_engine_uid(a->sound[a->track]) != K_SAMPLER_UID) { fprintf(stderr, "sampler: %s: the track in hand does not play the Sampler\n", base); return 1; }
    if (strlen(base) >= K_SMP_NAME || !k_smp_full(base, dst, sizeof dst)) return 1;
    if (strcmp(dst, path) != 0) {   /* into the folder (a song keeps the file's name there): copied, unless one of that name is there */
        struct stat st;
        if (stat(dst, &st) != 0) {
            FILE *in = fopen(path, "rb"), *out = in ? fopen(dst, "wb") : 0;
            char b[65536];
            size_t n;
            int ok = in && out;
            while (ok && (n = fread(b, 1, sizeof b, in)) > 0) ok = fwrite(b, 1, n, out) == n;
            if (in) fclose(in);
            if (out && fclose(out) != 0) ok = 0;
            if (!ok) { remove(dst); fprintf(stderr, "sampler: %s not copied into %s\n", base, k_smp_dir()); return 1; }
        }
    }
    r = k_smp_assign(a->track, base);
    fprintf(stderr, "sampler: %s on track %d%s%s\n", base, a->track + 1, r ? ": " : "", r ? k_wav_error(r) : "");
    return 1;
}

void sdl_sampler_shutdown(void)
{
    close_input();
    k_rec_shutdown();
}
