/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The browser's side of the UI: the core and the canvas as WebAssembly. The page (app.html) owns the clock, the
 * panel and the pointer; this file turns them into actions and hands back the picture as RGBA. */
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include "draw.h"
#include "kreisel.h"
#include "kreisel_plugin.h"
#include "kreisel_fx.h"
#include "kreisel_audio.h"
#include "kreisel_save.h"
#include <string.h>
#include "kreisel_demo.h"

static KApp app;
static KCursorVisual cursor;
static const KProfile *prof;
static KThemeId theme = K_THEME_DARK;
static uint32_t *rgba;
static int cw, ch;

static int rgba_px;   /* (the pixels rgba holds: the canvas may be drawn at a scale, ui_draw_set_scale) */
static int start(const KProfile *p)
{
    int w, h;
    if (prof) ui_draw_shutdown();
    free(rgba);
    prof = p;
    k_profile_size(p, &w, &h);
    cw = w; ch = h;
    rgba_px = w * h;
    rgba = malloc((size_t)w * h * 4);
    cursor = (KCursorVisual){0};
    k_set_shown(&app, p->shown);
    k_set_layout(&app, k_profile_layout(p));
    {   /* a browser: room for any plugin built at 44.1 kHz */
        static const KDeviceCaps caps = {44100u, 1u << 20, 2000u, K_AUDIO_VOICES, K_NEED_FLOAT};
        k_plugin_set_caps(&caps);
    }
    return rgba && ui_draw_init(p, theme);
}

EMSCRIPTEN_KEEPALIVE int kw_init(int profile, int th)
{
    (void)th; theme = K_THEME_DARK;
    k_init(&app);
    prof = NULL;
    return start(k_profile_at(profile < 0 || profile >= k_profile_count() ? 0 : profile));
}
/* a touch screen of any size (app.html: a phone, a tablet): w x h units, drawn at s pixels a unit (the device's pixel ratio) */
/* app.html: the whole screen is the surface's canvas (draw_surface.c), the page drawn in its box */
static int surface_on;
static const GCanvas *shown(void) { return surface_on ? ui_surface_canvas() : ui_draw_canvas(); }
static int tw, th_, ts;   /* (the touch screen as last laid out) */
static unsigned in_kinds = K_IN_TOUCH;   /* (a desktop browser, a fine pointer: also the mouse's and the keyboard's, as the Mac app) */
static int touch_screen(int w, int h, int s)
{
    int pw, ph;
    tw = w; th_ = h; ts = s;
    ui_surface_layout(w, h, &pw, &ph);
    if (!start(k_profile_custom(pw, ph, in_kinds))) return 0;
    if (s > 1) ui_draw_set_scale(s);
    surface_on = ui_surface_init(s);
    return surface_on;
}
/* app.html, before kw_init_touch: a desktop browser (a mouse, a keyboard) or not */
EMSCRIPTEN_KEEPALIVE void kw_desktop(int on)
{
    in_kinds = on ? (K_IN_TOUCH | K_IN_MOUSE | K_IN_KEYBOARD | K_IN_PAD) : K_IN_TOUCH;
    ui_surface_set_desktop(on);
}
EMSCRIPTEN_KEEPALIVE int kw_init_touch(int w, int h, int s)
{
    theme = K_THEME_DARK;
    k_init(&app);
    prof = NULL;
    return touch_screen(w, h, s);
}
EMSCRIPTEN_KEEPALIVE int kw_resize_touch(int w, int h, int s) { return touch_screen(w, h, s); }   /* the window turned or resized */
/* a finger: id, phase 0 down 1 moved 2 up, in the screen's units (the surface hands the page's first one to ui_touch) */
EMSCRIPTEN_KEEPALIVE void kw_finger(int id, int phase, int x, int y, uint32_t now) { ui_finger(&app, id, phase, x, y, now); }
/* a finger on the screen, in units: phase 0 down, 1 moved, 2 up (ui_touch: a tap, a value slid) */
EMSCRIPTEN_KEEPALIVE void kw_touch(int phase, int x, int y, uint32_t now) { ui_touch(&app, phase, x, y, now); }
EMSCRIPTEN_KEEPALIVE void kw_wheel(int x, int y, int d, uint32_t now) { ui_wheel(&app, x, y, d, now); }
EMSCRIPTEN_KEEPALIVE int kw_canvas_w(void) { return shown()->pw; }   /* the canvas in pixels (the scale applied) */
EMSCRIPTEN_KEEPALIVE int kw_canvas_h(void) { return shown()->ph; }
EMSCRIPTEN_KEEPALIVE int kw_demo(void) { return k_demo_load(&app); }   /* COLDWIRE (a first start with nothing saved) */
EMSCRIPTEN_KEEPALIVE int kw_profile_count(void) { return k_profile_count(); }
EMSCRIPTEN_KEEPALIVE const char *kw_profile_name(int i) { return k_profile_at(i)->name; }
EMSCRIPTEN_KEEPALIVE int kw_profile_knobs(int i) { return k_profile_at(i)->knobs; }
EMSCRIPTEN_KEEPALIVE int kw_profile_input(int i) { return k_profile_at(i)->input; }   /* K_IN_* */
EMSCRIPTEN_KEEPALIVE int kw_set_profile(int i) { return start(k_profile_at(i)); }
EMSCRIPTEN_KEEPALIVE void kw_set_theme(int th) { (void)th; theme = K_THEME_DARK; ui_draw_set_theme(theme); }   /* (Dark only) */
EMSCRIPTEN_KEEPALIVE void kw_battery(int level, int charging) { k_set_battery(&app, level, charging); }   /* -1: none */
EMSCRIPTEN_KEEPALIVE int kw_w(void) { return cw; }
EMSCRIPTEN_KEEPALIVE int kw_h(void) { return ch; }

