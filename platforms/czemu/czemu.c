/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Fune inside M5Stack's CardputerZero emulator (github.com/CardputerZero/Emulator: to try the Zero's screen and
 * keys before the device). The emulator dlopen()s an LVGL app and calls its ui_init(); it hands the app the SDL key events of its
 * skin's keyboard through lv_sdl_keyboard_handler(). Here: the Zero's profile drawn into an LVGL canvas each frame, the keys
 * through the SDL build's own input (platforms/sdl/input.c), the sound through SDL. Built by platforms/czemu/build.sh into
 * build_sim/ (not a target of its own: a test bench). */
#include "lvgl/lvgl.h"
#include <SDL.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "kreisel.h"
#include "kreisel_audio.h"
#include "kreisel_demo.h"
#include "ui/draw.h"
#include "input.h"
#include "midi.h"
#include "kreisel_io.h"
#include "kreisel_save.h"
#include "kreisel_store_file.h"
#include "desk.h"
#include "controllers.h"

#define W 320
#define H 170

static KApp app;
static KCursorVisual cursor;
static KAudio audio;
static SDL_AudioDeviceID audio_dev;
static lv_obj_t *canvas;
static uint32_t pixels[W * H];   /* 0xAARRGGBB: LVGL's ARGB8888, as the canvas draws it */

static void audio_callback(void *userdata, Uint8 *bytes, int length)
{
    k_audio_render((KAudio *)userdata, (float *)bytes, (size_t)length / (sizeof(float) * 2u));
}

static void frame(lv_timer_t *t)
{
    const uint32_t ms = SDL_GetTicks();
    static uint32_t last;
    const GCanvas *c;
    (void)t;
    if (ms - last < 15u) return;   /* (real time, about 60 a second: the emulator's LVGL clock runs 5 ms a pass of its loop, slower) */
    last = ms;
    sdl_midi_poll(&app, ms);   /* (the Mac's MIDI: CoreMIDI) */
    k_tick(&app, ms);
    if (audio_dev) {   /* as platforms/sdl/main.c: the notes to the voices, the sample clock as the playhead */
        KNoteEv ne;
        int playhead;
        SDL_LockAudioDevice(audio_dev);
        k_audio_sync(&audio, &app);
        while (k_take_note(&app, &ne)) k_audio_note(&audio, ne.track, ne.note, ne.vel, ne.vel > 0);
        playhead = audio.current_step;
        k_audio_song_follow(&audio, &app);
        SDL_UnlockAudioDevice(audio_dev);
        if (app.playing && playhead >= 0) { app.step = playhead; app.beat = (playhead / 4) & 3; }
    }
    {   /* (CZEMU_PROF: how long a frame's drawing takes, and how often frames come, every 2 s) */
        static uint64_t sum, n, t0; static uint32_t last_print, prev;
        uint64_t t = SDL_GetPerformanceCounter();
        ui_draw(&app, &cursor, ms);
        sum += SDL_GetPerformanceCounter() - t; n++;
        if (getenv("CZEMU_PROF") && ms - last_print >= 2000u) {
            printf("[fune] draw %.2f ms, a frame every %.1f ms\n", (double)sum * 1000.0 / (double)SDL_GetPerformanceFrequency() / (double)(n ? n : 1),
                   n ? (double)(ms - last_print) / (double)n : 0.0);
            fflush(stdout); sum = n = 0; last_print = ms;
        }
        (void)t0; (void)prev;
    }
    c = ui_draw_canvas();
    {   /* (CZEMU_SHOT=FILE.ppm: the screen written once, 3 s after the start: a check where no window can be looked at) */
        static uint32_t t_start; static int shot_done;
        const char *shot = getenv("CZEMU_SHOT");
        if (!t_start) t_start = ms ? ms : 1;
        if (shot && !shot_done && c && c->px && ms - t_start >= 3000u) {
            FILE *f = fopen(shot, "wb");
            int i;
            shot_done = 1;
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
                for (i = 0; i < c->w * c->h; i++) { const uint32_t px = c->px[i]; fputc((int)(px >> 16 & 255u), f); fputc((int)(px >> 8 & 255u), f); fputc((int)(px & 255u), f); }
                fclose(f);
                printf("[fune] screen written to %s\n", shot); fflush(stdout);
            }
        }
    }
    if (c && c->px && c->w == W && c->h == H) {
        if (memcmp(pixels, c->px, sizeof pixels)) {   /* (a frame like the last: nothing to redraw) */
            memcpy(pixels, c->px, sizeof pixels);
            lv_obj_invalidate(canvas);
            lv_refr_now(NULL);   /* now, not at LVGL's own refresh (33 ms of its slow clock) */
        }
    }
}

