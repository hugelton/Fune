/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAVE: the song slots and user presets through the overlay's own inputs (kreisel_save.c), on a store in memory. */
#include "kreisel.h"
#include "kreisel_engine.h"
#include "kreisel_save.h"
#include "kreisel_song.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t mem[2][K_USER_PRESETS][K_SAVE_BUF];
static long len[2][K_USER_PRESETS];
static int last = -1, writes, fail_writes;
static long m_read(void *c, int k, int s, uint8_t *b, size_t cap) { (void)c; if (len[k][s] > (long)cap) return -1; memcpy(b, mem[k][s], (size_t)len[k][s]); return len[k][s]; }
static int m_write(void *c, int k, int s, const uint8_t *b, size_t n)
{
    (void)c;
    if (fail_writes) return -1;
    memcpy(mem[k][s], b, n); len[k][s] = (long)n; writes++;
    if (k == K_STORE_SONG) last = s;
    return 0;
}
static int m_erase(void *c, int k, int s) { (void)c; len[k][s] = 0; return 0; }
static int m_latest(void *c) { (void)c; return last; }
static uint8_t setmem[K_SETTINGS_BYTES]; static long setlen; static int set_writes;
static long m_rset(void *c, uint8_t *b, size_t cap) { (void)c; if (setlen > (long)cap) return -1; memcpy(b, setmem, (size_t)setlen); return setlen; }
static int m_wset(void *c, const uint8_t *b, size_t n) { (void)c; memcpy(setmem, b, n); setlen = (long)n; set_writes++; return 0; }
static const KStore STORE = {m_read, m_write, m_erase, m_latest, 0, m_rset, m_wset};

static uint32_t now = 1000;
static void act(KApp *a, KActionType t, int x, int y) { k_dispatch(a, (KAction){t, x, y, now += 10}); }
static void arm_and_do(KApp *a, int act_k)   /* OCT+ opens the menu (Load first; an empty slot: Save), SELECT its row, OCT+ does it (ERASE asks once more) */
{
    int i;
    if (!a->save_act) act(a, K_ACT_OCTAVE_DELTA, 1, 0);
    for (i = 0; i < 4 && a->save_act != act_k; i++) act(a, K_ACT_FOCUS_NEXT, 0, 0);
    assert(a->save_act == act_k); act(a, K_ACT_OCTAVE_DELTA, 1, 0);
    if (act_k == K_SAVE_ERASE) { assert(a->save_act == K_SAVE_SURE); act(a, K_ACT_OCTAVE_DELTA, 1, 0); }
}

/* files moved to and from a computer (k_save_get / k_save_put / k_save_open_file): the bytes as a slot keeps them go out and come
 * back the same; a damaged one is refused exactly as the loader would refuse it, with nothing written */