/* draws a frame at the page's clock; 1 while something moves or waits on the clock */
EMSCRIPTEN_KEEPALIVE int kw_frame(uint32_t now)
{
    int busy = ui_draw(&app, &cursor, now);
    if (surface_on) {
        if (ui_surface_relayout()) { touch_screen(tw, th_, ts); busy = ui_draw(&app, &cursor, now); }   /* (the drawer moved: the page's room) */
        ui_surface_frame(&app, now); busy |= 1;   /* (held buttons repeat on the clock: drawn every frame) */
    }
    k_tick(&app, now);
    return busy;
}
/* the last frame as RGBA bytes (ImageData order) */
EMSCRIPTEN_KEEPALIVE uint32_t *kw_rgba(void)
{
    const GCanvas *c = shown();
    int i, n = c->pw * c->ph;   /* (its pixels: units x the scale) */
    if (n > rgba_px) {   /* (a scaled canvas: more pixels than the units) */
        uint32_t *q = realloc(rgba, (size_t)n * 4);
        if (!q) return rgba;
        rgba = q; rgba_px = n;
    }
    for (i = 0; i < n; i++) {
        uint32_t p = c->px[i];   /* 0xAARRGGBB */
        rgba[i] = 0xff000000u | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);   /* little-endian bytes R G B A */
    }
    return rgba;
}

EMSCRIPTEN_KEEPALIVE void kw_action(int type, int a, int b, uint32_t now) { k_dispatch(&app, (KAction){(KActionType)type, a, b, now}); }
/* a touch or click on the screen, as the SDL build sends it */
EMSCRIPTEN_KEEPALIVE void kw_pointer(int x, int y, uint32_t now)
{
    ui_tap(&app, x, y, now);   /* (draw.c: the overlays, the header, a target) */
}

