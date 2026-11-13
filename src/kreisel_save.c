/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAVE: song slots and user presets (kreisel_save.h). The flow is Felucca's SAVE pages' (ui_input.c, ui_name.c: the
 * owner's design), written anew for Kreisel's core. */
#include "kreisel_save.h"
#include "kreisel_song.h"
#include "kreisel_engine.h"
#include <stdio.h>
#include <string.h>

static const KStore *store;
static uint8_t buf[K_SAVE_BUF];
static uint8_t used[2][K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS];
static char names[2][K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS][K_NAME_LEN + 1];
static uint32_t engines[K_USER_PRESETS];
static const char CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";

static int slots(int kind) { return kind == K_STORE_SONG ? K_SONG_SLOTS : K_USER_PRESETS; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }

static long rd(int kind, int slot)
{
    if (!store || !store->read) return -1;
    return store->read(store->ctx, kind, slot, buf, sizeof buf);
}
static void scan_one(int kind, int slot)
{
    long n = rd(kind, slot);
    uint32_t eng = 0;
    used[kind][slot] = 0;
    names[kind][slot][0] = 0;
    if (kind == K_STORE_PRESET) engines[slot] = 0;
    if (n > 0 && k_file_info(buf, (size_t)n, names[kind][slot], &eng) == K_SONG_OK && buf[3] == (kind == K_STORE_SONG ? 'G' : 'P')) {
        used[kind][slot] = 1;
        if (kind == K_STORE_PRESET) engines[slot] = eng;
    } else names[kind][slot][0] = 0;
}
void k_save_scan(void)
{
    int k, i;
    for (k = 0; k < 2; k++) for (i = 0; i < slots(k); i++) scan_one(k, i);
}
int k_store_set(const KStore *s)
{
    if (k_song_max_size() > sizeof buf || k_preset_max_size() > sizeof buf) { store = 0; return -1; }
    store = s;
    k_save_scan();
    return 0;
}
int k_save_used(int kind, int slot) { return kind >= 0 && kind < 2 && slot >= 0 && slot < slots(kind) && used[kind][slot]; }
const char *k_save_name(int kind, int slot) { return k_save_used(kind, slot) ? names[kind][slot] : ""; }
uint32_t k_save_engine(int slot) { return k_save_used(K_STORE_PRESET, slot) ? engines[slot] : 0; }
const char *k_save_act_name(int act) { static const char *const N[6] = {"", "Load", "Save", "Name", "Erase", "Erase"}; return act >= 0 && act < 6 ? N[act] : ""; }
const char *k_save_charset(void) { return CHARSET; }

static int load_song(KApp *a, int slot)
{
    long n = rd(K_STORE_SONG, slot);
    int rc;
    if (n <= 0) return -1;
    if ((rc = k_song_load(a, buf, (size_t)n)) != K_SONG_OK) return rc == K_SONG_ERR_TRACKS ? -2 : -1;   /* (a refusal leaves the app as it was) */
    a->song_from = (uint8_t)(slot + 1);
    return 0;
}
int k_save_load(KApp *a, int slot)
{
    if (slot < 0 || slot >= K_SONG_SLOTS || !used[K_STORE_SONG][slot]) return -1;
    return load_song(a, slot);
}
int k_save_song(KApp *a, int slot)
{
    size_t n;
    if (slot < 0 || slot >= K_SONG_SLOTS || !store || !store->write || !(n = k_song_save(a, buf, sizeof buf))) return -1;
    if (store->write(store->ctx, K_STORE_SONG, slot, buf, n) != 0) { scan_one(K_STORE_SONG, slot); return -1; }
    scan_one(K_STORE_SONG, slot);
    a->song_from = (uint8_t)(slot + 1);
    return 0;
}
static void say(KApp *a, KBalloonKind k) { k_balloon_show(&a->balloon, k, a->clock_ms); }
static void step_say(KApp *a, int d)   /* PRESETS' balloon: the name comes in from the side it turned to */
{
    say(a, K_BALLOON_PRESET);
    a->balloon.step_ms = a->clock_ms;
    a->balloon.dir = (int8_t)(d > 0 ? 1 : -1);
}
static void preset_is(KApp *a, int slot)   /* the track in hand's sound is now user preset `slot`'s */
{
    a->preset_slot[a->track] = (int8_t)slot;
    a->factory[a->track] = -1;
    a->preset_dirty[a->track] = 0;
    snprintf(a->preset_name[a->track], sizeof a->preset_name[a->track], "%s", names[K_STORE_PRESET][slot]);
}
/* user preset `slot` was erased, overwritten (gone: 0) or renamed (1): every track that still says it plays that slot follows,
 * but `except` (the track the slot was just written from: preset_is has it) */
static void slot_follow(KApp *a, int slot, int renamed, int except)
{
    int t;
    for (t = 0; t < K_TRACKS; t++) {
        if (t == except || a->preset_slot[t] != slot) continue;
        if (renamed) { snprintf(a->preset_name[t], sizeof a->preset_name[t], "%s", names[K_STORE_PRESET][slot]); continue; }
        a->preset_slot[t] = -1;   /* (its sound stays; it is no preset's any more) */
        a->preset_name[t][0] = 0;
        a->preset_dirty[t] = 0;
    }
}
#define K_DIRTY_GAP_MS 250u   /* PRESETS over a changed preset: the second detent counts only this long after the shake began */
/* PRESETS, as Felucca's: one list for the track, over every engine: each engine's factory presets in the engine's order
 * (presets_factory.c), then the user presets in their slots, one a detent, round the end. A preset of another engine turns the
 * track to it (k_track_engine: the steps and the drum lanes are the track's, kept either way). Where it stands: the factory one it
 * came from, else the user slot, else just before its engine's first */
int k_save_preset_step(KApp *a, int d)
{
    const int t = a->track, E = k_engine_count() < K_ENGINE_COUNT ? k_engine_count() : K_ENGINE_COUNT;
    const uint8_t dirty = a->preset_dirty[t];
    int base[K_ENGINE_COUNT + 1], e, e0, F, N, pos, i;
    for (e = 0, F = 0; e < E; e++) { base[e] = F; F += k_factory_count(k_engine_uid(e)); }
    base[E] = F;
    N = F + K_USER_PRESETS;
    e = a->sound[t] < E ? a->sound[t] : 0;
    pos = a->factory[t] >= 0 ? base[e] + a->factory[t] : a->preset_slot[t] >= 0 ? F + a->preset_slot[t] : -1;
    if (!d) return -1;
    if (k_track_missing(a, t)) { say(a, K_BALLOON_MISSING); return -1; }   /* NO PLUGIN: an engine of here would let its saved data go */
    if (dirty && pos >= 0) {   /* the preset in hand changed: the first detent only says so (it shakes); one more while that is up,
                                * but not in the same flick (K_DIRTY_GAP_MS), lets the changes go and steps on */
        const int32_t held = (int32_t)(a->clock_ms - a->balloon.held_ms);
        if (!(a->balloon.kind == K_BALLOON_DIRTY && held >= 0 && held < (int32_t)K_BALLOON_HOLD_MS)) {
            k_balloon_show(&a->balloon, K_BALLOON_DIRTY, a->clock_ms);
            a->balloon.in_ms = a->clock_ms;   /* (each refusal shakes again) */
            return -1;
        }
        if (held < (int32_t)K_DIRTY_GAP_MS) return -1;   /* (too soon: the shake has not been seen; the clock is not reset) */
    }
    if (pos < 0) pos = d > 0 ? base[e] - 1 : base[e];   /* (none in hand: from its own engine's) */
    e0 = a->sound[t];
    for (i = 1; i <= N; i++) {
        const int c = wrapi(pos + (d > 0 ? i : -i), N);
        long n;
        if (c < F) {
            int pe = 0;
            while (pe + 1 < E && base[pe + 1] <= c) pe++;
            if (k_factory_count(k_engine_uid(pe)) <= c - base[pe]) continue;
            k_track_engine(a, t, pe);
            if (k_factory_apply(a, t, c - base[pe]) == 0) { step_say(a, d); return 0; }
            continue;
        }
        if (!used[K_STORE_PRESET][c - F]) continue;
        n = rd(K_STORE_PRESET, c - F);
        if (n > 0) {
            const int ue = k_engine_find(engines[c - F]);
            if (ue >= 0) k_track_engine(a, t, ue);   /* (its own engine first: what the old one had is kept for ALGORITHM) */
            if (k_preset_load(a, t, buf, (size_t)n) == K_SONG_OK) { preset_is(a, c - F); step_say(a, d); return 0; }
        }
    }
    if (a->sound[t] != e0) k_track_engine(a, t, e0);   /* (nothing loaded: back on the engine it was on) */
    a->preset_dirty[t] = dirty;   /* (nothing loaded: the '*' stays) */
    say(a, K_BALLOON_NO_PRESET);
    return -1;
}
/* a user preset picked by its slot (a desktop's list): onto the track in hand, on its own engine; 0 loaded */
int k_save_preset_load(KApp *a, int slot)
{
    const int t = a->track;
    long n;
    if (slot < 0 || slot >= K_USER_PRESETS || !used[K_STORE_PRESET][slot] || k_track_missing(a, t)) return -1;
    n = rd(K_STORE_PRESET, slot);
    if (n <= 0) return -1;
    {   const int ue = k_engine_find(engines[slot]);
        if (ue >= 0) k_track_engine(a, t, ue); }
    if (k_preset_load(a, t, buf, (size_t)n) != K_SONG_OK) return -1;
    preset_is(a, slot);
    say(a, K_BALLOON_PRESET);
    return 0;
}
/* the synth pages (and FX, the preset's inserts): SAVE there overwrites the user preset the track plays (after a question),
 * rather than opening the overlay. HOME's macro row turns the preset too, but HOME's SAVE is the song's: there it asks only
 * when the preset in hand has changed */
static int synth_page(const KApp *a)
{
    const KPage p = a->page;
    if (K_IS_HOME(p)) return a->preset_dirty[a->track] && a->preset_slot[a->track] >= 0 && used[K_STORE_PRESET][a->preset_slot[a->track]];
    return p == K_PAGE_SOUND || p == K_PAGE_EDIT || p == K_PAGE_ENV1 || p == K_PAGE_ENV2 || p == K_PAGE_ENV_DEST || p == K_PAGE_LFO || p == K_PAGE_FX;
}
int k_save_quick(KApp *a, KAction e)
{
    const int t = a->track, s = a->preset_slot[t];
    if (a->save_ask) {
        switch (e.type) {   /* what passes: releases (a hold ends as it began), the level; the transport and the keys say no and still act */
        case K_ACT_PAGE_GROUP_UP: case K_ACT_HOME_UP: case K_ACT_MASTER_DELTA: return 0;
        case K_ACT_KEY: if (e.b) a->save_ask = 0; return 0;
        case K_ACT_PLAY: a->save_ask = 0; return 0;
        default: break;
        }
        a->save_ask = 0;
        if (e.type != K_ACT_SAVE && !(e.type == K_ACT_OCTAVE_DELTA && e.a > 0)) return 1;   /* SAVE again or OCT+ (yes everywhere else): yes; any other control: no */
        if (s < 0 || !used[K_STORE_PRESET][s]) return 1;
        if (a->playing) { say(a, K_BALLOON_STOP); return 1; }
        {
            const size_t n = k_preset_save(a, t, names[K_STORE_PRESET][s], buf, sizeof buf);
            if (!n || !store || !store->write || store->write(store->ctx, K_STORE_PRESET, s, buf, n) != 0) { scan_one(K_STORE_PRESET, s); say(a, K_BALLOON_SAVE_FAIL); return 1; }
            scan_one(K_STORE_PRESET, s);
            preset_is(a, s);
            slot_follow(a, s, 0, t);
            say(a, K_BALLOON_SAVED);
        }
        return 1;
    }
    if (e.type != K_ACT_SAVE || a->save_open || a->menu_open || a->settings_open || !synth_page(a)) return 0;
    if (s < 0 || !used[K_STORE_PRESET][s]) {   /* no user preset in hand: the overlay on its SOUND (preset) side, to pick a slot */
        k_save_open(a);
        a->save_open = 2;
        return 1;
    }
    a->save_ask = 1;
    return 1;
}
int k_save_boot(KApp *a)
{
    int s = store && store->latest ? store->latest(store->ctx) : -1;
    if (s < 0 || s >= K_SONG_SLOTS || !used[K_STORE_SONG][s]) return -1;
    return load_song(a, s);
}

/* ---------------------------------------------------------------- files moved to and from a computer --- */
long k_save_get(int kind, int slot, uint8_t *out, size_t cap)
{
    if ((kind != K_STORE_SONG && kind != K_STORE_PRESET) || slot < 0 || slot >= slots(kind) || !out || !store || !store->read) return -1;
    return store->read(store->ctx, kind, slot, out, cap);
}
static int check_kind(int kind, const uint8_t *b, size_t n, size_t *len)   /* a whole file of that kind: its own length (bytes past it dropped) */
{
    const int rc = kind == K_STORE_SONG ? k_song_check(b, n) : k_preset_check(b, n);
    if (rc != K_SONG_OK) return rc;
    *len = 8u + ((size_t)b[6] | (size_t)b[7] << 8) + 2u;   /* (header, payload, CRC: checked to be within n) */
    return *len > sizeof buf ? K_SONG_ERR_TRUNC : K_SONG_OK;
}
int k_save_put(KApp *a, int kind, int slot, const uint8_t *b, size_t n)
{
    size_t len;
    int rc;
    if (!a || (kind != K_STORE_SONG && kind != K_STORE_PRESET) || slot < 0 || slot >= slots(kind) || !b) return K_SONG_ERR_ARG;
    if ((rc = check_kind(kind, b, n, &len)) != K_SONG_OK) return rc;
    if (a->playing) return K_SAVE_ERR_PLAYING;
    if (!store || !store->write) return K_SAVE_ERR_WRITE;
    rc = store->write(store->ctx, kind, slot, b, len);
    scan_one(kind, slot);
    if (rc != 0 || !used[kind][slot]) return K_SAVE_ERR_WRITE;
    if (kind == K_STORE_PRESET) slot_follow(a, slot, 0, -1);   /* (a track that played the old one keeps its sound, no longer that slot's) */
    return K_SONG_OK;
}
int k_save_open_file(KApp *a, const uint8_t *b, size_t n)
{
    char name[K_NAME_LEN + 1];
    uint32_t eng = 0;
    int rc, t;
    if (!a || !b) return K_SONG_ERR_ARG;
    if (n < 4 || b[3] != 'P') {   /* (a song, or its refusal) */
        if ((rc = k_song_load(a, b, n)) != K_SONG_OK) return rc;
        a->song_from = 0;   /* (no slot's: SAVE asks where) */
        return K_SONG_OK;
    }
    if ((rc = k_preset_check(b, n)) != K_SONG_OK) return rc;
    t = a->track;
    if (k_track_missing(a, t)) return K_SONG_ERR_ARG;
    k_file_info(b, n, name, &eng);
    {   const int ue = k_engine_find(eng);
        if (ue >= 0) k_track_engine(a, t, ue); }
    if ((rc = k_preset_load(a, t, b, n)) != K_SONG_OK) return rc;
    a->preset_slot[t] = -1;
    a->factory[t] = -1;
    a->preset_dirty[t] = 0;
    snprintf(a->preset_name[t], sizeof a->preset_name[t], "%s", name);
    say(a, K_BALLOON_PRESET);
    return K_SONG_OK;
}

/* is v in row i's list? k_setting_value names a value past the list as the list's first (the same string), and every value of a
 * row is its own string; a row with no list (an action) shows the same whatever its value, so only 0 is in it */
static int setting_in(KApp *a, int i, int v)
{
    const uint8_t keep = a->set_v[i];
    const char *first, *at;
    if (!v) return 1;
    a->set_v[i] = 0; first = k_setting_value(a, i);
    a->set_v[i] = (uint8_t)v; at = k_setting_value(a, i);
    a->set_v[i] = keep;
    return at != first;
}
/* the settings' record: "KRST", version 1, the count, a byte a row (a row the record does not have keeps its default; a value
 * past a row's list is the record's fault: it comes in as the list's last) */
int k_settings_load(KApp *a)
{
    uint8_t b[K_SETTINGS_BYTES];
    long n;
    int i, v;
    if (!store || !store->read_settings) return -1;
    n = store->read_settings(store->ctx, b, sizeof b);
    if (n < 6 || memcmp(b, "KRST", 4) || b[4] != 1u || 6 + (long)b[5] > n) return -1;
    for (i = 0; i < K_SETTINGS && i < b[5]; i++) {
        for (v = b[6 + i]; v > 0 && !setting_in(a, i, v); v--) ;
        a->set_v[i] = (uint8_t)v;
    }
    return 0;
}
int k_settings_save(const KApp *a)
{
    uint8_t b[K_SETTINGS_BYTES];
    int i;
    if (!store || !store->write_settings) return -1;
    memcpy(b, "KRST", 4); b[4] = 1u; b[5] = (uint8_t)K_SETTINGS;
    for (i = 0; i < K_SETTINGS; i++) b[6 + i] = a->set_v[i];
    return store->write_settings(store->ctx, b, (size_t)(6 + K_SETTINGS));
}
void k_save_open(KApp *a)
{
    a->group_held = -1;   /* (a page key held into SAVE: its menu must not open over the SAVE screen, nor its release be lost) */
    if (!a->save_open) {
        a->save_open = 1;
        a->save_sel[0] = (uint8_t)(a->song_from ? a->song_from - 1 : a->save_sel[0]);
    } else a->save_open = a->save_open == 1 ? 2 : 1;
    a->save_act = K_SAVE_NONE;   /* the slots first: OCT+ opens the menu on the slot in hand, Save first */
    a->name_open = 0;
    a->menu_open = 0;
}

/* the name a SAVE starts from: the slot's own, the song's, or "SONG 3" / "ANALOG 3" */
static void default_name(const KApp *a, int kind, int slot, char *out)
{
    if (used[kind][slot] && names[kind][slot][0]) { snprintf(out, K_NAME_LEN + 1, "%s", names[kind][slot]); return; }
    if (kind == K_STORE_SONG && a->song_name[0]) { snprintf(out, K_NAME_LEN + 1, "%s", a->song_name); return; }
    if (kind == K_STORE_SONG) snprintf(out, K_NAME_LEN + 1, "SONG %d", slot + 1);
    else {
        char e[K_NAME_LEN + 1];
        int i;
        snprintf(e, sizeof e, "%s", k_engine_at(a->sound[a->track])->name);
        for (i = 0; e[i]; i++) if (e[i] >= 'a' && e[i] <= 'z') e[i] = (char)(e[i] - 32);
        e[K_NAME_LEN - 3] = 0;   /* (room for " 24") */
        snprintf(out, K_NAME_LEN + 1, "%s %d", e, slot + 1);
    }
}
static void name_start(KApp *a, int what)
{
    const int kind = a->save_open - 1, slot = a->save_sel[kind];
    int n;
    default_name(a, kind, slot, a->name_buf);
    for (n = (int)strlen(a->name_buf); n < K_NAME_LEN; n++) a->name_buf[n] = ' ';   /* the whole field, padded */
    a->name_buf[K_NAME_LEN] = 0;
    for (n = K_NAME_LEN; n > 0 && a->name_buf[n - 1] == ' '; n--) ;
    a->name_pos = (uint8_t)(n < K_NAME_LEN ? n : K_NAME_LEN - 1);   /* after the name */
    a->name_open = (uint8_t)what;
}
static int write_now(KApp *a)   /* the NAME screen's OCT+: save, or rename */
{
    const int kind = a->save_open - 1, slot = a->save_sel[kind];
    char nm[K_NAME_LEN + 1], keep[K_NAME_LEN + 1];
    size_t n = 0;
    int i;
    memcpy(keep, a->song_name, sizeof keep);
    memcpy(nm, a->name_buf, sizeof nm);
    for (i = K_NAME_LEN; i > 0 && (nm[i - 1] == ' ' || !nm[i - 1]); i--) nm[i - 1] = 0;
    if (a->name_open == 1) {   /* SAVE */
        if (kind == K_STORE_SONG) {
            memcpy(a->song_name, nm, sizeof nm);   /* (the song's bytes carry its name: the old one back on any failure below) */
            n = k_song_save(a, buf, sizeof buf);
        } else n = k_preset_save(a, a->track, nm, buf, sizeof buf);
    } else {   /* NAME: the slot's own bytes, renamed */
        long r = rd(kind, slot);
        if (r > 0) n = k_file_rename(buf, (size_t)r, sizeof buf, nm);
    }
    if (!n || !store || !store->write || store->write(store->ctx, kind, slot, buf, n) != 0) {
        memcpy(a->song_name, keep, sizeof keep);
        scan_one(kind, slot);
        return -1;
    }
    scan_one(kind, slot);
    if (kind == K_STORE_PRESET && a->name_open == 1) { preset_is(a, slot); slot_follow(a, slot, 0, a->track); }   /* (saved from the track in
                                                * hand: it is that preset now; the others that played the slot's old sound are not) */
    else if (kind == K_STORE_PRESET) slot_follow(a, slot, 1, -1);   /* (renamed: every track that plays it) */
    if (kind == K_STORE_SONG && (a->name_open == 1 || a->song_from == slot + 1)) {
        memcpy(a->song_name, nm, sizeof nm);
        a->song_from = (uint8_t)(slot + 1);
    }
    return 0;
}
static void run(KApp *a)   /* the armed action, OCT+ */
{
    const int kind = a->save_open - 1, slot = a->save_sel[kind], act = a->save_act;
    a->save_act = K_SAVE_NONE;
    if (act == K_SAVE_LOAD) {
        if (!used[kind][slot]) return;
        if (kind == K_STORE_SONG) {
            const int rc = load_song(a, slot);
            if (rc == 0) say(a, K_BALLOON_LOADED);   /* (a fresh app: the overlay is closed, HOME) */
            else say(a, rc == -2 ? K_BALLOON_TRACKS : K_BALLOON_SAVE_FAIL);   /* (a song of more tracks than here) */
        } else {
            long n = rd(kind, slot);
            const int ue = k_engine_find(engines[slot]), e0 = a->sound[a->track];
            if (n > 0 && ue >= 0) k_track_engine(a, a->track, ue);   /* (its own engine first, as PRESETS does: the SEQ page follows, and
                                                                     * what the old engine had is kept for ALGORITHM) */
            if (n > 0 && k_preset_load(a, a->track, buf, (size_t)n) == K_SONG_OK) { preset_is(a, slot); a->save_open = 0; say(a, K_BALLOON_PRESET); }
            else { if (a->sound[a->track] != e0) k_track_engine(a, a->track, e0); say(a, K_BALLOON_SAVE_FAIL); }   /* (not read: its engine back) */
        }
        return;
    }
    if (a->playing) { say(a, K_BALLOON_STOP); return; }
    if (act == K_SAVE_SAVE) {   /* saved at once, under the slot's name, the song's, or "SONG 3" (no NAME screen on the way) */
        name_start(a, 1);
        if (write_now(a) == 0) { a->name_open = 0; a->save_open = 0; say(a, K_BALLOON_SAVED); }
        else { a->name_open = 0; a->save_act = K_SAVE_SAVE; say(a, K_BALLOON_SAVE_FAIL); }   /* (failed: the menu stays, Save on it) */
    }
    else if (act == K_SAVE_NAME) { if (used[kind][slot]) name_start(a, 2); }
    else if (act == K_SAVE_ERASE && used[kind][slot]) a->save_act = K_SAVE_SURE;   /* lost for good: asked once more */
    else if (act == K_SAVE_SURE && used[kind][slot]) {
        if (!store || !store->erase || store->erase(store->ctx, kind, slot) != 0) { say(a, K_BALLOON_SAVE_FAIL); scan_one(kind, slot); return; }
        scan_one(kind, slot);
        if (kind == K_STORE_SONG && a->song_from == slot + 1) a->song_from = 0;
        if (kind == K_STORE_PRESET) slot_follow(a, slot, 0, -1);
        say(a, K_BALLOON_ERASED);
    }
}
static void name_turn(KApp *a, int d)
{
    const int n = (int)sizeof CHARSET - 1, p = a->name_pos;
    const char *c = strchr(CHARSET, a->name_buf[p] ? a->name_buf[p] : ' ');
    int i = c ? (int)(c - CHARSET) : 0;
    a->name_buf[p] = CHARSET[wrapi(i + d, n)];
}
/* the SAVE menu's rows, in order: Load, Save, Name, Erase (Load first); on an empty slot Save alone */
static const int8_t MENU[4] = {K_SAVE_LOAD, K_SAVE_SAVE, K_SAVE_NAME, K_SAVE_ERASE};
int k_save_menu_row(int full, int i) { return !full ? (i == 0 ? K_SAVE_SAVE : K_SAVE_NONE) : i >= 0 && i < 4 ? MENU[i] : K_SAVE_NONE; }
static void menu_move(KApp *a, int d)
{
    const int kind = a->save_open - 1, cur = a->save_act == K_SAVE_SURE ? K_SAVE_ERASE : a->save_act;
    int i;
    if (!used[kind][a->save_sel[kind]]) { a->save_act = K_SAVE_SAVE; return; }
    for (i = 0; i < 4 && MENU[i] != cur; i++) ;
    a->save_act = MENU[wrapi((i < 4 ? i : 0) + d, 4)];
}
int k_save_dispatch(KApp *a, KAction e)
{
    const int kind = a->save_open - 1;
    if (!a->save_open) return 0;
    if (e.type == K_ACT_PLAY || e.type == K_ACT_KEY || e.type == K_ACT_MASTER_DELTA || e.type == K_ACT_TRACK_DELTA || e.type == K_ACT_SOUND_DELTA)
        return 0;   /* the transport, the keys, the level and the track still answer */
    if (a->name_open) {
        switch (e.type) {
        case K_ACT_TYPE_DELTA: if (e.a) name_turn(a, e.a > 0 ? 1 : -1); break;   /* ALGORITHM: the letter, as K2 */
        case K_ACT_PARAM_DELTA:
            if (e.a == 0) a->name_pos = (uint8_t)clampi(a->name_pos + (e.b > 0 ? 1 : -1), 0, K_NAME_LEN - 1);
            else if (e.a == 1) name_turn(a, e.b > 0 ? 1 : -1);
            break;
        case K_ACT_FOCUS_NEXT: case K_ACT_VALUE_DELTA: case K_ACT_DOWN: case K_ACT_FOCUS_PREV: case K_ACT_UP:
            name_turn(a, e.type == K_ACT_VALUE_DELTA ? (e.a > 0 ? 1 : -1) : e.type == K_ACT_FOCUS_NEXT || e.type == K_ACT_DOWN ? 1 : -1);
            break;
        case K_ACT_LEFT: case K_ACT_RIGHT:
            a->name_pos = (uint8_t)clampi(a->name_pos + (e.type == K_ACT_RIGHT ? 1 : -1), 0, K_NAME_LEN - 1);
            break;
        case K_ACT_OCTAVE_DELTA: case K_ACT_ACCEPT: case K_ACT_SAVE:
            if (e.type == K_ACT_OCTAVE_DELTA && e.a < 0) { a->name_open = 0; break; }
            {
                const int was = a->name_open;
                say(a, write_now(a) == 0 ? (was == 1 ? K_BALLOON_SAVED : K_BALLOON_NAMED) : K_BALLOON_SAVE_FAIL);
                a->name_open = 0;   /* (back to the slots) */
            }
            break;
        case K_ACT_BACK: a->name_open = 0; break;
        case K_ACT_POINTER: if (e.a == -1) a->name_pos = (uint8_t)clampi(e.b, 0, K_NAME_LEN - 1); break;   /* a letter touched: the cursor there */
        default: break;
        }
        return 1;
    }
    switch (e.type) {   /* the slots, then (OCT+) a menu of what to do with the one in hand: Save, Load, Name, Erase (no knob
                         * picks a one-shot action). save_act: K_SAVE_NONE the slots, else the menu's row (a pointer's cards light it) */
    case K_ACT_SAVE: k_save_open(a); break;
    case K_ACT_TYPE_DELTA: if (!e.a) break;
        /* fall through */
    case K_ACT_FOCUS_NEXT: case K_ACT_DOWN: case K_ACT_FOCUS_PREV: case K_ACT_UP: case K_ACT_VALUE_DELTA: {
        const int d = e.type == K_ACT_TYPE_DELTA || e.type == K_ACT_VALUE_DELTA ? (e.a > 0 ? 1 : -1) : e.type == K_ACT_FOCUS_NEXT || e.type == K_ACT_DOWN ? 1 : -1;
        if (a->save_act) menu_move(a, d);   /* the menu: its row */
        else a->save_sel[kind] = (uint8_t)wrapi(a->save_sel[kind] + d, slots(kind));   /* the slots */
        break;
    }
    case K_ACT_LEFT: case K_ACT_RIGHT: a->save_open = a->save_open == 1 ? 2 : 1; a->save_act = K_SAVE_NONE; break;
    case K_ACT_POINTER:
        if (e.a == -2 && e.b >= K_SAVE_LOAD && e.b <= K_SAVE_ERASE) {   /* a card tapped (a pointer): armed by the first tap, done by the second */
            if ((a->save_act == K_SAVE_SURE ? K_SAVE_ERASE : a->save_act) == e.b) run(a);
            else if (e.b == K_SAVE_SAVE || used[kind][a->save_sel[kind]]) a->save_act = (int8_t)e.b;
        } else if (e.b >= 0 && e.b < slots(kind)) { a->save_sel[kind] = (uint8_t)e.b; a->save_act = K_SAVE_NONE; }
        break;
    case K_ACT_OCTAVE_DELTA: case K_ACT_ACCEPT:
        if (e.type == K_ACT_OCTAVE_DELTA && e.a < 0) { if (a->save_act) a->save_act = K_SAVE_NONE; else a->save_open = 0; }   /* OCT-: the menu shut, then out */
        else if (a->save_act) run(a);
        else a->save_act = used[kind][a->save_sel[kind]] ? K_SAVE_LOAD : K_SAVE_SAVE;   /* OCT+ on a slot: the menu, Load first (an empty slot: Save) */
        break;
    case K_ACT_BACK: if (a->save_act) a->save_act = K_SAVE_NONE; else a->save_open = 0; break;
    default: break;   /* (the knobs: nothing here) */
    }
    return 1;
}
