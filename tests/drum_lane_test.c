/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* An outside drum's values for each lane: TDRM, a synth that says it is a drum (twelve parameters: the first
 * ten each lane's, the last two the track's), built here on the SDK. The knobs turn the lane in hand's and show another lane's
 * when the lane changes; each voice sounds with its lane's values; a song and a preset keep them (and a song from before the
 * lanes gives every lane the track's); a song naming it when it is not here keeps its lanes untouched and writes them back. */
#include "kreisel.h"
#include "kreisel_audio.h"
#include "kreisel_song.h"
#include "kreisel_sdk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ TDRM --- */
static void tdrm_note_on(track_t *t, voice_t *v)
{
    v->ph[0] = 0;
    v->s[0] = t->p[P_E0];                       /* LVL as the note started: note_on sees the lane's too */
}
static void tdrm_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int32_t level = t->p[P_E0] * 258;     /* LVL, 0 .. 127 -> Q15: the lane's */
    uint32_t ph = v->ph[0], i;
    for (i = 0; i < n; i++) { out[i] += voice_amp(mulq15(sine_i(ph), level), m, i); ph += m->inc; }
    v->ph[0] = ph;
}
static const engine_t TDRM_DSP = {.name = "TDRM", .note_on = tdrm_note_on, .render = tdrm_render};
static KEngine TDRM_DESC = {
    .name = "TDRM",
    .uid = K_UID('t', 'd', 'r', 'm'),
    .page_title = {"HIT", "MORE", ""},
    .edit = {
        {"LVL", KF_PCT, 0, 127, 100, 0, 0}, {"PIT", KF_INT, -24, 24, 0, 0, "st"}, {"P2", KF_PCT, 0, 100, 50, 0, 0},
        {"P3", KF_PCT, 0, 100, 50, 0, 0}, {"P4", KF_PCT, 0, 100, 50, 0, 0}, {"P5", KF_PCT, 0, 100, 50, 0, 0},
        {"P6", KF_PCT, 0, 100, 50, 0, 0}, {"P7", KF_PCT, 0, 100, 50, 0, 0}, {"P8", KF_PCT, 0, 100, 50, 0, 0},
        {"P9", KF_PCT, 0, 100, 50, 0, 0}, {"KITA", KF_PCT, 0, 100, 50, 0, 0}, {"KITB", KF_PCT, 0, 100, 50, 0, 0},
    },
    .macro = {0, 1, 2, 10},
    .nparams = 12,
    .page2 = 4,
    .drum = 1,
    .lanes = {"BD", "SD", "CH", "OH", "CP", "LT", "HT", "RS"},
};
static const KPlugin TDRM = {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &TDRM_DESC, &TDRM_DSP, FS, 0, 4, 0};

static void act(KApp *a, KActionType t, int x, int y) { k_dispatch(a, (KAction){t, x, y, 1}); }

static double hit(const KApp *g, int lane)   /* the energy of a lane's hit on track 1, its first tenth of a second */
{
    static KAudio au;
    static float buf[2 * 4410];
    double e = 0;
    int i;
    k_audio_init(&au, 44100);
    k_audio_sync(&au, g);
    k_audio_note(&au, 0, lane, 100, 1);
    k_audio_render(&au, buf, 4410);
    for (i = 0; i < 4410; i++) e += (double)buf[2 * i] * buf[2 * i];
    return e;
}