/* what the panel shows of the state */
EMSCRIPTEN_KEEPALIVE int kw_page(void) { return app.page; }
EMSCRIPTEN_KEEPALIVE int kw_page_group(void)
{
    KPage p = app.page;
    return K_IS_HOME(p) ? -1 : p == K_PAGE_STEP || p == K_PAGE_DRUM || p == K_PAGE_ROLL ? K_GROUP_PATTERN : p == K_PAGE_EDIT ? K_GROUP_EDIT : p == K_PAGE_MIXER || p == K_PAGE_SONG || p == K_PAGE_BUS1 || p == K_PAGE_BUS2 ? K_GROUP_GLO : p == K_PAGE_LFO ? K_GROUP_LFO : p == K_PAGE_ARP || p == K_PAGE_SCL ? K_GROUP_ARP : p == K_PAGE_SOUND ? K_GROUP_SOUND :
           p == K_PAGE_FX ? K_GROUP_FX : K_GROUP_ENV;
}
EMSCRIPTEN_KEEPALIVE const char *kw_page_name(void) { return k_page_name(app.page); }
EMSCRIPTEN_KEEPALIVE const char *kw_label(int i) { return ui_item_name(&app, app.page, i); }   /* what card i of the page says */
EMSCRIPTEN_KEEPALIVE int kw_playing(void) { return app.playing; }
EMSCRIPTEN_KEEPALIVE int kw_recording(void) { return app.recording; }
EMSCRIPTEN_KEEPALIVE int kw_step(int i) { return k_key_lit(&app, i); }
EMSCRIPTEN_KEEPALIVE int kw_action_id(int which)   /* the action numbers, so the page does not repeat the enum */
{
    switch (which) {
    case 0: return K_ACT_FOCUS_NEXT; case 1: return K_ACT_FOCUS_PREV; case 2: return K_ACT_UP; case 3: return K_ACT_DOWN;
    case 4: return K_ACT_LEFT; case 5: return K_ACT_RIGHT; case 6: return K_ACT_ACCEPT; case 7: return K_ACT_BACK;
    case 8: return K_ACT_PLAY; case 9: return K_ACT_TRACK_DELTA; case 10: return K_ACT_VALUE_DELTA;
    case 11: return K_ACT_PARAM_DELTA; case 12: return K_ACT_PAGE_GROUP_DOWN; case 13: return K_ACT_PAGE_GROUP_UP;
    case 14: return K_ACT_PAGE_MENU; case 15: return K_ACT_SOUND_DELTA; case 16: return K_ACT_MASTER_DELTA;
    case 17: return K_ACT_OCTAVE_DELTA; case 18: return K_ACT_STEP_TOGGLE; case 19: return K_ACT_SAVE;
    case 20: return K_ACT_REC; case 21: return K_ACT_LANE_SELECT; case 22: return K_ACT_KEY;
    case 23: return K_ACT_HOME_DOWN; case 24: return K_ACT_HOME_UP; case 25: return K_ACT_SETTINGS;
    case 26: return K_ACT_SELECT_DELTA; case 27: return K_ACT_TYPE_DELTA; case 28: return K_ACT_TOOLS;
    default: return -1;
    }
}
EMSCRIPTEN_KEEPALIVE int kw_group_id(int which)
{
    switch (which) {
    case 0: return K_GROUP_PATTERN; case 1: return K_GROUP_GLO; case 2: return K_GROUP_SOUND;
    case 3: return K_GROUP_ENV; case 4: return K_GROUP_FX; case 5: return K_GROUP_EDIT; case 6: return K_GROUP_LFO; case 7: return K_GROUP_ARP; default: return -1;
    }
}
/* the theme's colours as 0xRRGGBB, for the page's panel: bg surf text accent mid dim line raise key edge ink */
EMSCRIPTEN_KEEPALIVE uint32_t kw_color(int i)
{
    KTheme t;
    k_theme(&t, theme);
    switch (i) {
    case 0: return t.bg; case 1: return t.surf; case 2: return t.text; case 3: return t.accent; case 4: return t.mid;
    case 5: return t.dim; case 6: return t.line; case 7: return t.raise; case 8: return t.key; case 9: return t.edge;
    case 10: return t.ink; default: return 0;
    }
}
/* the feel: 0 card open ms, 1 card close ms, 2 hold ms */
EMSCRIPTEN_KEEPALIVE void kw_feel(int which, float v)
{
    KFeel *f = ui_feel();
    if (which == 0) f->open_ms = v; else if (which == 1) f->close_ms = v; else if (which == 2) f->hold_ms = (unsigned)v;
}