static KApp xa, xb, xs, xs0;
static uint8_t xf[K_SAVE_BUF + 16], xg[K_SAVE_BUF + 16];
static uint16_t xcrc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFF; size_t i; int k;
    for (i = 0; i < n; i++) { c ^= (uint16_t)((unsigned)p[i] << 8); for (k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (c << 1) ^ 0x1021u : c << 1); }
    return c;
}
static void transfer_test(void)
{
    long n, m;
    size_t i, pn;
    int w, rc, bit;
    k_init(&xa);
    xa.bpm = 141; k_roll_fill(&xa, 1, 0, 0x5A5A, 64); snprintf(xa.song_name, sizeof xa.song_name, "%s", "FIELD");
    assert(k_save_song(&xa, 2) == 0);
    n = k_save_get(K_STORE_SONG, 2, xf, sizeof xf);
    assert(n > 0 && n == len[K_STORE_SONG][2] && !memcmp(xf, mem[K_STORE_SONG][2], (size_t)n));   /* pull: the bytes as kept */
    assert(k_save_get(K_STORE_SONG, K_SONG_SLOTS, xg, sizeof xg) < 0 && k_save_get(2, 0, xg, sizeof xg) < 0);
    /* push into another slot (with junk after it: dropped), then that slot loads as the song it was, and saves the same bytes */
    k_init(&xb);
    memcpy(xg, xf, (size_t)n); memset(xg + n, 0xA5, 9);
    w = writes; assert(k_save_put(&xb, K_STORE_SONG, 4, xg, (size_t)n + 9) == 0 && writes == w + 1);
    assert(len[K_STORE_SONG][4] == n && !memcmp(mem[K_STORE_SONG][4], xf, (size_t)n));
    assert(k_save_used(K_STORE_SONG, 4) && !strcmp(k_save_name(K_STORE_SONG, 4), "FIELD"));
    assert(k_save_load(&xb, 4) == 0 && xb.bpm == 141 && xb.song_from == 5);
    m = (long)k_song_save(&xb, xg, sizeof xg); assert(m == n && !memcmp(xg, xf, (size_t)n));   /* round trip */
    /* single-bit flips and every truncation: k_song_check says what a scratch app's k_song_load says; put refuses, writes nothing */
    for (i = 0; i < (size_t)n; i++) for (bit = 0; bit < 8; bit += 3) {
        memcpy(xg, xf, (size_t)n); xg[i] ^= (uint8_t)(1u << bit);
        xs = xa; xs0 = xs;
        rc = k_song_load(&xs, xg, (size_t)n);
        assert(k_song_check(xg, (size_t)n) == rc);
        if (rc != K_SONG_OK) {
            assert(!memcmp(&xs, &xs0, sizeof xs));
            w = writes; assert(k_save_put(&xb, K_STORE_SONG, 3, xg, (size_t)n) == rc && writes == w);
        }
    }
    for (i = 0; i < (size_t)n; i++) { w = writes; assert(k_song_check(xf, i) != K_SONG_OK && k_save_put(&xb, K_STORE_SONG, 3, xf, i) != 0 && writes == w); }
    assert(!k_save_used(K_STORE_SONG, 3));
    /* a song is no preset (and back), a slot out of range, and none while playing */
    assert(k_save_put(&xb, K_STORE_PRESET, 0, xf, (size_t)n) == K_SONG_ERR_MAGIC && k_save_put(&xb, K_STORE_SONG, K_SONG_SLOTS, xf, (size_t)n) == K_SONG_ERR_ARG);
    xb.playing = 1; w = writes; assert(k_save_put(&xb, K_STORE_SONG, 3, xf, (size_t)n) == K_SAVE_ERR_PLAYING && writes == w); xb.playing = 0;
    /* a preset: out of a track, into a slot; a track that played that slot no longer says it does */
    k_track_engine(&xa, 2, 1 % k_engine_count());
    pn = k_preset_save(&xa, 2, "WIRE", xf, sizeof xf); assert(pn > 0);
    assert(k_save_put(&xb, K_STORE_SONG, 3, xf, pn) == K_SONG_ERR_MAGIC);
    xb.preset_slot[1] = 5;
    assert(k_save_put(&xb, K_STORE_PRESET, 5, xf, pn) == 0 && xb.preset_slot[1] == -1);
    assert(k_save_used(K_STORE_PRESET, 5) && !strcmp(k_save_name(K_STORE_PRESET, 5), "WIRE") && k_save_engine(5) == k_engine_uid(xa.sound[2]));
    assert(k_save_get(K_STORE_PRESET, 5, xg, sizeof xg) == (long)pn && !memcmp(xg, xf, pn));
    for (i = 0; i < pn; i++) {
        memcpy(xg, xf, pn); xg[i] ^= 0x10;
        xs = xa; rc = k_preset_load(&xs, 0, xg, pn);
        assert(k_preset_check(xg, pn) == rc);
        if (rc != K_SONG_OK) { w = writes; assert(k_save_put(&xb, K_STORE_PRESET, 6, xg, pn) == rc && writes == w); }
    }
    {   /* a preset with no engine (TRAK) in it: a whole, valid file, refused all the same */
        static const uint8_t nm[] = {'N', 'A', 'M', 'E', 2, 0, 'N', 'O'};
        uint16_t c;
        memcpy(xg, "KRSP", 4); xg[4] = K_SONG_FORMAT; xg[5] = 0; xg[6] = sizeof nm; xg[7] = 0;
        memcpy(xg + 8, nm, sizeof nm); c = xcrc16(xg, 8 + sizeof nm); xg[8 + sizeof nm] = (uint8_t)c; xg[9 + sizeof nm] = (uint8_t)(c >> 8);
        xs = xa; assert(k_preset_load(&xs, 0, xg, 10 + sizeof nm) == K_SONG_ERR_TRUNC && k_preset_check(xg, 10 + sizeof nm) == K_SONG_ERR_TRUNC);
        assert(k_save_put(&xb, K_STORE_PRESET, 6, xg, 10 + sizeof nm) == K_SONG_ERR_TRUNC && !k_save_used(K_STORE_PRESET, 6));
    }
    /* a desktop opening a file: a song replaces the song (no slot's), a preset goes onto the track in hand, garbage changes nothing */
    k_init(&xb); xb.track = K_TRACKS - 1;
    assert(k_save_open_file(&xb, xf, pn) == 0 && xb.sound[K_TRACKS - 1] == xa.sound[2] && !strcmp(xb.preset_name[K_TRACKS - 1], "WIRE") && xb.preset_slot[K_TRACKS - 1] == -1);
    n = k_save_get(K_STORE_SONG, 2, xf, sizeof xf);
    assert(k_save_open_file(&xb, xf, (size_t)n) == 0 && xb.bpm == 141 && xb.song_from == 0 && !strcmp(xb.song_name, "FIELD"));
    xf[20] ^= 1; xs0 = xb;
    assert(k_save_open_file(&xb, xf, (size_t)n) == K_SONG_ERR_CRC && !memcmp(&xb, &xs0, sizeof xb));
    assert(k_save_open_file(&xb, (const uint8_t *)"hello, world", 12) != 0 && !memcmp(&xb, &xs0, sizeof xb));
}