int main(void)
{
    static KApp a, b;
    static uint8_t s1[4096], s2[4096];
    size_t n1, n2;
    int d, l, k;
    d = k_plugin_register(&TDRM);
    assert(d >= K_ENGINE_COUNT && k_plugin_lane_params(d) == K_LANE_VALS && K_LANE_VALS == 10);
    assert(k_plugin_lane_params(0) == 0 && k_plugin_lane_params(k_engine_find(K_UID('D', 'R', 'U', 'M'))) == 0);   /* (synths, DRUM: none) */

    /* the knobs: the lane in hand's; another lane shows its own; the eleventh value on is the track's */
    k_init(&a);
    a.track = 0;
    k_track_engine(&a, 0, d);
    assert(a.sound[0] == d && k_is_drum(&a, 0));
    k_set_shown(&a, 4);
    k_open_page(&a, K_PAGE_SOUND);
    for (l = 0; l < K_LANES; l++) assert(k_lane_val(&a, 0, l, 0) == 100);   /* (none turned yet: each lane the track's) */
    a.cur_lane[0] = 0;
    act(&a, K_ACT_PARAM_DELTA, 0, -80);   /* K1: LVL, lane 1 */
    assert(k_param_get(&a, K_PAGE_SOUND, 0) == 20 && a.plug_lane_uid[0] == TDRM_DESC.uid && a.plug_lane[0][0][0] == 20);
    k_select(&a, K_SEL_LANE, 1);
    if (a.cur_lane[0] != 1) a.cur_lane[0] = 1;   /* (the DRUM page takes a lane in hand; SOUND may not) */
    assert(k_param_get(&a, K_PAGE_SOUND, 0) == 100);   /* lane 2: its own, untouched */
    act(&a, K_ACT_PARAM_DELTA, 0, 20);
    act(&a, K_ACT_PARAM_DELTA, 1, 5);     /* K2: PIT (few values: a step a detent) */
    assert(k_param_get(&a, K_PAGE_SOUND, 0) == 120 && k_param_get(&a, K_PAGE_SOUND, 1) == 1);
    a.cur_lane[0] = 0;
    assert(k_param_get(&a, K_PAGE_SOUND, 0) == 20 && k_param_get(&a, K_PAGE_SOUND, 1) == 0);   /* lane 1 again: as it was left */
    assert(k_lane_val(&a, 0, 0, 0) == 20 && k_lane_val(&a, 0, 1, 0) == 120 && k_lane_val(&a, 0, 2, 0) == 100);
    k_open_page(&a, K_PAGE_EDIT);   /* EDIT: P4 .. KITB; KITA (the eleventh) is the track's, whatever the lane */
    assert(k_param(&a, K_PAGE_EDIT, 6) == &a.params[0][K_PAGE_SOUND][4 + 10]);
    assert(k_param(&a, K_PAGE_EDIT, 0) == &a.plug_lane[0][0][4]);
    k_open_page(&a, K_PAGE_SOUND);

    /* the audio: each lane's hit with its own LVL (20 against 120: ~36 x the energy) */
    {
        const double e1 = hit(&a, 0), e2 = hit(&a, 1), e3 = hit(&a, 2);
        printf("lane 1 %.4f, lane 2 %.4f, lane 3 %.4f\n", e1, e2, e3);
        assert(e1 > 0 && e2 > 20 * e1 && e2 < 50 * e1);
        assert(e3 > 0.5 * e2 && e3 < 0.9 * e2);   /* (lane 3: LVL 100 against lane 2's 120: ~0.69) */
    }

    /* a song keeps them */
    for (l = 0; l < K_LANES; l++) a.lane_on[0][l] = (uint16_t)(0x1111u << (l & 3));
    a.plug_lane[0][7][9] = 77;
    n1 = k_song_save(&a, s1, sizeof s1);
    assert(n1 > 0 && n1 <= k_song_max_size());
    k_init(&b);
    assert(k_song_load(&b, s1, n1) == K_SONG_OK && b.sound[0] == d && b.plug_lane_uid[0] == TDRM_DESC.uid);
    for (l = 0; l < K_LANES; l++) for (k = 0; k < K_LANE_VALS; k++) assert(b.plug_lane[0][l][k] == k_lane_val(&a, 0, l, k));
    assert(!memcmp(b.lane_on[0], a.lane_on[0], sizeof a.lane_on[0]) && b.params[0][K_PAGE_SOUND][4 + 10] == a.params[0][K_PAGE_SOUND][4 + 10]);
    assert(b.lane_p[0][0][0] == 0 && b.lane_p[0][1][0] == 3);   /* (DRUM's kit, not in use: not kept, the default) */
    n2 = k_song_save(&b, s2, sizeof s2);
    assert(n2 == n1 && !memcmp(s1, s2, n1));

    /* a preset too (onto another track) */
    n2 = k_preset_save(&a, 0, "LANES", s2, sizeof s2);
    assert(n2 > 0 && n2 <= k_preset_max_size());
    k_init(&b);
    assert(k_preset_load(&b, 2, s2, n2) == K_SONG_OK && b.sound[2] == d && b.plug_lane_uid[2] == TDRM_DESC.uid);
    for (l = 0; l < K_LANES; l++) for (k = 0; k < K_LANE_VALS; k++) assert(b.plug_lane[2][l][k] == k_lane_val(&a, 0, l, k));

    /* the plugin not here: kept as read (the track missing), written back the same; back again, the lanes are there */
    k_plugin_reset();
    k_init(&b);
    assert(k_song_load(&b, s1, n1) == K_SONG_OK && k_track_missing(&b, 0));
    n2 = k_song_save(&b, s2, sizeof s2);
    assert(n2 == n1 && !memcmp(s1, s2, n1));
    d = k_plugin_register(&TDRM);
    assert(d >= K_ENGINE_COUNT);
    k_init(&b);
    assert(k_song_load(&b, s2, n2) == K_SONG_OK && !k_track_missing(&b, 0) && b.plug_lane[0][1][0] == 120 && b.plug_lane[0][7][9] == 77);

    /* a song from before the lanes (TDRM saved as a synth that is not a drum: TRAK without them, DRUM with its kit): each lane the
     * track's values */
    TDRM_DESC.drum = 0;
    a.sound[0] = d;
    a.params[0][K_PAGE_SOUND][4] = 64;
    n2 = k_song_save(&a, s2, sizeof s2);
    TDRM_DESC.drum = 1;
    k_init(&b);
    assert(k_song_load(&b, s2, n2) == K_SONG_OK && b.sound[0] == d && b.plug_lane_uid[0] == 0);
    for (l = 0; l < K_LANES; l++) assert(k_lane_val(&b, 0, l, 0) == 64);
    assert(hit(&b, 0) > 0.9 * hit(&b, 3) && hit(&b, 0) < 1.1 * hit(&b, 3));
    b.track = 0; b.cur_lane[0] = 3; k_open_page(&b, K_PAGE_SOUND);
    assert(k_param_get(&b, K_PAGE_SOUND, 0) == 64);   /* (reached: taken from the track's, every lane) */
    for (l = 0; l < K_LANES; l++) assert(b.plug_lane[0][l][0] == 64);

    /* another engine and back: the lanes stay */
    k_track_engine(&a, 0, 0);
    k_track_engine(&a, 0, d);
    a.cur_lane[0] = 1;
    assert(k_param_get(&a, K_PAGE_SOUND, 0) == 120);
    puts("kreisel drum lane test: OK");
    return 0;
}