/* sound: the page hands the KApp's bytes and the live notes to the audio module (kreisel_audio_web.c, in the worklet) */
EMSCRIPTEN_KEEPALIVE const uint8_t *kw_app_ptr(void) { return (const uint8_t *)&app; }
EMSCRIPTEN_KEEPALIVE int kw_app_size(void) { return (int)sizeof app; }
/* the next live note packed track << 16 | note << 8 | vel (vel 0: off), or -1 */
EMSCRIPTEN_KEEPALIVE int kw_take_note(void)
{
    KNoteEv e;
    return k_take_note(&app, &e) ? (e.track << 16) | (e.note << 8) | e.vel : -1;
}
/* the sample clock's step while playing, as the SDL build does: it is the playhead the picture shows */
EMSCRIPTEN_KEEPALIVE void kw_playhead(int step)
{
    if (app.playing && step >= 0) { app.step = step; app.beat = (step / 4) & 3; }
}

/* SAVE's store: the slots in memory here, kept by the page in localStorage (app.html). The page fills the slots it kept
 * (kw_store_slot, kw_store_put), then kw_store_start sets the store and loads the song saved last. Every write or erase is
 * told to the page (Module.kreiselStored) so it keeps the bytes. */
static uint8_t sbytes[2][K_USER_PRESETS][K_SAVE_BUF];
static int slen[2][K_USER_PRESETS], slatest = -1;
EM_JS(void, kw_js_stored, (int kind, int slot, const uint8_t *p, int n, int latest), {
    if (Module.kreiselStored) Module.kreiselStored(kind, slot, n > 0 ? HEAPU8.slice(p, p + n) : null, latest);
});
static int ws_ok(int k, int s) { return k == K_STORE_SONG ? s >= 0 && s < K_SONG_SLOTS : k == K_STORE_PRESET && s >= 0 && s < K_USER_PRESETS; }
static long ws_read(void *c, int k, int s, uint8_t *b, size_t cap)
{
    (void)c;
    if (!ws_ok(k, s)) return -1;
    if ((size_t)slen[k][s] > cap) return -1;
    memcpy(b, sbytes[k][s], (size_t)slen[k][s]);
    return slen[k][s];
}
static int ws_write(void *c, int k, int s, const uint8_t *b, size_t n)
{
    (void)c;
    if (!ws_ok(k, s) || n > K_SAVE_BUF) return -1;
    memcpy(sbytes[k][s], b, n); slen[k][s] = (int)n;
    if (k == K_STORE_SONG) slatest = s;
    kw_js_stored(k, s, sbytes[k][s], (int)n, slatest);
    return 0;
}
static int ws_erase(void *c, int k, int s)
{
    (void)c;
    if (!ws_ok(k, s)) return -1;
    slen[k][s] = 0;
    if (k == K_STORE_SONG && slatest == s) slatest = -1;
    kw_js_stored(k, s, sbytes[k][s], 0, slatest);
    return 0;
}
static int ws_latest(void *c) { (void)c; return slatest; }
static const KStore WEB_STORE = {ws_read, ws_write, ws_erase, ws_latest, 0};
EMSCRIPTEN_KEEPALIVE uint8_t *kw_store_slot(int kind, int slot) { return ws_ok(kind, slot) ? sbytes[kind][slot] : NULL; }   /* K_SAVE_BUF bytes */
EMSCRIPTEN_KEEPALIVE int kw_store_cap(void) { return K_SAVE_BUF; }
EMSCRIPTEN_KEEPALIVE void kw_store_put(int kind, int slot, int n) { if (ws_ok(kind, slot)) slen[kind][slot] = n < 0 ? 0 : n > K_SAVE_BUF ? K_SAVE_BUF : n; }
EMSCRIPTEN_KEEPALIVE int kw_store_start(int latest)   /* 0: the song saved last loaded */
{
    slatest = latest >= 0 && latest < K_SONG_SLOTS ? latest : -1;
    k_store_set(&WEB_STORE);
    return k_save_boot(&app);
}

