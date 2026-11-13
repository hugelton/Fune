/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* An outside plugin as a desktop host loads it: FM2 (plugins/fm2) from build/plugins. It registers after the built-in engines,
 * a track can take it and it sounds; a song names it by its id, and a host without it keeps the track as NO PLUGIN (a sine). */
#include "kreisel.h"
#include "kreisel_audio.h"
#include "kreisel_plugin.h"
#include "kreisel_engine_api.h"
#include "kreisel_dsp.h"
#include "kreisel_engine_types.h"
#include "kreisel_plugin_load.h"
#include "kreisel_song.h"
#include "kreisel_fx.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double play(KApp *g)   /* a chord on track 1 for a fifth of a second: its energy */
{
    static KAudio a;
    static float buf[2 * 4096];
    double sum = 0;
    int n;
    size_t i;
    k_audio_init(&a, 44100);
    k_audio_sync(&a, g);
    k_audio_note(&a, 0, 60, 100, 1);
    k_audio_note(&a, 0, 67, 100, 1);
    for (n = 0; n < 2; n++) {
        k_audio_render(&a, buf, 4096);
        for (i = 0; i < 8192; i++) { assert(fabsf(buf[i]) <= 1.f); sum += (double)buf[i] * buf[i]; }
    }
    return sum;
}