/* the song at power-on: as the SDL build's (platforms/sdl/main.c), the data folder its desk_dir (on the Mac ~/Library/Application
 * Support/Kreisel, on Linux ~/.local/share/kreisel: shared with the desktop build's songs and presets) */
static int have_store;
static int any_song(void) { int i; for (i = 0; i < K_SONG_SLOTS; i++) if (k_save_used(K_STORE_SONG, i)) return 1; return 0; }
static void power_on(const KProfile *p)
{
    k_init(&app);
    k_set_shown(&app, p->shown);
    k_set_layout(&app, k_profile_layout(p));
    if (have_store) {
        k_settings_load(&app);
        if (KREISEL_DEMO_SONG == 1) k_demo_load(&app);
        else if (!app.set_v[K_SET_RESTORE] && k_save_boot(&app) == 0) printf("[fune] song: slot %d loaded\n", app.song_from);
        else if (KREISEL_DEMO_SONG == 2 && !any_song()) k_demo_load(&app);
    } else if (KREISEL_DEMO_SONG) k_demo_load(&app);
}

void ui_init(void)
{
    const KProfile *p = k_profile_find("zero");
    static int done;
    if (done) { power_on(p); return; }   /* (POWER: anew) */
    done = 1;
    setvbuf(stdout, NULL, _IOLBF, 0);   /* (the log as it comes, also into a file) */
    if (!p || !ui_draw_init(p, K_THEME_DARK)) { fprintf(stderr, "[fune] draw init failed\n"); return; }
    {   char dir[1024];   /* SAVE's slots and the settings: files (s SAVE; the song written last opens at the next start) */
        if (desk_dir(dir, sizeof dir) && k_store_set(k_store_file(dir)) == 0) { have_store = 1; printf("[fune] files: %s\n", dir); }
        sdl_ctl_load(have_store ? dir : "");   /* the controllers' profiles (midi.c asks them of each MIDI source) */
    }
    power_on(p);
    sdl_input_set(p->input);   /* the Zero's: a keyboard alone */
    k_io_midi_map(k_io_map_desk, k_io_map_desk_n);
    sdl_midi_open("Fune");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want = {0}, have = {0};
        want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 512;
        want.callback = audio_callback; want.userdata = &audio;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev) { k_audio_init(&audio, (uint32_t)have.freq); k_audio_sync(&audio, &app); SDL_PauseAudioDevice(audio_dev, 0); }
    }
    canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, pixels, W, H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    lv_timer_create(frame, 1, NULL);   /* every pass of the emulator's loop; frame() keeps to about 60 a second in real time */
    printf("[fune] running on the Zero's profile (%dx%d)\n", W, H);
}

/* the emulator's keys: its own LVGL keypad not wanted, the SDL events straight to Fune's SDL input */
lv_indev_t *lv_sdl_keyboard_create(void) { return NULL; }
/* the emulator unloads this library (dlclose) before SDL_Quit: the sound must stop first, or its callback runs into code gone (a crash
 * on quitting) */
__attribute__((destructor)) static void fune_unload(void)
{
    sdl_midi_close();
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
}

/* the keys as they come (the Zero's orange keys work without Fn: platforms/sdl/input.c zero_key) */
void lv_sdl_keyboard_handler(SDL_Event *e)
{
    int running = 1;
    if (e->type == SDL_TEXTINPUT) return;
    sdl_process_event(&app, e, NULL, &running);
}