/* ---- plugins: what the running Kreisel has, and swapping a track's engine or an insert's effect ----
 * The engines are the registry's (built-ins first, then registered plugins: k_engine_count); the effects are a registry of
 * their own (type 0 NONE, 1..K_FX_BUILTINS built in, then installed ones). A slot changes only through the core's own paths,
 * so a swap here is what ALGORITHM does on the device. The audio worklet is a module of its own built with the same built-in
 * tables, so the indices in the KApp bytes mean the same there; a plugin registered here would not be registered there yet
 *. */
EMSCRIPTEN_KEEPALIVE int kw_tracks(void) { return K_TRACKS; }
EMSCRIPTEN_KEEPALIVE int kw_fx_slots(void) { return K_FX_SLOTS; }
EMSCRIPTEN_KEEPALIVE int kw_engine_count(void) { return k_engine_count(); }
EMSCRIPTEN_KEEPALIVE int kw_engine_builtin_count(void) { return K_ENGINE_COUNT; }
EMSCRIPTEN_KEEPALIVE const char *kw_engine_name(int i) { return k_engine_at(i)->name; }
EMSCRIPTEN_KEEPALIVE uint32_t kw_engine_uid(int i) { return k_engine_uid(i); }
EMSCRIPTEN_KEEPALIVE int kw_engine_kind(int i) { return i >= 0 && i < k_engine_count() ? k_plugin_at(i)->kind : -1; }   /* KPluginKind */
EMSCRIPTEN_KEEPALIVE int kw_engine_version(int i) { return i >= 0 && i < k_engine_count() ? k_plugin_at(i)->version : 0; }
EMSCRIPTEN_KEEPALIVE int kw_engine_nparams(int i) { return k_engine_at(i)->nparams; }
EMSCRIPTEN_KEEPALIVE int kw_effect_count(void) { return k_effect_count(); }   /* types 1..count (0: NONE) */
EMSCRIPTEN_KEEPALIVE int kw_effect_builtin_count(void) { return (int)K_FX_BUILTINS; }
static const KEngine *fx_desc(int type) { const KPlugin *p = k_effect_at(type); return p ? p->desc : 0; }
EMSCRIPTEN_KEEPALIVE const char *kw_effect_name(int type) { return fx_desc(type) ? fx_desc(type)->name : "NONE"; }
EMSCRIPTEN_KEEPALIVE uint32_t kw_effect_uid(int type) { return fx_desc(type) ? fx_desc(type)->uid : 0; }
EMSCRIPTEN_KEEPALIVE int kw_effect_version(int type) { return k_effect_at(type) ? k_effect_at(type)->version : 0; }
EMSCRIPTEN_KEEPALIVE int kw_effect_nparams(int type) { return fx_desc(type) ? fx_desc(type)->nparams : 0; }
EMSCRIPTEN_KEEPALIVE int kw_effect_ram(int type) { return k_effect_at(type) ? (int)k_effect_at(type)->ram : 0; }   /* bytes */
EMSCRIPTEN_KEEPALIVE int kw_effect_cost(int type) { return k_effect_at(type) ? k_effect_at(type)->cost : 0; }      /* us a block */