int main(void)
{
    static const KDeviceCaps desk = {44100u, 1u << 20, 2000u, K_AUDIO_VOICES, K_NEED_FLOAT};
    static uint8_t song[4096];
    KApp g;
    int fm, i;
    size_t n;
    double e_fm, e_sine;
    /* the built-in engines and effects are records as a plugin is, and pass the same check; a synth's DSP is its own engine's */
    for (i = 0; i < K_ENGINE_COUNT; i++) {
        const KPlugin *b = k_plugin_at(i);
        assert(b == &K_BUILTIN_PLUGINS[i] && k_plugin_check(b, 1) == 0 && k_plugin_check(b, 0) == K_PLUG_E_UID);
        assert(b->desc == &K_ENGINE[i] && (b->kind == K_PLUG_SYNTH) == !K_ENGINE[i].drum);
        if (b->kind == K_PLUG_SYNTH) assert(!strcmp(((const engine_t *)b->dsp)->name, b->desc->name));
    }
    for (i = 1; i <= k_effect_count(); i++) assert(k_plugin_check(k_effect_at(i), 1) == 0);
    {   /* the version a song keeps is a byte: 1 .. 255 */
        KPlugin v = *k_plugin_at(0);
        v.version = 0; assert(k_plugin_check(&v, 1) == K_PLUG_E_VERSION);
        v.version = K_ENGINE_VERSION_MAX + 1; assert(k_plugin_check(&v, 1) == K_PLUG_E_VERSION);
        v.version = K_ENGINE_VERSION_MAX; assert(k_plugin_check(&v, 1) == 0);
    }
    assert(K_ENGINE_API_MAJOR == K_PLUGIN_ABI && K_ENGINE_API_VERSION == (K_PLUGIN_ABI << 8 | K_ENGINE_API_MINOR));
    k_plugin_set_caps(&desk);
    assert(k_plugin_load_dir("/nonexistent") == 0);
    assert(k_plugin_load_dir(KREISEL_PLUGIN_DIR) == 3);   /* FM2, FLAT, TILT (an effect) */
    fm = k_engine_find(K_UID('k', 'f', 'm', '2'));
    assert(fm >= K_ENGINE_COUNT && k_engine_count() == K_ENGINE_COUNT + 2 && k_plugin_at(fm)->kind == K_PLUG_SYNTH);
    {   /* FLAT lays out its own screens: a table for each page, a large screen's own; FM2 has none (the vanilla view) */
        const int fl = k_engine_find(K_UID('f', 'l', 'a', 't'));
        const KLayout *sq, *lg;
        static KEngine bad_desc;
        static KWidget bad_w[1];
        static KLayout bad_l;
        static KPluginExt bad_ext;
        KPlugin bad;
        assert(fl >= K_ENGINE_COUNT && k_plugin_at(fl)->ext);
        sq = k_plugin_layout(fl, K_LPAGE_SOUND, K_SCR_SQUARE);
        lg = k_plugin_layout(fl, K_LPAGE_SOUND, K_SCR_LARGE);
        assert(sq && lg && sq != lg && sq->page == K_LPAGE_SOUND && k_plugin_layout(fl, K_LPAGE_EDIT, K_SCR_SMALL));
        assert(!k_plugin_layout(fm, K_LPAGE_SOUND, K_SCR_SQUARE) && !k_plugin_layout(0, K_LPAGE_SOUND, K_SCR_SQUARE));
        assert(k_screen_kind(240, 240) == K_SCR_SQUARE && k_screen_kind(320, 170) == K_SCR_SMALL && k_screen_kind(1280, 800) == K_SCR_LARGE);
        k_host_set_screen(320, 170, K_IN_PAD | K_IN_KEYBOARD, 0);   /* what a plugin is told: what the device has, not its name */
        assert(k_host_api()->size == sizeof(KHostApi) && k_host_api()->screen == K_SCR_SMALL && k_host_api()->fs == 44100u &&
               (k_host_api()->has & K_NEED_FLOAT) && k_host_api()->input == (K_IN_PAD | K_IN_KEYBOARD));
        /* a layout that is not sound is refused: a cell past its grid, a parameter it has not */
        bad_desc = *k_engine_at(fl); bad_desc.uid = K_UID('b', 'a', 'd', '1');
        bad_ext = *k_plugin_at(fl)->ext; bad_ext.layouts = &bad_l; bad_ext.nlayouts = 1;
        bad_l = (KLayout){K_SCR_ANY, K_LPAGE_SOUND, 4, 4, 1, bad_w};
        bad = *k_plugin_at(fl); bad.desc = &bad_desc; bad.ext = &bad_ext;
        bad_w[0] = (KWidget){K_W_SLIDER, 0, 0, 3, 0, 2, 1, 0};
        assert(k_plugin_register(&bad) == K_PLUG_E_DESC);
        bad_w[0] = (KWidget){K_W_SLIDER, 0, 8, 0, 0, 1, 1, 0};
        assert(k_plugin_register(&bad) == K_PLUG_E_DESC);
        bad_w[0] = (KWidget){K_W_SLIDER, 0, K_WP_MIX, 0, 0, 1, 1, 0};   /* an insert's MIX: an effect's alone */
        assert(k_plugin_register(&bad) == K_PLUG_E_DESC);
        bad_w[0] = (KWidget){K_W_SLIDER, 0, 0, 0, 0, 1, 1, 0};
        bad_l.page = K_LPAGE_FX; assert(k_plugin_register(&bad) == K_PLUG_E_DESC);     /* the FX page: an effect's alone */
        bad_l.page = K_LPAGE_DRUM; assert(k_plugin_register(&bad) == K_PLUG_E_DESC);   /* a drum's lane page: a drum's alone */
        bad_l.page = K_LPAGE_COUNT; assert(k_plugin_register(&bad) == K_PLUG_E_DESC);
        {   /* a synth that is a drum may have it (taken there before its SOUND layout) */
            static const char *const LN[K_LANES] = {"BD", "SD", "CH", "OH", "CP", "LT", "HT", "RS"};
            KPlugin dr = bad;
            static KEngine dr_desc;
            int k, di;
            dr_desc = bad_desc; dr_desc.uid = K_UID('d', 'r', 'm', '1'); dr_desc.drum = 1;
            for (k = 0; k < K_LANES; k++) dr_desc.lanes[k] = LN[k];
            dr.desc = &dr_desc;
            bad_l.page = K_LPAGE_DRUM;
            di = k_plugin_register(&dr);
            assert(di >= K_ENGINE_COUNT && k_plugin_layout(di, K_LPAGE_DRUM, K_SCR_SQUARE) == &bad_l && !k_plugin_layout(di, K_LPAGE_SOUND, K_SCR_SQUARE));
            bad_l.page = K_LPAGE_SOUND;
        }
        bad_w[0] = (KWidget){K_W_COUNT + 3, 0, -1, 0, 0, 1, 1, 0};   /* a widget of a later host: taken (this one skips it) */
        assert(k_plugin_register(&bad) >= K_ENGINE_COUNT);
        {   /* TILT, an effect, lays out its insert (K_LPAGE_FX) on each kind of screen; a built-in effect has none */
            const int ty = k_effect_find(K_UID('t', 'i', 'l', 't'));
            const KPlugin *tp = ty ? k_effect_at(ty) : 0;
            static KLayout fx_l;
            static KPluginExt fx_ext;
            static KEngine fx_desc;
            KPlugin fxb;
            assert(ty > (int)K_FX_BUILTINS && tp && tp->kind == K_PLUG_EFFECT && k_plugin_check(tp, 0) == 0);
            assert(k_effect_layout(ty, K_SCR_SQUARE) && k_effect_layout(ty, K_SCR_SMALL) && k_effect_layout(ty, K_SCR_LARGE));
            assert(k_effect_layout(ty, K_SCR_SQUARE)->page == K_LPAGE_FX && !k_effect_layout(1, K_SCR_SQUARE) && !k_effect_layout(0, K_SCR_SQUARE));
            assert(!k_plugin_layout(fl, K_LPAGE_FX, K_SCR_SQUARE));
            fx_desc = *tp->desc; fx_desc.uid = K_UID('b', 'a', 'd', '2');
            fx_ext = *tp->ext; fx_ext.layouts = &fx_l; fx_ext.nlayouts = 1;
            fxb = *tp; fxb.desc = &fx_desc; fxb.ext = &fx_ext;
            fx_l = *k_effect_layout(ty, K_SCR_SQUARE);
            fx_l.page = K_LPAGE_SOUND; assert(k_plugin_check(&fxb, 0) == K_PLUG_E_DESC);   /* an effect has no SOUND, EDIT, DRUM */
            fx_l.page = K_LPAGE_DRUM; assert(k_plugin_check(&fxb, 0) == K_PLUG_E_DESC);
            fx_l.page = K_LPAGE_FX; assert(k_plugin_check(&fxb, 0) == 0);
            fx_l.w = bad_w; fx_l.n = 1; fx_l.cols = fx_l.rows = 4;
            bad_w[0] = (KWidget){K_W_KNOB, 0, K_WP_MIX, 0, 0, 1, 1, 0}; assert(k_plugin_check(&fxb, 0) == 0);
            bad_w[0] = (KWidget){K_W_ENV, 0, K_WP_MIX, 0, 0, 1, 1, 0}; assert(k_plugin_check(&fxb, 0) == K_PLUG_E_DESC);
            bad_w[0] = (KWidget){K_W_KNOB, 0, 4, 0, 0, 1, 1, 0}; assert(k_plugin_check(&fxb, 0) == K_PLUG_E_DESC);   /* (it has four) */
        }
        {   /* and it sounds */
            KApp f;
            k_init(&f);
            f.sound[0] = fl;
            for (i = 0; i < k_engine_at(fl)->nparams; i++) f.params[0][K_PAGE_SOUND][4 + i] = k_engine_at(fl)->edit[i].def;
            assert(play(&f) > 1e-3);
        }
    }
    assert(!k_engine_at(fm)->drum && k_engine_at(fm)->nparams == 11 && k_engine_at(fm)->page2 == 7);

    k_init(&g);   /* a track takes it at its defaults, and it sounds */
    g.sound[0] = fm;
    for (i = 0; i < k_engine_at(fm)->nparams; i++) g.params[0][K_PAGE_SOUND][4 + i] = k_engine_at(fm)->edit[i].def;
    e_fm = play(&g);
    assert(e_fm > 1e-3);
    n = k_song_save(&g, song, sizeof song);   /* the song names it by its id */
    assert(n > 0);

    k_plugin_reset();   /* a host without it: the track is kept, says NO PLUGIN, plays a sine */
    {
        KApp h;
        k_init(&h);
        assert(k_song_load(&h, song, n) == 0 && k_track_missing(&h, 0));
        e_sine = play(&h);
        assert(e_sine > 1e-3 && fabs(e_sine - e_fm) > 1e-4);
        assert(k_plugin_load_dir(KREISEL_PLUGIN_DIR) == 3);   /* installed again: the same song finds it */
        k_init(&h);
        assert(k_song_load(&h, song, n) == 0 && !k_track_missing(&h, 0) && h.sound[0] == k_engine_find(K_UID('k', 'f', 'm', '2')));
    }
    {   /* a song made with a newer version of an engine that is here: kept untouched (as a missing one), written back as it was */
        static uint8_t song2[4096], song3[4096];
        KPlugin v2 = *k_plugin_at(k_engine_find(K_UID('k', 'f', 'm', '2')));
        KApp h;
        size_t n2, n3;
        int e2;
        v2.version = 2;
        k_plugin_reset();
        e2 = k_plugin_register(&v2);   /* (its descriptor and DSP: the library loaded above, still open) */
        assert(e2 >= K_ENGINE_COUNT);
        k_init(&h);
        h.sound[0] = e2;
        for (i = 0; i < k_engine_at(e2)->nparams; i++) h.params[0][K_PAGE_SOUND][4 + i] = k_engine_at(e2)->edit[i].def;
        h.params[0][K_PAGE_SOUND][4 + 2] = 77;   /* (INDEX: not its default) */
        n2 = k_song_save(&h, song2, sizeof song2);
        assert(n2 > 0);
        k_plugin_reset();
        assert(k_plugin_load_dir(KREISEL_PLUGIN_DIR) == 3);   /* version 1 here */
        k_init(&h);
        assert(k_song_load(&h, song2, n2) == 0 && k_track_missing(&h, 0) && k_track_newer(&h, 0) && h.missing[0].version == 2);
        n3 = k_song_save(&h, song3, sizeof song3);
        assert(n3 > 0);
        k_init(&h);   /* a song of version 1: played by version 1 */
        assert(k_song_load(&h, song, n) == 0 && !k_track_missing(&h, 0) && !k_track_newer(&h, 0));
        k_plugin_reset();
        assert(k_plugin_register(&v2) >= K_ENGINE_COUNT);   /* the newer one back: the written-back song plays, its values as made */
        k_init(&h);
        assert(k_song_load(&h, song3, n3) == 0 && !k_track_missing(&h, 0) && h.params[0][K_PAGE_SOUND][4 + 2] == 77);
        k_plugin_reset();
        assert(k_plugin_load_dir(KREISEL_PLUGIN_DIR) == 3);
    }
    printf("plugin: FM2 loaded, energy %.4f (a sine without it %.4f)\nkreisel plugin test: OK\n", e_fm, e_sine);
    return 0;
}