int main(void)
{
    KApp a, b;
    int i;
    assert(k_song_max_size() <= K_SAVE_BUF && k_preset_max_size() <= K_SAVE_BUF);
    printf("song max %zu, preset max %zu bytes\n", k_song_max_size(), k_preset_max_size());
    assert(k_store_set(&STORE) == 0);
    k_init(&a);
    for (i = 0; i < K_SONG_SLOTS; i++) assert(!k_save_used(K_STORE_SONG, i));

    /* SAVE opens SONG, Save chosen; OCT+ writes at once under SONG 2 (no NAME screen) and closes */
    a.bpm = 133; k_roll_fill(&a, 0, 0, 0x1234, 60);
    act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open == 1 && a.save_sel[0] == 0);
    act(&a, K_ACT_FOCUS_NEXT, 0, 0); assert(a.save_sel[0] == 1);
    arm_and_do(&a, K_SAVE_SAVE); assert(!a.name_open && !a.save_open);
    assert(k_save_used(K_STORE_SONG, 1) && !strcmp(k_save_name(K_STORE_SONG, 1), "SONG 2") && a.song_from == 2 && !strcmp(a.song_name, "SONG 2"));
    assert(a.balloon.kind == K_BALLOON_SAVED);

    /* changed, then LOAD brings it back (a fresh app: the overlay closed) */
    a.bpm = 90; k_roll_fill(&a, 0, 0, 0, 60);
    act(&a, K_ACT_SAVE, 0, 0); assert(a.save_sel[0] == 1);   /* (it opens on the song's own slot) */
    arm_and_do(&a, K_SAVE_LOAD);
    assert(a.bpm == 133 && a.steps[0][0] == 0x1234 && !a.save_open && a.song_from == 2 && !strcmp(a.song_name, "SONG 2"));

    /* NAME: the slot renamed (K2 turns the letter at the cursor) */
    act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open == 1 && a.save_sel[0] == 1);
    arm_and_do(&a, K_SAVE_NAME); assert(a.name_open == 2);
    a.name_pos = 0; act(&a, K_ACT_PARAM_DELTA, 1, 1);   /* S -> T */
    act(&a, K_ACT_OCTAVE_DELTA, 1, 0);
    assert(!strcmp(k_save_name(K_STORE_SONG, 1), "TONG 2") && !strcmp(a.song_name, "TONG 2"));
    k_init(&b); assert(k_song_load(&b, mem[0][1], (size_t)len[0][1]) == 0 && b.bpm == 133 && !strcmp(b.song_name, "TONG 2"));

    /* OCT- lets an armed action go; playing: no save */
    act(&a, K_ACT_PARAM_DELTA, 3, 1); assert(a.save_act == K_SAVE_NONE);   /* (a knob: nothing here) */
    act(&a, K_ACT_OCTAVE_DELTA, 1, 0); assert(a.save_act == K_SAVE_LOAD);   /* OCT+: the menu, Load first */
    act(&a, K_ACT_FOCUS_PREV, 0, 0); assert(a.save_act == K_SAVE_ERASE);   /* (round: Load Save Name Erase) */
    act(&a, K_ACT_OCTAVE_DELTA, -1, 0); assert(a.save_act == K_SAVE_NONE && a.save_open && k_save_used(K_STORE_SONG, 1));   /* OCT-: the menu shut */
    act(&a, K_ACT_OCTAVE_DELTA, -1, 0); assert(!a.save_open); act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open && a.save_act == K_SAVE_NONE);   /* again: out */
    a.playing = 1; i = writes;
    arm_and_do(&a, K_SAVE_SAVE); assert(!a.name_open && writes == i && a.balloon.kind == K_BALLOON_STOP);
    a.playing = 0;

    /* a failing store: not saved, said so */
    fail_writes = 1; act(&a, K_ACT_FOCUS_NEXT, 0, 0);
    arm_and_do(&a, K_SAVE_SAVE);
    assert(a.balloon.kind == K_BALLOON_SAVE_FAIL && !k_save_used(K_STORE_SONG, 2) && a.save_open && a.save_act == K_SAVE_SAVE);   /* (the menu stays, on Save) */   /* (failed: still open, to try again) */
    fail_writes = 0;

    /* power-on: the slot written last */
    k_init(&b); assert(k_save_boot(&b) == 0 && b.bpm == 133 && b.song_from == 2);

    /* SAVE again: SOUND. A preset of track 1's sound, loaded onto track 2; its steps and colour stay its own */
    act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open == 2);
    a.track = 0; a.sound[0] = 2; a.params[0][K_PAGE_SOUND][4] = 3; a.params[0][K_PAGE_ENV1][0] = 33; a.params[0][K_PAGE_LFO][0] = 77;
    arm_and_do(&a, K_SAVE_SAVE); assert(!a.name_open && !a.save_open);
    assert(k_save_used(K_STORE_PRESET, 0) && k_save_engine(0) == k_engine_at(2)->uid);
    act(&a, K_ACT_SAVE, 0, 0); act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open == 2);
    printf("preset name \"%s\" (%ld bytes)\n", k_save_name(K_STORE_PRESET, 0), len[1][0]);
    a.track = 1; a.tcolor[1] = 5; k_roll_fill(&a, 1, 0, 0x0F0F, 60); a.sound[1] = 0;
    arm_and_do(&a, K_SAVE_LOAD);
    assert(a.sound[1] == 2 && a.params[1][K_PAGE_SOUND][4] == 3 && a.params[1][K_PAGE_ENV1][0] == 33 && a.params[1][K_PAGE_LFO][0] == 77);
    assert(a.tcolor[1] == 5 && a.steps[1][0] == 0x0F0F && !a.save_open);

    /* ERASE */
    act(&a, K_ACT_SAVE, 0, 0); act(&a, K_ACT_SAVE, 0, 0); assert(a.save_open == 2);
    act(&a, K_ACT_OCTAVE_DELTA, 1, 0); act(&a, K_ACT_FOCUS_PREV, 0, 0); act(&a, K_ACT_OCTAVE_DELTA, 1, 0); assert(a.save_act == K_SAVE_SURE && k_save_used(K_STORE_PRESET, 0));   /* asked again */
    act(&a, K_ACT_OCTAVE_DELTA, -1, 0); assert(a.save_act == K_SAVE_NONE && k_save_used(K_STORE_PRESET, 0) && a.save_open);   /* OCT-: kept */
    arm_and_do(&a, K_SAVE_ERASE); assert(!k_save_used(K_STORE_PRESET, 0) && a.balloon.kind == K_BALLOON_ERASED);
    act(&a, K_ACT_BACK, 0, 0); assert(!a.save_open);

    /* the drum track's kit goes with its preset */
    {
        const int d = K_ENGINE_COUNT - 1;   /* DRUM */
        uint8_t buf[K_SAVE_BUF];
        size_t n;
        k_init(&b); b.sound[0] = d; b.lane_p[0][3][0] = 9; b.lane_p[0][3][2] = 101;
        n = k_preset_save(&b, 0, "KIT 1", buf, sizeof buf); assert(n > 0);
        printf("drum preset %zu bytes\n", n);
        k_init(&b); assert(k_preset_load(&b, 2, buf, n) == 0 && b.sound[2] == d && b.lane_p[2][3][0] == 9 && b.lane_p[2][3][2] == 101);
        {   /* renamed to nothing, then to a long name (cut to K_NAME_LEN): still the same sound */
            char nm[K_NAME_LEN + 1];
            uint32_t e;
            n = k_file_rename(buf, n, sizeof buf, ""); assert(n > 0 && k_file_info(buf, n, nm, &e) == 0 && !nm[0] && e == k_engine_at(d)->uid);
            n = k_file_rename(buf, n, sizeof buf, "a very long name"); assert(n > 0 && k_file_info(buf, n, nm, &e) == 0 && !strcmp(nm, "A VERY LON"));
            k_init(&b); assert(k_preset_load(&b, 1, buf, n) == 0 && b.lane_p[1][3][2] == 101);
        }
    }
    {   /* PRESETS: the engine's factory presets, then its user presets, round the end; never another engine's (a drum track's lanes
         * and sound stay apart); a changed preset holds the first detent (it shakes), a second discards and steps */
        KApp c;
        uint8_t pb[K_SAVE_BUF];
        size_t n;
        int i;
        for (i = 0; i < K_USER_PRESETS; i++) len[1][i] = 0;
        k_init(&c); c.sound[0] = 0; n = k_preset_save(&c, 0, "ANA A", pb, sizeof pb); memcpy(mem[1][4], pb, n); len[1][4] = (long)n;
        c.sound[0] = 1; n = k_preset_save(&c, 0, "LOFI B", pb, sizeof pb); memcpy(mem[1][9], pb, n); len[1][9] = (long)n;
        k_save_scan();
        k_init(&c); c.track = 0;
        k_open_page(&c, K_PAGE_STEP);
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.factory[0] == -1 && c.preset_slot[0] == -1);   /* SEQ: PRESETS does nothing (no preset's name there) */
        { KApp m; k_init(&m); m.track = 0; assert(m.page == K_PAGE_MIXER);   /* the mixer: it turns the track in hand's (its strips name them) */
          act(&m, K_ACT_SOUND_DELTA, 1, 0); assert(m.factory[0] != -1 || m.preset_slot[0] != -1); }
        k_open_page(&c, K_PAGE_SOUND);   /* (the synth: its name shown, PRESETS turns it) */
        const int F_A = k_factory_count(k_engine_uid(0));
        {
            const int F = F_A;
            assert(F == 12);   /* (ANALOG: 12 factory presets) */
            c.params[0][K_PAGE_FX][0] = 12;   /* an insert left by the sound before */
            act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.factory[0] == 0 && c.preset_slot[0] == -1 && c.sound[0] == 0 && !strcmp(c.preset_name[0], "Fat Bass"));
            assert(c.params[0][K_PAGE_FX][0] == 0);   /* a factory preset: no insert */
            assert(c.balloon.kind == K_BALLOON_PRESET && c.balloon.dir == 1);
            assert(c.params[0][K_PAGE_SOUND][4 + 6] == 1 && c.params[0][K_PAGE_SOUND][4 + 0] == -12 && c.params[0][K_PAGE_ENV1][1] == 35);   /* WAVE2 SQR, PITCH, D */
            for (i = 1; i < F; i++) act(&c, K_ACT_SOUND_DELTA, 1, 0);
            assert(c.factory[0] == F - 1);
        }
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.factory[0] == 0 && c.sound[0] == 1);   /* on into the next engine's (PHASE): the track turns */
        k_roll_fill(&c, 0, 0, 0x0F0F, 60);
        {   int tot = 0; for (i = 0; i < K_ENGINE_COUNT; i++) tot += k_factory_count(k_engine_uid(i));
            for (i = 1; i < tot - F_A; i++) act(&c, K_ACT_SOUND_DELTA, 1, 0); }
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 4 && c.factory[0] == -1 && c.sound[0] == 0 && !strcmp(c.preset_name[0], "ANA A"));
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 9 && c.sound[0] == 1);   /* any engine's ("LOFI B" was saved on engine 1, PHASE) */
        assert(c.steps[0][0] == 0x0F0F);   /* the steps stay the track's */
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.factory[0] == 0 && c.sound[0] == 0);   /* round the end: ANALOG's first */
        act(&c, K_ACT_SOUND_DELTA, -1, 0); act(&c, K_ACT_SOUND_DELTA, -1, 0); assert(c.preset_slot[0] == 4 && c.balloon.dir == -1);
        /* changed: the first detent refused, the second lets go */
        k_open_page(&c, K_PAGE_SOUND); c.win = 0;
        act(&c, K_ACT_PARAM_DELTA, 0, 3); assert(c.preset_dirty[0]);
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 4 && c.balloon.kind == K_BALLOON_DIRTY && c.preset_dirty[0]);
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 4 && c.preset_dirty[0] && c.balloon.kind == K_BALLOON_DIRTY);   /* the same flick: not yet */
        now += 300;
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 9 && !c.preset_dirty[0] && c.balloon.kind == K_BALLOON_PRESET);
        act(&c, K_ACT_PARAM_DELTA, 0, 3); assert(c.preset_dirty[0]);
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.balloon.kind == K_BALLOON_DIRTY);
        now += K_BALLOON_HOLD_MS + 100;   /* gone: asks again */
        act(&c, K_ACT_SOUND_DELTA, 1, 0); assert(c.preset_slot[0] == 9 && c.balloon.kind == K_BALLOON_DIRTY);
        act(&c, K_ACT_TYPE_DELTA, 1, 0); assert(c.preset_slot[0] == -1 && c.factory[0] == -1 && !c.preset_name[0][0] && !c.preset_dirty[0]);
        for (i = 0; i < K_USER_PRESETS; i++) len[1][i] = 0;
        k_track_engine(&c, 0, K_ENGINE_COUNT - 1);   /* DRUM, no user presets: its first kit */
        k_save_scan(); act(&c, K_ACT_SOUND_DELTA, 1, 0);
        assert(c.sound[0] == K_ENGINE_COUNT - 1 && c.factory[0] == 0 && !strcmp(c.preset_name[0], "Classic"));
        assert(c.lane_p[0][5][0] == 6 && c.lane_p[0][5][1] == 40 && c.lane_p[0][7][0] == 10);   /* LT: T8 TOM tuned down; RS: T8 RS */
    }
    {   /* SAVE on a synth page over a user preset: a question; any other control says no, SAVE again overwrites it (its name kept) */
        KApp c, d;
        uint8_t pb[K_SAVE_BUF];
        size_t n;
        k_init(&c); c.sound[0] = 1; n = k_preset_save(&c, 0, "LOFI A", pb, sizeof pb); memcpy(mem[1][4], pb, n); len[1][4] = (long)n;
        k_save_scan();
        k_init(&c); c.track = 0; k_open_page(&c, K_PAGE_SOUND); for (int j = 0; j < 200 && c.preset_slot[0] != 4; j++) act(&c, K_ACT_SOUND_DELTA, 1, 0);   /* on to it */
        assert(c.preset_slot[0] == 4);
        k_open_page(&c, K_PAGE_SOUND);
        c.params[0][K_PAGE_SOUND][5] = 77;
        act(&c, K_ACT_SAVE, 0, 0); assert(c.save_ask && !c.save_open);
        act(&c, K_ACT_FOCUS_NEXT, 0, 0); assert(!c.save_ask && !c.save_open);   /* no */
        k_init(&d); assert(k_preset_load(&d, 0, mem[1][4], (size_t)len[1][4]) == 0 && d.params[0][K_PAGE_SOUND][5] != 77);
        act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0); assert(!c.save_ask && !c.save_open && c.balloon.kind == K_BALLOON_SAVED);
        k_init(&d); assert(k_preset_load(&d, 0, mem[1][4], (size_t)len[1][4]) == 0 && d.params[0][K_PAGE_SOUND][5] == 77);
        assert(!strcmp(k_save_name(K_STORE_PRESET, 4), "LOFI A"));
        c.balloon.kind = 0; act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_OCTAVE_DELTA, -1, 0); assert(!c.save_ask && c.balloon.kind != K_BALLOON_SAVED);   /* OCT-: no */
        act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_OCTAVE_DELTA, 1, 0); assert(!c.save_ask && c.balloon.kind == K_BALLOON_SAVED);   /* OCT+: yes, as everywhere */
        k_open_page(&c, K_PAGE_HOME); act(&c, K_ACT_SAVE, 0, 0); assert(!c.save_ask && c.save_open == 1);   /* elsewhere: the overlay */
        act(&c, K_ACT_BACK, 0, 0);
        c.preset_slot[0] = -1; k_open_page(&c, K_PAGE_SOUND); act(&c, K_ACT_SAVE, 0, 0); assert(!c.save_ask && c.save_open == 2);   /* no preset in hand: the overlay, its SOUND side */
    }
    {   /* the settings: kept when they close (not while playing), back at power-on; a song load keeps them */
        KApp c, d;
        k_init(&c); assert(k_settings_load(&c) != 0 && !c.set_v[K_SET_LEDS]);   /* none kept yet: the defaults */
        c.settings_open = 1; c.settings_sel = K_SET_LEDS; c.playing = 1;
        act(&c, K_ACT_PARAM_DELTA, 0, 2); assert(c.set_v[K_SET_LEDS] == 1 && c.set_pending);
        act(&c, K_ACT_BACK, 0, 0); k_tick(&c, now); assert(!set_writes && c.set_pending);   /* playing: later */
        c.playing = 0; k_tick(&c, now + 1); assert(set_writes == 1 && !c.set_pending);
        k_init(&d); assert(k_settings_load(&d) == 0 && d.set_v[K_SET_LEDS] == 1);
        {   uint8_t sb[K_SAVE_BUF]; size_t sn; KApp e; k_init(&e); sn = k_song_save(&e, sb, sizeof sb);
            assert(k_song_load(&d, sb, sn) == 0 && d.set_v[K_SET_LEDS] == 1); }
        k_init(&d); d.set_v[K_SET_HELP] = 1; k_open_page(&d, K_PAGE_SOUND); assert(d.balloon.kind != K_BALLOON_HINT);   /* HELP OFF: no hint */
    }
    {   /* the fixes of 2026-11-07 */
        KApp c;
        uint8_t pb[K_SAVE_BUF];
        size_t n;
        char was[K_NAME_LEN + 1];
        for (i = 0; i < K_USER_PRESETS; i++) len[1][i] = 0;
        k_init(&c); c.sound[0] = 2; n = k_preset_save(&c, 0, "TRIO A", pb, sizeof pb); memcpy(mem[1][4], pb, n); len[1][4] = (long)n;
        k_save_scan();
        /* NO PLUGIN: PRESETS refuses and keeps what the plugin had */
        k_init(&c); k_open_page(&c, K_PAGE_SOUND); c.missing[0].uid = K_UID('f', 'u', 'z', 'z'); c.params[0][K_PAGE_SOUND][4] = 77;
        act(&c, K_ACT_SOUND_DELTA, 1, 0);
        assert(k_track_missing(&c, 0) && c.params[0][K_PAGE_SOUND][4] == 77 && c.preset_slot[0] == -1 && c.factory[0] == -1 && c.balloon.kind == K_BALLOON_MISSING);
        /* the overlay's LOAD turns the track's engine first: the old engine's values kept for ALGORITHM */
        k_init(&c); c.track = 1; c.sound[1] = 0; c.params[1][K_PAGE_SOUND][4] = 7;
        act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0); assert(c.save_open == 2);
        act(&c, K_ACT_TYPE_DELTA, 1, 0); assert(c.save_sel[1] == 1);   /* ALGORITHM picks the slot */
        act(&c, K_ACT_TYPE_DELTA, -1, 0); act(&c, K_ACT_TYPE_DELTA, -1, 0); assert(c.save_sel[1] == K_USER_PRESETS - 1);
        c.save_sel[1] = 4;
        arm_and_do(&c, K_SAVE_LOAD);
        assert(c.sound[1] == 2 && c.preset_slot[1] == 4 && (c.eng_mem_set[1] & 1u) && c.eng_mem[1][0][0] == 7);
        /* two tracks on one slot: a rename names both, an erase lets both go */
        c.track = 0; act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0); c.save_sel[1] = 4;
        arm_and_do(&c, K_SAVE_LOAD); assert(c.preset_slot[0] == 4 && c.preset_slot[1] == 4);
        act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0); assert(c.save_open == 2 && c.save_sel[1] == 4);
        arm_and_do(&c, K_SAVE_NAME); assert(c.name_open == 2);
        c.name_pos = 0; act(&c, K_ACT_TYPE_DELTA, 1, 0); assert(c.name_buf[0] == 'U');   /* ALGORITHM turns the letter: T -> U */
        act(&c, K_ACT_OCTAVE_DELTA, 1, 0);
        assert(!strcmp(k_save_name(K_STORE_PRESET, 4), "URIO A") && !strcmp(c.preset_name[0], "URIO A") && !strcmp(c.preset_name[1], "URIO A"));
        arm_and_do(&c, K_SAVE_ERASE); assert(!k_save_used(K_STORE_PRESET, 4) && c.preset_slot[0] == -1 && c.preset_slot[1] == -1 && !c.preset_name[1][0]);
        /* an overwrite from track 0 lets track 1 go (it plays the old sound) */
        n = k_preset_save(&c, 0, "TRIO A", pb, sizeof pb); memcpy(mem[1][4], pb, n); len[1][4] = (long)n; k_save_scan();
        c.save_sel[1] = 4; c.track = 1; arm_and_do(&c, K_SAVE_LOAD); c.track = 0; act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0); arm_and_do(&c, K_SAVE_LOAD);
        assert(c.preset_slot[0] == 4 && c.preset_slot[1] == 4 && !c.save_open);
        k_open_page(&c, K_PAGE_SOUND); act(&c, K_ACT_SAVE, 0, 0); act(&c, K_ACT_SAVE, 0, 0);
        assert(c.balloon.kind == K_BALLOON_SAVED && c.preset_slot[0] == 4 && c.preset_slot[1] == -1);
        /* the question: PLAY and a key say no and still act */
        act(&c, K_ACT_SAVE, 0, 0); assert(c.save_ask);
        act(&c, K_ACT_PLAY, 0, 0); assert(!c.save_ask && c.playing);
        act(&c, K_ACT_PLAY, 0, 0); assert(!c.playing);
        act(&c, K_ACT_SAVE, 0, 0); assert(c.save_ask);
        act(&c, K_ACT_KEY, 0, 1); assert(!c.save_ask); act(&c, K_ACT_KEY, 0, 0);
        /* FX asks as the synth pages; HOME only over a changed preset (else SAVE there is the song's) */
        k_open_page(&c, K_PAGE_FX); act(&c, K_ACT_SAVE, 0, 0); assert(c.save_ask && !c.save_open); act(&c, K_ACT_BACK, 0, 0); assert(!c.save_ask);
        k_open_page(&c, K_PAGE_HOME); c.preset_dirty[0] = 0; act(&c, K_ACT_SAVE, 0, 0); assert(!c.save_ask && c.save_open == 1); act(&c, K_ACT_BACK, 0, 0);
        c.preset_dirty[0] = 1; act(&c, K_ACT_SAVE, 0, 0); assert(c.save_ask && !c.save_open); act(&c, K_ACT_SAVE, 0, 0); assert(c.balloon.kind == K_BALLOON_SAVED && !c.preset_dirty[0]);
        /* a song SAVE that fails keeps the song's name */
        snprintf(c.song_name, sizeof c.song_name, "%s", "OLD"); memcpy(was, c.song_name, sizeof was);
        act(&c, K_ACT_SAVE, 0, 0); assert(c.save_open == 1); c.save_sel[0] = 4;
        fail_writes = 1; arm_and_do(&c, K_SAVE_SAVE); fail_writes = 0; assert(!c.name_open);
        assert(c.balloon.kind == K_BALLOON_SAVE_FAIL && !strcmp(c.song_name, was));
        act(&c, K_ACT_BACK, 0, 0);
        /* a settings record past its rows' lists: each value comes in as its list's last */
        memcpy(setmem, "KRST", 4); setmem[4] = 1; setmem[5] = K_SETTINGS; for (i = 0; i < K_SETTINGS; i++) setmem[6 + i] = 200; setlen = 6 + K_SETTINGS;
        k_init(&c); assert(k_settings_load(&c) == 0);
        assert(c.set_v[K_SET_LEDS] == 2 && c.set_v[K_SET_ANIM] == 1 && c.set_v[K_SET_SCREEN_OFF] == 4 && c.set_v[K_SET_HELP] == 1 && c.set_v[K_SET_RESTORE] == 1);
        assert(!c.set_v[K_SET_CLEAR] && !c.set_v[K_SET_ABOUT]);
        setmem[6 + K_SET_LEDS] = 1; k_init(&c); assert(k_settings_load(&c) == 0 && c.set_v[K_SET_LEDS] == 1);   /* (in range: as it was) */
    }
    transfer_test();
    printf("save_test ok\n");
    return 0;
}