static int track_ok(int t) { return t >= 0 && t < K_TRACKS; }
EMSCRIPTEN_KEEPALIVE int kw_track(void) { return app.track; }
EMSCRIPTEN_KEEPALIVE int kw_track_engine(int t) { return track_ok(t) ? app.sound[t] : -1; }
EMSCRIPTEN_KEEPALIVE uint32_t kw_track_missing(int t) { return track_ok(t) ? app.missing[t].uid : 0; }   /* a saved plugin not here */
EMSCRIPTEN_KEEPALIVE const char *kw_track_preset(int t) { return track_ok(t) ? app.preset_name[t] : ""; }
EMSCRIPTEN_KEEPALIVE int kw_track_drum(int t) { return track_ok(t) ? k_is_drum(&app, t) : 0; }
/* track t onto engine e, as ALGORITHM does (k_track_engine: the engine's kept values come back); the engine it has now */
EMSCRIPTEN_KEEPALIVE int kw_set_track_engine(int t, int e)
{
    if (!track_ok(t) || e < 0 || e >= k_engine_count()) return -1;
    k_track_engine(&app, t, e);
    return app.sound[t];
}
EMSCRIPTEN_KEEPALIVE int kw_track_fx(int t, int s) { return track_ok(t) && s >= 0 && s < K_FX_SLOTS ? app.params[t][K_PAGE_FX][s * 4] : -1; }
EMSCRIPTEN_KEEPALIVE int kw_track_fx_mix(int t, int s) { return track_ok(t) && s >= 0 && s < K_FX_SLOTS ? app.params[t][K_PAGE_FX][s * 4 + 3] : 0; }
/* insert s of track t onto effect type (0: NONE), as ALGORITHM sets it on FX; the type it has now */
EMSCRIPTEN_KEEPALIVE int kw_set_track_fx(int t, int s, int type)
{
    KVal *ty;
    if (!track_ok(t) || s < 0 || s >= K_FX_SLOTS || type < 0 || type > k_effect_count()) return -1;
    ty = &app.params[t][K_PAGE_FX][s * 4];
    if (*ty != type) { *ty = type; app.preset_dirty[t] = 1; }
    return *ty;
}
/* The seam for an uploaded plugin. The browser has no loader yet (to come: a wasm side module whose kreisel_plugin() record is
 * registered here and in the worklet's module alike). Until then it says what k_plugin_register says of a kind it cannot
 * play: K_PLUG_E_KIND. */
EMSCRIPTEN_KEEPALIVE int kw_plugin_install(const uint8_t *bytes, int n) { (void)bytes; (void)n; return K_PLUG_E_KIND; }

/* ---- a desktop browser: the mouse and the keyboard, as the Mac app (platforms/sdl/input.c) ---- */
static int reset_click;   /* (the button went down with option: its drag and release do nothing) */
EMSCRIPTEN_KEEPALIVE void kw_hover(int x, int y, int down) { ui_hover(&app, x, y, down); }
/* a left button down on the page: 1 when it was taken here (the popup's row, option's reset), else the surface's finger */
EMSCRIPTEN_KEEPALIVE int kw_click(int x, int y, int alt, uint32_t ms)
{
    reset_click = 0;
    if (ui_popup_open()) { reset_click = 1; ui_popup_click(&app, x, y, ms); return 1; }
    if (alt) { reset_click = 1; ui_reset_at(&app, x, y, ms); return 1; }
    return 0;
}
EMSCRIPTEN_KEEPALIVE int kw_click_held(void) { return reset_click; }
/* a right click: closes what is open, else the menu for what is under the pointer (never the tools) */
EMSCRIPTEN_KEEPALIVE void kw_right(int x, int y, uint32_t ms)
{
    if (ui_popup_open()) ui_popup_close();
    else if (app.menu_open || app.settings_open || app.tools_open || app.save_open || app.save_ask || app.name_open)
        k_dispatch(&app, (KAction){K_ACT_BACK, 0, 0, ms});
    else ui_context(&app, x, y, ms);
}
/* the keys app.html names: a letter or a digit as itself, these for the rest */
enum { WK_UP = 1000, WK_DOWN, WK_LEFT, WK_RIGHT, WK_ENTER, WK_ESC, WK_TAB, WK_SPACE, WK_DEL, WK_PGUP, WK_PGDN, WK_PLUS, WK_MINUS };
/* the computer's keys as a keyboard, as Logic Pro's: A W S E D F T G Y H U J K O L P ; ' (C3 to the F above at octave 0); Z X the
 * octave, C V the velocity */
static const char NOTE_KEYS[] = "awsedftgyhujkolp;'";
static int note_of(int k) { const char *p = k > 0 && k < 128 ? strchr(NOTE_KEYS, k) : NULL; return p ? 48 + (int)(p - NOTE_KEYS) + app.octave * 12 : -1; }
static int held_note[128];
/* a key down (1) or up (0); mods: 1 Cmd/Ctrl, 2 Shift, 4 Alt. 1 when it was taken (the page keeps its default otherwise) */
EMSCRIPTEN_KEEPALIVE int kw_key(int k, int down, int repeat, int mods, uint32_t ms)
{
    const int cmd = mods & 1;
    int pb = k == WK_UP ? KP_UP : k == WK_DOWN ? KP_DOWN : k == WK_LEFT ? KP_LEFT : k == WK_RIGHT ? KP_RIGHT : k == WK_ENTER ? KP_A :
             k == WK_ESC ? KP_B : k == WK_TAB ? KP_SELECT : -1;
    if (!cmd && pb >= 0) { if (!repeat || pb <= KP_RIGHT) ui_pad(&app, pb, down, ms); return 1; }   /* (as a pad: a held arrow repeats) */
    if (!cmd && !(mods & 4) && k > 0 && k < 128 && note_of(k) >= 0 && !ui_popup_open()) {   /* a note: down, up (where it went down) */
        if (repeat) return 1;
        if (down) { const int n = note_of(k); if (n >= 0 && n < 128) { held_note[k] = n + 1; k_note_in(&app, n, ui_surface_velocity()); ui_surface_key_held(n, 1); } }
        else if (held_note[k]) { k_note_in(&app, held_note[k] - 1, 0); ui_surface_key_held(held_note[k] - 1, 0); held_note[k] = 0; }
        return 1;
    }
    if (!down || repeat) return 0;
    if (ui_popup_open()) { if (k == WK_ESC) ui_popup_close(); return 1; }
    if (cmd && (k == 'c' || k == 'v')) { ui_edit(&app, k == 'c' ? 0 : 1, ms); return 1; }
    if (cmd && k == 'z') { k_dispatch(&app, (KAction){K_ACT_UNDO, 0, 0, ms}); return 1; }
    if (cmd && k == 's') { k_dispatch(&app, (KAction){K_ACT_SAVE, 0, 0, ms}); return 1; }
    if (cmd) return 0;
    switch (k) {
    case WK_DEL: ui_edit(&app, 2, ms); return 1;
    case WK_SPACE: k_dispatch(&app, (KAction){K_ACT_PLAY, 0, 0, ms}); return 1;
    case '`': k_dispatch(&app, (KAction){K_ACT_TOOLS, 0, 0, ms}); return 1;   /* (T plays F#: the tools on `) */
    case 'z': k_dispatch(&app, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, ms}); return 1;
    case 'x': k_dispatch(&app, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, ms}); return 1;
    case 'c': ui_surface_velocity_delta(-10); return 1;
    case 'v': ui_surface_velocity_delta(10); return 1;
    case WK_PGUP: k_dispatch(&app, (KAction){K_ACT_TRACK_DELTA, -1, 0, ms}); return 1;
    case WK_PGDN: k_dispatch(&app, (KAction){K_ACT_TRACK_DELTA, 1, 0, ms}); return 1;
    case WK_PLUS: k_dispatch(&app, (KAction){K_ACT_VALUE_DELTA, 5, 0, ms}); return 1;
    case WK_MINUS: k_dispatch(&app, (KAction){K_ACT_VALUE_DELTA, -5, 0, ms}); return 1;
    case '[': k_dispatch(&app, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, ms}); return 1;   /* (the octave, as the Zero's [ ]) */
    case ']': k_dispatch(&app, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, ms}); return 1;
    default: return 0;
    }
}
