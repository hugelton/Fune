/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The UI library draws every profile without SDL, and the dp sizes and tiers come out as designed. */
#include "draw.h"
#include "kreisel_engine.h"
#include "kreisel_plugin.h"
#include "kreisel_level.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(int got, int want) { assert(got >= want - 2 && got <= want + 2); }
int main(void)
{
    int i, w, h;
    {
        /* Canonical effect identities exist independently of installed DSPs.
         * A legacy CHO slot is really FLNG and must not become chorus-pink. */
        const uint32_t expected[K_FX_COLOR_COUNT] = {
            0xE5484D, 0x4285E8, 0x9299A3, 0xE9C344, 0xF08A42,
            0xA176DB, 0xE774AF, 0x39AE74, 0xAC815E, 0x303238,
            0x37BDCF, 0xA3CF4E, 0xE7E9EB, 0x6171C8, 0x40BAA6
        };
        for (int k = 0; k < K_FX_COLOR_COUNT; k++)
            assert(k_fx_color((KFxColorId)k) == expected[k]);
        assert(k_fx_color((KFxColorId)-1) == 0);
        assert(k_fx_color(K_FX_COLOR_COUNT) == 0);
        assert(k_fx_color_for_uid(K_UID('F','L','N','G')) == k_fx_color(K_FX_COLOR_FLANGER));
        assert(k_fx_color_for_uid(K_UID('D','R','V','E')) == k_fx_color(K_FX_COLOR_DRIVE));
        assert(k_fx_color_for_uid(K_UID('T','R','M','L')) == k_fx_color(K_FX_COLOR_TREMOLO));
        assert(k_fx_color_for_uid(K_UID('P','I','T','C')) == 0); /* pitch shift != harmonizer */
        assert(k_fx_color_for_uid(0) == 0);
    }
    for (i = 0; i < k_profile_count(); i++) {   /* every profile draws a frame at its own size */
        const KProfile *p = k_profile_at(i);
        KApp a;
        KCursorVisual c = {0};
        const GCanvas *cv;
        k_profile_size(p, &w, &h);
        k_init(&a);
        k_set_shown(&a, p->shown);
        assert(ui_draw_init(p, K_THEME_DARK));
        ui_draw(&a, &c, 0);
        cv = ui_draw_canvas();
        assert(cv->w == w && cv->h == h && cv->px[0] != 0);
        ui_draw_shutdown();
    }
    {   /* every label fits inside the smallest card: 240 wide, four cards */
        int fit = ((240 - 2 * 3) - 3 * 4) / 4 - 4, v, pg, k;   /* a label may use the card but for 2 px each side */
        for (v = 0; v < 2; v++) {
            g_font_vector(v);
            for (pg = 0; pg < K_PAGE_COUNT; pg++)
                for (k = 0; k < k_param_count((KPage)pg); k++) {
                    int tw = g_text_w(GF_S, ui_param_name((KPage)pg, k)) + (ui_param_track((KPage)pg, k) || ui_param_icon((KPage)pg, k) >= 0 ? 15 : 0);   /* (a track number in its cushion, or an icon) */
                    if (tw > fit) printf("label too wide: page %d %s %d > %d\n", pg, ui_param_name((KPage)pg, k), tw, fit);
                    assert(tw <= fit);
                }
        }
        g_font_vector(1);
    }
    {   /* the row of cards is as wide as the picture above it, on every profile */
        for (i = 0; i < k_profile_count(); i++) {
            const KProfile *p = k_profile_at(i);
            KApp b; KCursorVisual c = {0}; const GCanvas *cv; int y, x, first = -1, last = -1, cf = -1, cl = -1;
            k_profile_size(p, &w, &h);
            k_init(&b); k_set_shown(&b, p->shown);
            assert(ui_draw_init(p, K_THEME_DARK));
            ui_draw(&b, &c, 0); cv = ui_draw_canvas();
            for (y = 30; y < h - 3 - 24 - 3; y++)   /* every row of the picture: its leftmost and rightmost pixel */
                for (x = 0; x < w; x++)
                    if (cv->px[(size_t)y * w + x] != cv->px[(size_t)y * w]) { if (first < 0 || x < first) first = x; if (x > last) last = x; }
            for (x = 0; x < w; x++) {   /* the gauges of the cards at rest */
                y = h - 3 - 24 + 19;
                if (cv->px[(size_t)y * w + x] != cv->px[(size_t)y * w]) { if (cf < 0) cf = x; cl = x; }
            }
            if (first != cf || last != cl) printf("%s: picture %d..%d, cards %d..%d\n", p->name, first, last, cf, cl);
            assert(first >= cf - 1 && first <= cf && last >= cl && last <= cl + 1);   /* a rounded corner may bleed a pixel */
            ui_draw_shutdown();
        }
    }
    {   /* a popup keeps the screen drawing until it has faded, and one frame past it (else its last frame stays up) */
        KApp b; KCursorVisual c = {0};
        k_init(&b); k_balloon_show(&b.balloon, K_BALLOON_PAGE, 0);
        assert(ui_draw_init(k_profile_find("fm1"), K_THEME_DARK));
        assert(ui_draw(&b, &c, 0) && ui_draw(&b, &c, 1000) && ui_draw(&b, &c, 1700));
        assert(ui_draw(&b, &c, 1790));
        assert(!ui_draw(&b, &c, 1900));
        ui_draw_shutdown();
    }
    {   /* a line always ends: every slope between two points reaches the second (a loop that never ended hung the mixer) */
        GCanvas c; int x, y;
        assert(g_init(&c, 64, 64));
        for (y = 0; y < 64; y += 7) for (x = 0; x < 64; x += 5) { g_line(&c, 32, 32, x, y, 2, 0xFFFFFF); g_line(&c, x, y, 32, 32, 2, 0xFFFFFF); }
        g_free(&c);
    }
    {   /* every engine's labels fit its column on SYNTH (a quarter of 240, no icon); its macros, with their icons, a card (HOME) */
        KApp b; int e, i, fit = ((240 - 2 * 3) - 3 * 4) / 4 - 4, col = (240 - 2 * 3) / 4 - 2;
        k_init(&b);
        for (e = 0; e < K_ENGINE_COUNT; e++) {
            b.sound[0] = e;
            for (i = 0; i < 4; i++) {
                int tw = g_text_w(GF_S, ui_item_name(&b, K_PAGE_HOME, i)) + (ui_item_icon(&b, K_PAGE_HOME, i) >= 0 ? 15 : 0);
                if (tw > fit) printf("macro label too wide: %s %s %d\n", K_ENGINE[e].name, ui_item_name(&b, K_PAGE_HOME, i), tw);
                assert(tw <= fit);
            }
            for (i = 0; i < 1 + K_ENGINE[e].nparams; i++) {
                int tw = g_text_w(GF_S, ui_item_name(&b, K_PAGE_SOUND, i)), room = col;
                if (tw > room) printf("engine label too wide: %s %s %d\n", K_ENGINE[e].name, ui_item_name(&b, K_PAGE_SOUND, i), tw);
                assert(tw <= room);
            }
        }
    }
    {   /* a song saved with another engine in the slot (a bad engine index, parameters out of their range): every page still draws */
        KApp b;KCursorVisual c={0};int pg,k;
        assert(ui_draw_init(k_profile_find("fm1"),K_THEME_DARK));
        k_init(&b);b.sound[0]=9999;
        for(k=0;k<K_MAX_PARAMS;k++){b.params[0][K_PAGE_SOUND][k]=32767;b.params[0][K_PAGE_FX][k]=-32768;}
        for(pg=0;pg<K_PAGE_COUNT;pg++){k_open_page(&b,(KPage)pg);ui_draw(&b,&c,0);}
        b.sound[0]=-3;for(pg=0;pg<K_PAGE_COUNT;pg++){k_open_page(&b,(KPage)pg);ui_draw(&b,&c,0);}
        ui_draw_shutdown();
    }
    {   /* LFO, ARP, SCL and CLOCK: their labels fit a card with their icons */
        KApp b; int pg, i, fit = ((240 - 2 * 3) - 3 * 4) / 4 - 4;
        k_init(&b);
        for (pg = 0; pg < K_PAGE_COUNT; pg++) {
            if (!k_page_desc((KPage)pg)) continue;
            for (i = 0; i < 4; i++) {
                int tw = g_text_w(GF_S, ui_item_name(&b, (KPage)pg, i)) + (ui_item_icon(&b, (KPage)pg, i) >= 0 ? 15 : 0);
                if (tw > fit) printf("page label too wide: %d %s %d\n", pg, ui_item_name(&b, (KPage)pg, i), tw);
                assert(tw <= fit);
            }
        }
    }
    {   /* the inserts' labels, for every effect, fit a card with their icons */
        KApp b; int t, slot, f, fit = ((240 - 2 * 3) - 3 * 4) / 4 - 4;
        k_init(&b);
        for (t = 0; t < K_FX_TYPES; t++) for (slot = 0; slot < 2; slot++) for (f = 0; f < 4; f++) {
            int i = slot * 4 + f, tw;
            b.params[0][K_PAGE_FX][slot * 4] = t;
            tw = g_text_w(GF_S, ui_item_name(&b, K_PAGE_FX, i)) + (ui_item_icon(&b, K_PAGE_FX, i) >= 0 ? 15 : 0);
            if (tw > fit) fprintf(stderr, "insert label too wide: %s %d (max %d)\n", ui_item_name(&b, K_PAGE_FX, i), tw, fit);
            assert(tw <= fit);
        }
    }
    k_profile_size(k_profile_find("fm1"), &w, &h);       assert(w == 240 && h == 240);        /* a fixture is in dp already */
    {   /* a real screen (its pixels and diagonal): its size in dp (no target has one now; the rule kept for one that will) */
        static const KProfile MPC = {"mpc", 0, 0, K_IN_KNOBS | K_IN_KEYS, 3, 3, 320, 240, 2.4f}, PSP = {"psp", 0, 0, K_IN_PAD, 0, 4, 480, 272, 4.3f};
        static const KProfile DECK = {"deck", 0, 0, K_IN_PAD | K_IN_TOUCH, 0, 4, 1280, 800, 7.0f}, ALLY = {"ally", 0, 0, K_IN_PAD | K_IN_TOUCH, 0, 4, 1920, 1080, 7.0f};
        k_profile_size(&MPC, &w, &h);       near(w, 307); near(h, 230);
        k_profile_size(&PSP, &w, &h);       near(w, 598); near(h, 339);
        k_profile_size(&DECK, &w, &h);      near(w, 950); near(h, 594);
        k_profile_size(&ALLY, &w, &h);      near(w, 976); near(h, 549);
        assert(k_profile_tier(&MPC) == K_TIER_S && k_profile_tier(&PSP) == K_TIER_M && k_profile_tier(&ALLY) == K_TIER_L);
        assert(!k_profile_cursor(&MPC));   /* knobs and keys: no cursor */
        assert(k_profile_scale(&ALLY) > 1.9f && k_profile_scale(&PSP) < 0.9f);
    }
    k_profile_size(k_profile_find("switch"), &w, &h);    assert(w == 640 && h == 360);        /* (1280 x 720 at 2x) */
    k_profile_size(k_profile_find("h700"), &w, &h);      assert(w == 320 && h == 240);        /* (at 2 x on its 640 x 480) */
    assert(k_profile_tier(k_profile_find("fm1")) == K_TIER_S && k_profile_tier(k_profile_find("zero")) == K_TIER_S);
    assert(k_profile_tier(k_profile_find("switch")) == K_TIER_M);
    assert(!(k_profile_find("fm1")->input & (K_IN_PAD | K_IN_TOUCH | K_IN_KEYBOARD)));   /* the FM-1: knobs and keys only */
    assert((k_profile_find("fm1")->input & K_IN_KNOBS) && (k_profile_find("handheld")->input & (K_IN_PAD | K_IN_TOUCH)));
    assert(!k_profile_cursor(k_profile_find("fm1")));   /* knobs and keys: no cursor */
    assert(k_profile_cursor(k_profile_find("handheld")) && k_profile_cursor(k_profile_find("zero")) && k_profile_cursor(k_profile_find("cores3")));
    assert(k_profile_scale(k_profile_find("fm1")) == 1.0f);
    /* the layout: the FM-1's own flags; another screen of the same size and inputs has the generic ones */
    {
        const unsigned f = k_profile_layout(k_profile_find("fm1")), c = k_profile_layout(k_profile_custom(240, 240, K_IN_KNOBS | K_IN_KEYS));
        assert((f & K_LAY_LEDS) && (f & K_LAY_PRESET_POP) && !(f & K_LAY_CONNECT));
        assert(!(c & K_LAY_LEDS) && !(c & K_LAY_PRESET_POP) && (c & K_LAY_CONNECT) && (c & K_LAY_FX_ALGO));
        k_profile_size(k_profile_custom(800, 480, K_IN_TOUCH), &w, &h); assert(w == 800 && h == 480);
        assert(k_profile_layout(k_profile_custom(800, 480, K_IN_TOUCH)) & K_LAY_SYNTH_ALL);
        k_profile_size(k_profile_custom(10, 10, K_IN_PAD), &w, &h); assert(w == 160 && h == 120);   /* (a floor: not smaller than that) */
    }
    /* PHASE: one envelope page, three rows (DCO, DCW, DCA: A D S R each), drawn as three graphs on both screens */
    {
        KApp a; KCursorVisual cur = {0};
        const KProfile *fm1 = k_profile_find("fm1");
        const KProfile *wide = k_profile_find("handheld");
        int phase = k_engine_find(K_UID('K','P','H','S'));
        assert(phase >= 0 && fm1 && wide);
        k_init(&a); a.sound[0] = phase;
        assert(!strcmp(ui_item_name(&a, K_PAGE_ENV1, 0), "ATK") && !strcmp(ui_item_name(&a, K_PAGE_ENV1, 7), "REL"));
        assert(!strcmp(ui_item_name(&a, K_PAGE_ENV1, 10), "SUS") && !strcmp(ui_item_name(&a, K_PAGE_ENV1, 12), ""));
        assert(ui_draw_init(fm1, K_THEME_DARK));
        k_open_page(&a, K_PAGE_ENV1); ui_draw(&a, &cur, 0);
        k_open_page(&a, K_PAGE_ENV2); assert(a.page == K_PAGE_ENV1 && a.win == 8); ui_draw(&a, &cur, 500);
        ui_draw_shutdown();
        assert(ui_draw_init(wide, K_THEME_DARK));
        k_set_layout(&a, k_profile_layout(wide));
        k_open_page(&a, K_PAGE_ENV1);
        assert(k_page_count(&a, K_PAGE_ENV1) == 12 && !strcmp(ui_item_name(&a, K_PAGE_ENV1, 8), "ATK"));
        ui_draw(&a, &cur, 900);
        ui_draw_shutdown();
    }
#if K_WITH_VECTOR
    /* VECTOR uses the host-rendered XY pad on 240x240 and 320x180. Changing
     * XY must move its graphic, without changing the regular knob focus model. */
    {
        const char *const screens[] = {"fm1", "handheld"};
        for (unsigned k = 0; k < 2; k++) {
            const KProfile *p = k_profile_find(screens[k]);
            KApp a; KCursorVisual cursor = {0};
            const GCanvas *cv;
            unsigned long long before = 0, after = 0;
            int id = k_engine_find(K_UID('K','V','E','C'));
            assert(p && id >= 0 && ui_draw_init(p, K_THEME_DARK));
            k_init(&a);
            a.sound[0] = id;
            k_open_page(&a, K_PAGE_SOUND);
            k_set_shown(&a, p->shown);
            assert(a.focus_count > 0);
            a.params[0][K_PAGE_SOUND][4] = 0;
            a.params[0][K_PAGE_SOUND][5] = 0;
            ui_draw(&a, &cursor, 0);
            cv = ui_draw_canvas();
            for (int j = 0; j < cv->w * cv->h; j++) before += (unsigned long long)cv->px[j];
            a.params[0][K_PAGE_SOUND][4] = 127;
            a.params[0][K_PAGE_SOUND][5] = 127;
            ui_draw(&a, &cursor, 1000);
            for (int j = 0; j < cv->w * cv->h; j++) after += (unsigned long long)cv->px[j];
            assert(before != after);
            ui_draw_shutdown();
        }
    }
    /* Visual review regressions: all four VECTOR corner assignments have to
     * affect the visible host panel, on SOUND *and* EDIT at both sizes.
     * The same screen must keep valid focus bounds for its four knobs. */
    {
        const char *const profile[] = {"fm1", "handheld"};
        for (unsigned v = 0; v < 2; v++) {
            KApp a; KCursorVisual cur = {0};
            const KProfile *p = k_profile_find(profile[v]);
            const GCanvas *canvas;
            int id = k_engine_find(K_UID('K','V','E','C'));
            int pages[2] = {K_PAGE_SOUND, K_PAGE_EDIT};
            assert(p && id >= 0);
            assert(ui_draw_init(p, K_THEME_DARK));
            k_init(&a); a.sound[0] = id; k_set_shown(&a, p->shown);
            for (unsigned pg = 0; pg < 2; pg++) {
                unsigned long long sum[2] = {0, 0};
                k_open_page(&a, (KPage)pages[pg]);
                a.balloon.kind = 0; /* compare the visual only, not a toast */
                for (unsigned j = 0; j < 2; j++) {
                    /* The wave choice changes text on the XY preview while
                     * the position itself stays fixed (unlike the older test). */
                    a.params[0][K_PAGE_SOUND][4+6] = j ? 7 : 0;
                    ui_draw(&a, &cur, 100);
                    canvas = ui_draw_canvas();
                    for (int n = 0; n < canvas->w * canvas->h; n++)
                        sum[j] += (unsigned long long)canvas->px[n];
                    for (int k = 0; k < a.focus_count; k++) {
                        if (a.focus[k].kind != K_NODE_PARAM) continue;
                        KRect r = a.focus[k].bounds;
                        assert(r.w > 0 && r.h > 0);
                        assert(r.x >= 0 && r.y >= 0);
                        assert(r.x + r.w <= canvas->w && r.y + r.h <= canvas->h);
                    }
                }
                assert(sum[0] != sum[1]);
            }
            ui_draw_shutdown();
        }
    }
#endif
    /* An active FX pair and AMP's extra parameter row must render without
     * invalid rectangles or leaking an old FX row's parameter identities. */
    {
        const char *const profile[] = {"fm1", "handheld"};
        for (unsigned v = 0; v < 2; v++) {
            KApp a; KCursorVisual cur = {0};
            const KProfile *p = k_profile_find(profile[v]);
            assert(p && ui_draw_init(p, K_THEME_DARK));
            k_init(&a); k_set_shown(&a, p->shown); k_open_page(&a, K_PAGE_FX);
            a.params[0][K_PAGE_FX][0] = 1; /* DIST */
            a.params[0][K_PAGE_FX][4] = 2; /* CHO */
            ui_draw(&a, &cur, 100);
            assert(!strcmp(ui_item_name(&a, K_PAGE_FX, 1), "DRIV"));
            assert(!strcmp(ui_item_name(&a, K_PAGE_FX, 5), "RATE"));
            a.params[0][K_PAGE_FX][0] = 15; /* AMP: extended parameters */
            k_set_shown(&a, p->shown);
            a.win = 8;
            ui_draw(&a, &cur, 200);
            assert(!strcmp(ui_item_name(&a, K_PAGE_FX, 8), "BASS"));
            assert(!strcmp(ui_item_name(&a, K_PAGE_FX, 9), "MID"));
            assert(!strcmp(ui_item_name(&a, K_PAGE_FX, 10), "TREB"));
            ui_draw_shutdown();
        }
    }
    /* Wide ENV2 navigation was silently redirected to ENV1 with window 0.
     * Check both synth types, the real menu selection and device rotation. */
    {
        const int synths[1] = {k_engine_find(K_UID('K','A','N','L'))};   /* (PHASE has one page of three: below) */
        for (int e = 0; e < 1; e++) {
            KApp a;
            assert(synths[e] >= 0);
            k_init(&a);
            a.sound[0] = synths[e];
            k_set_layout(&a, K_LAY_ENV_PAIR);
            k_open_page(&a, K_PAGE_ENV2);
            assert(a.page == K_PAGE_ENV1 && a.win == 8);
            assert(k_page_count(&a, K_PAGE_ENV1) >= 12);
            { int ix = 8; assert(k_item_page(&a, K_PAGE_ENV1, &ix) == K_PAGE_ENV2 && ix == 0); }
            k_set_layout(&a, 0);
            assert(a.page == K_PAGE_ENV2 && a.win == 0);
            k_set_layout(&a, K_LAY_ENV_PAIR);
            assert(a.page == K_PAGE_ENV1 && a.win == 8);
            k_open_page(&a, K_PAGE_ENV1);
            assert(a.page == K_PAGE_ENV1 && a.win == 0);
            k_open_page(&a, K_PAGE_ENV2);
            assert(a.win == 8);
        }
        {   /* PHASE: ENV 2 opens its one page on the DCA row, on either screen, and a turn of the screen keeps it */
            KApp a;
            k_init(&a);
            a.sound[0] = k_engine_find(K_UID('K','P','H','S'));
            k_set_layout(&a, K_LAY_ENV_PAIR);
            k_open_page(&a, K_PAGE_ENV2); assert(a.page == K_PAGE_ENV1 && a.win == 8 && k_page_count(&a, K_PAGE_ENV1) == 12);
            k_set_layout(&a, 0); assert(a.page == K_PAGE_ENV1 && a.win == 8);
            k_open_page(&a, K_PAGE_ENV2); assert(a.page == K_PAGE_ENV1 && a.win == 8);
        }
        { /* Drum ENV2 stays on its single lane ENV page, not at item 8. */
            KApp a;
            k_init(&a);
            a.sound[0] = k_engine_find(K_UID('D','R','U','M'));
            k_set_layout(&a, K_LAY_ENV_PAIR);
            k_open_page(&a, K_PAGE_ENV2);
            assert(a.page == K_PAGE_ENV1 && a.win == 0);
        }
    }
    {   /* a page the engine does without (ORGAN's ENV): drawn over the last page, it leaves nothing of it (the ground is not painted
         * under a panel page) */
        static uint32_t fresh[480 * 320];
        int w, h, i, d = 0, pg;
        const KProfile *p = k_profile_find("fm1");
        KApp b; KCursorVisual c = {0}; const GCanvas *cv;
        k_profile_size(p, &w, &h);
        for (pg = 0; pg < 2; pg++) {
            const KPage page = pg ? K_PAGE_LFO : K_PAGE_ENV1;
            assert(ui_draw_init(p, K_THEME_DARK));
            k_init(&b); k_set_shown(&b, p->shown);
            b.sound[0] = k_engine_find(K_UID('K','O','G','N')); k_open_page(&b, page);
            ui_draw(&b, &c, 0); cv = ui_draw_canvas();
            memcpy(fresh, cv->px, (size_t)w * h * 4);
            b.sound[0] = 0; k_open_page(&b, page); ui_draw(&b, &c, 0);       /* ANALOG's page ... */
            b.sound[0] = k_engine_find(K_UID('K','O','G','N')); k_open_page(&b, page); ui_draw(&b, &c, 0);   /* ... then ORGAN's */
            for (i = 0; i < w * h; i++) d += cv->px[i] != fresh[i];
            ui_draw_shutdown();
        }
        if (d) printf("organ's empty pages: %d pixels left of the last page\n", d);
        assert(d == 0);
    }
    {   /* taps (ui_tap): the settings' rows of the tab shown, their tabs, outside the box; SAVE's slots and actions */
        KApp t; KCursorVisual c = {0};
        assert(ui_draw_init(k_profile_find("handheld"), K_THEME_DARK));
        k_init(&t); t.settings_open = 1; t.settings_sel = K_SET_RESTORE;
        ui_draw(&t, &c, 0);
        {   /* the RESTORE LAST row: where page_settings put it (the DISPLAY tab's rows must not answer there) */
            int x, y, hit = 0;
            for (y = 0; y < 180 && !hit; y++) for (x = 0; x < 320 && !hit; x++)
                if (ui_hit_menu_index(&t, x, y) == K_SET_RESTORE) { ui_tap(&t, x, y, 10); hit = 1; }
            assert(hit && t.settings_sel == K_SET_RESTORE && t.set_v[K_SET_RESTORE] == 1);   /* a tap on the row in hand: its value */
            ui_draw(&t, &c, 20); ui_tap(&t, x, y, 30); assert(t.set_v[K_SET_RESTORE] == 0);   /* again: round to ON */
            for (y = 0; y < 180; y++) for (x = 0; x < 320; x++) { const int h = ui_hit_menu_index(&t, x, y); assert(h < 0 || h >= K_SET_RESTORE); }   /* (no DISPLAY row left behind) */
        }
        ui_tap(&t, 2, 178, 40); assert(!t.settings_open);   /* outside the box: closed */
        k_init(&t); k_open_page(&t, K_PAGE_HOME); k_dispatch(&t, (KAction){K_ACT_SAVE, 0, 0, 50}); assert(t.save_open == 1);
        ui_draw(&t, &c, 60);
        {   int x, y, done = 0;
            for (y = 62; y < 180 && !done; y += 2) for (x = 60; x < 120 && !done; x += 2) {   /* (below the two sides, left of the actions) */
                ui_tap(&t, x, y, 70); if (t.save_sel[0] == 2) done = 1;
            }
            assert(done);   /* a slot row picks its slot */
        }
        {   /* NAME by touch (a handheld: no keys for it): a letter takes the cursor, the one in hand touched again the next letter,
             * up along it the letters on; Cancel and Save (no FM-1 keys drawn) */
            int x, y, px = -1, py = -1, n0;
            t.name_open = 1; t.name_pos = 0; snprintf(t.name_buf, sizeof t.name_buf, "SONG");
            ui_draw(&t, &c, 80);
            for (y = 20; y < 180 && px < 0; y += 2) for (x = 0; x < 320 && px < 0; x += 2) {
                t.name_pos = K_NAME_LEN - 1; ui_touch(&t, 0, x, y, 90); ui_touch(&t, 2, x, y, 91);   /* (the cursor kept off the boxes looked at first) */
                if (t.name_pos == 3) { px = x; py = y; }
            }
            assert(px >= 0 && t.name_open && !strcmp(t.name_buf, "SONG"));   /* the fourth letter's box: the cursor there, the letter as it was */
            n0 = t.name_buf[3];
            ui_touch(&t, 0, px, py, 100); ui_touch(&t, 2, px, py, 101); assert(t.name_buf[3] != n0);   /* touched again: the next letter */
            n0 = t.name_buf[3];
            ui_touch(&t, 0, px, py, 110); ui_touch(&t, 1, px, py - 16, 111); ui_touch(&t, 2, px, py - 16, 112);
            assert(t.name_buf[3] != n0 && t.name_pos == 3);   /* up 16 px: two letters on, no extra on letting go */
            {   int closed = 0;
                for (y = py + 10; y < 180 && !closed; y += 2) for (x = 0; x < 320 && !closed; x += 2) {
                    ui_touch(&t, 0, x, y, 120); ui_touch(&t, 2, x, y, 121);
                    if (!t.name_open) closed = 1;
                }
                assert(closed);   /* a button under the letters closes it (Cancel or Save) */
            }
        }
        {   /* a swipe on a value: down on the mixer's first card, up 20 px: five steps up; down again: back */
            int x, y, found = 0, v0;
            k_init(&t); k_open_page(&t, K_PAGE_MIXER);
            ui_draw(&t, &c, 100);
            for (y = 179; y > 0 && !found; y--) for (x = 0; x < 160 && !found; x++) {
                const int n = k_hit_focus(&t, x, y);
                if (n >= 0 && t.focus[n].kind == K_NODE_PARAM && t.focus[n].index == 0) found = 1;
            }
            assert(found);
            x--; y++;   /* (the loop's last step past it) */
            v0 = t.params[0][K_PAGE_MIXER][0];
            ui_touch(&t, 0, x, y, 110);
            {   const KRect r = t.focus[k_hit_focus(&t, x, y)].bounds;   /* a card slides sideways (a fader, a knob: up and down) */
                if (r.w >= 2 * r.h) { ui_touch(&t, 1, x + 10, y, 120); ui_touch(&t, 1, x + 20, y, 130); }
                else { ui_touch(&t, 1, x, y - 10, 120); ui_touch(&t, 1, x, y - 20, 130); } }
            assert(t.params[0][K_PAGE_MIXER][0] == v0 + 5);
            ui_touch(&t, 1, x, y, 140); ui_touch(&t, 2, x, y, 150);
            assert(t.params[0][K_PAGE_MIXER][0] == v0);
            {   /* a drag from where no value is: nothing turns (not the one that had the focus) */
                int ex, ey, got = 0;
                for (ey = 40; ey < 170 && !got; ey++) for (ex = 0; ex < 320 && !got; ex++)
                    if (k_hit_focus(&t, ex, ey) < 0 && !ui_hit_header(ex, ey)) got = 1;
                ex--; ey--;
                assert(got);
                ui_touch(&t, 0, ex, ey, 160); ui_touch(&t, 1, ex, ey - 20, 170); ui_touch(&t, 2, ex, ey - 20, 180);
                assert(t.params[0][K_PAGE_MIXER][0] == v0);
            }
        }
        ui_draw_shutdown();
    }
    {   /* the Switch's synth: ANALOG, its ENV and LFO on the same panel, each a target: a swipe on ENV 1's DEC turns ENV 1's DEC */
        KApp w; KCursorVisual c = {0};
        const KProfile *sp = k_profile_find("switch");
        int i, n = -1, d0;
        assert(ui_draw_init(sp, K_THEME_DARK));
        k_init(&w); k_set_shown(&w, sp->shown); k_set_layout(&w, k_profile_layout(sp));
        assert(w.lay & K_LAY_SYNTH_ALL);
        k_open_page(&w, K_PAGE_SOUND); ui_draw(&w, &c, 100);
        for (i = 0; i < w.focus_count; i++)
            if (w.focus[i].kind == K_NODE_PARAM && w.focus[i].index == k_engine_at(w.sound[0])->nparams + 1) n = i;
        assert(n >= 0 && w.focus[n].bounds.w > 0);
        d0 = w.params[0][K_PAGE_ENV1][1];
        {   const KRect r = w.focus[n].bounds;
            ui_touch(&w, 0, r.x + r.w / 2, r.y + r.h / 2, 110); ui_touch(&w, 1, r.x + r.w / 2, r.y + r.h / 2 - 12, 120); ui_touch(&w, 2, r.x + r.w / 2, r.y + r.h / 2 - 12, 130); }
        assert(w.params[0][K_PAGE_ENV1][1] == d0 + 3);
        ui_draw_shutdown();
    }
    {   /* the piano roll under a finger (the Switch's and the handheld's screens): a cell drawn, made longer, moved, a tap deletes */
        static const char *const PR[2] = {"switch", "handheld"};
        int pi;
        for (pi = 0; pi < 2; pi++) {
            KApp a; KCursorVisual c = {0};
            const KProfile *pf = k_profile_find(PR[pi]);
            uint32_t ms = 1000;
            int i, x0, y0, cw, n, slot;
            assert(ui_draw_init(pf, K_THEME_DARK));
            k_init(&a); k_set_shown(&a, pf->shown); k_set_layout(&a, k_profile_layout(pf));
            k_roll_fill(&a, 0, 0, 0, 60);
            k_open_page(&a, K_PAGE_ROLL);
            for (i = 0; i < 5; i++) ui_draw(&a, &c, ms += 16);
            {   /* the cells, from the step columns the roll set (K_NODE_STEP) */
                const KRect r0 = a.focus[0].bounds, r1 = a.focus[1].bounds;
                assert(a.focus[0].kind == K_NODE_STEP && r0.w > 0);
                cw = r1.x - r0.x; x0 = r0.x + cw / 2; y0 = r0.y + r0.h / 2;
            }
            ui_touch(&a, 0, x0 + 2 * cw, y0, ms); ui_touch(&a, 1, x0 + 4 * cw, y0, ms + 10); ui_touch(&a, 2, x0 + 4 * cw, y0, ms + 20);   /* drawn on step 2, 3 long */
            assert(k_roll_count(&a, 0, 0, 2) == 1);
            for (slot = 0; slot < K_ROLL_NOTES && !a.roll[0][slot].len; slot++) {}
            assert(slot < K_ROLL_NOTES && a.roll[0][slot].len == 3 && a.rlen[0] == 3);
            n = a.roll[0][slot].note;
            ui_draw(&a, &c, ms += 16);
            ui_touch(&a, 0, x0 + 2 * cw, y0, ms); ui_touch(&a, 1, x0 + 6 * cw, y0 - 2 * (a.focus[0].bounds.h / 21), ms + 10); ui_touch(&a, 2, x0 + 6 * cw, y0, ms + 20);   /* moved */
            assert((a.roll[0][slot].at & 15) == 6 && a.roll[0][slot].len == 3 && a.roll[0][slot].note > n && k_roll_count(&a, 0, 0, 2) == 0);
            ui_draw(&a, &c, ms += 16);
            ui_touch(&a, 0, x0 + 6 * cw, y0 - 2 * (a.focus[0].bounds.h / 21), ms); ui_touch(&a, 2, x0 + 6 * cw, y0 - 2 * (a.focus[0].bounds.h / 21), ms + 10);   /* a tap: gone */
            assert(!a.roll[0][slot].len && !a.steps[0][0]);
            {   int got; KNoteEv e; int on = 0, off = 0; while ((got = k_take_note(&a, &e))) { if (e.vel) on++; else off++; } assert(on >= 2 && on == off); }   /* (heard while held, let go) */
            ui_draw_shutdown();
        }
    }
    {   /* - 0 +: PITCH's SEMI in semitones, a centred value against its default, a range below 0 with its plus */
        KApp a; char b[16]; const char *u;
        const KParamDesc semi = {"SEMI", KF_FXSEMI, 0, 100, 50, 0, 0}, tune = {"TUNE", KF_CENTER, 0, 127, 64, 0, 0}, swp = {"SWP", KF_INT, -64, 63, 0, 0, 0};
        (void)a;
        assert(ui_draw_init(k_profile_find("fm1"), K_THEME_DARK));
        ui_format_desc(&semi, 50, b, sizeof b, &u); assert(!strcmp(b, "0") && !strcmp(u, "st"));
        ui_format_desc(&semi, 100, b, sizeof b, &u); assert(!strcmp(b, "+12"));
        ui_format_desc(&semi, 0, b, sizeof b, &u); assert(!strcmp(b, "-12"));
        ui_format_desc(&tune, 64, b, sizeof b, &u); assert(!strcmp(b, "0"));
        ui_format_desc(&tune, 70, b, sizeof b, &u); assert(!strcmp(b, "+6"));
        ui_format_desc(&tune, 60, b, sizeof b, &u); assert(!strcmp(b, "-4"));
        ui_format_desc(&swp, 5, b, sizeof b, &u); assert(!strcmp(b, "+5"));
        ui_format_desc(&swp, -5, b, sizeof b, &u); assert(!strcmp(b, "-5"));
        ui_draw_shutdown();
    }
    {   /* the exponential scales' text in integers (no FPU on the FM-1): every value as the float formulas printed it */
        const KParamDesc cut = {"CUT", KF_CUTOFF, 0, 127, 0, 0, 0}, rate = {"RATE", KF_LFOHZ, 0, 127, 0, 0, 0}, tm = {"TIME", KF_TIME, 0, 127, 0, 0, 0};
        char b[16], f[16];
        const char *u, *fu;
        int v;
        for (v = 0; v <= 127; v++) {
            const float hz = 30.f * powf(16000.f / 30.f, (float)v / 127.f), lf = .05f * powf(800.f, (float)v / 127.f), ms = powf(10000.f, (float)v / 127.f);
            ui_format_desc(&cut, v, b, sizeof b, &u);
            if (hz < 1000.f) { snprintf(f, sizeof f, "%d", (int)(hz + .5f)); fu = "Hz"; } else { snprintf(f, sizeof f, "%.1f", (double)(hz / 1000.f)); fu = "kHz"; }
            if (strcmp(b, f) || strcmp(u, fu)) { fprintf(stderr, "cutoff %d: %s%s, was %s%s\n", v, b, u, f, fu); assert(0); }
            ui_format_desc(&rate, v, b, sizeof b, &u);
            snprintf(f, sizeof f, lf < 10.f ? "%.2f" : "%.1f", (double)lf);
            if (strcmp(b, f) || strcmp(u, "Hz")) { fprintf(stderr, "rate %d: %s, was %s\n", v, b, f); assert(0); }
            ui_format_desc(&tm, v, b, sizeof b, &u);
            if (ms < 100.f) { snprintf(f, sizeof f, "%.1f", (double)ms); fu = "ms"; }
            else if (ms < 10000.f) { snprintf(f, sizeof f, "%d", (int)(ms + .5f)); fu = "ms"; }
            else { snprintf(f, sizeof f, "%.1f", (double)(ms / 1000.f)); fu = "s"; }
            if (strcmp(b, f) || strcmp(u, fu)) { fprintf(stderr, "time %d: %s%s, was %s%s\n", v, b, u, f, fu); assert(0); }
        }
        for (v = 0; v <= 100; v++) assert(k_level_db10(v) == (int)(k_level_db(v) * 10.f + (k_level_db(v) < 0.f ? -.5f : .5f)));   /* the fader's dB */
        for (v = -100; v <= 12; v++) assert(k_db_level_i(v) == k_db_level((float)v));
    }
    {   /* the desktop's mouse: the wheel turns the value under the pointer, and the tools' row under it (the rail's, closed: it opens) */
        KApp b; KCursorVisual c = {0}; int k, v0, hit = -1;
        k_init(&b); k_set_layout(&b, k_profile_layout(k_profile_find("mac")));
        assert(ui_draw_init(k_profile_find("mac"), K_THEME_DARK));
        k_open_page(&b, K_PAGE_MIXER); ui_draw(&b, &c, 0);
        for (k = 0; k < b.focus_count; k++) if (b.focus[k].kind == K_NODE_PARAM && b.focus[k].bounds.w) { hit = k; break; }
        assert(hit >= 0);
        v0 = k_param_get(&b, K_PAGE_MIXER, b.focus[hit].index);
        ui_wheel(&b, b.focus[hit].bounds.x + b.focus[hit].bounds.w / 2, b.focus[hit].bounds.y + b.focus[hit].bounds.h / 2, -1, 10);
        assert(b.focused == hit && k_param_get(&b, K_PAGE_MIXER, b.focus[hit].index) == v0 - 1);
        k_open_page(&b, K_PAGE_ROLL); b.playing = 0; ui_draw(&b, &c, 20);   /* the rail's first row (FILL), the list closed: opened on it, EVERY 4 -> 5 */
        {   const int cx = 640 - 75, x0 = b.tool_x[K_TOOL_FILL];
            int y;
            for (y = 30; y < 200 && !b.tools_open; y += 2) ui_wheel(&b, cx, y, 1, 30);
            assert(b.tools_open && b.tool_x[K_TOOL_FILL] == x0 + 1 && b.tools_try == 0); }
        {   const int m0 = b.master; int x;   /* the header's level knob (a desktop has no knob of its own): the wheel over it */
            for (x = 200; x < 440 && b.master == m0; x++) ui_wheel(&b, x, 10, 1, 40);
            assert(b.master == m0 + 2); }
        {   int k2, fx = -1; KRect r = {0, 0, 0, 0};   /* the mixer's fader follows the pointer: its top 100, its foot 0 */
            k_open_page(&b, K_PAGE_MIXER); ui_draw(&b, &c, 50);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].index == 0) { fx = k2; r = b.focus[k2].bounds; }
            assert(fx >= 0 && r.h > 40);
            ui_touch(&b, 0, r.x + r.w / 2, r.y + 4, 60); assert(b.params[b.track][K_PAGE_MIXER][0] == 100);
            ui_touch(&b, 1, r.x + r.w / 2, r.y + r.h - 4, 70); assert(b.params[b.track][K_PAGE_MIXER][0] == 0);
            ui_touch(&b, 1, r.x + r.w / 2, r.y + r.h / 2, 80); assert(b.params[b.track][K_PAGE_MIXER][0] > 40 && b.params[b.track][K_PAGE_MIXER][0] < 60);
            ui_touch(&b, 2, r.x + r.w / 2, r.y + r.h / 2, 90); }
        {   int k2, tn = -1; KRect r = {0, 0, 0, 0};   /* HOME: a track tapped twice opens its sequencer */
            k_open_page(&b, K_PAGE_HOME); ui_draw(&b, &c, 100);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_TRACK && b.focus[k2].index == 1) { tn = k2; r = b.focus[k2].bounds; }
            assert(tn >= 0);
            ui_touch(&b, 0, r.x + r.w / 2, r.y + r.h / 2, 1000); ui_touch(&b, 2, r.x + r.w / 2, r.y + r.h / 2, 1050);
            assert(b.page == K_PAGE_HOME && b.track == 1);
            ui_touch(&b, 0, r.x + r.w / 2, r.y + r.h / 2, 1200); ui_touch(&b, 2, r.x + r.w / 2, r.y + r.h / 2, 1250);
            assert(b.track == 1 && b.page == k_seq_page(&b)); }
        {   int t2 = (b.track + 1) % K_TRACKS, x, y, got = 0;   /* a desktop's mixer: another strip's fader, no strip to pick first; its M */
            k_open_page(&b, K_PAGE_MIXER); ui_draw(&b, &c, 1290);
            b.params[t2][K_PAGE_MIXER][0] = 50;
            for (y = 60; y < 300 && !got; y += 2) for (x = 0; x < 640 && !got; x += 2) {   /* (the top of a fader: that track's level to 100) */
                const int tb = b.track;
                ui_touch(&b, 0, x, y, 1291); ui_touch(&b, 2, x, y, 1292);
                if (b.track == t2 && b.params[t2][K_PAGE_MIXER][0] == 100) got = 1;
                else if (b.track != tb) { b.track = tb; }
                ui_draw(&b, &c, 1290);
            }
            assert(got); }
        {   int e, k2, n2 = -1; KRect r = {0, 0, 0, 0};   /* ORGAN's drawbar: its cap goes where the pointer is (down: pulled out) */
            for (e = 0; e < k_engine_count(); e++) if (k_engine_uid(e) == K_UID('K', 'O', 'G', 'N')) k_track_engine(&b, b.track, e);
            assert(k_engine_uid(b.sound[b.track]) == K_UID('K', 'O', 'G', 'N'));
            k_open_page(&b, K_PAGE_SOUND); ui_draw(&b, &c, 1295);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].index == 4) { n2 = k2; r = b.focus[k2].bounds; }
            assert(n2 >= 0 && r.h > 40);
            b.preset_dirty[b.track] = 0;
            ui_touch(&b, 0, r.x + r.w / 2, r.y + r.h - 20, 1296); assert(k_param_get(&b, K_PAGE_SOUND, 4) == 8);
            ui_touch(&b, 1, r.x + r.w / 2, r.y, 1297); assert(k_param_get(&b, K_PAGE_SOUND, 4) == 0);
            assert(b.preset_dirty[b.track]);   /* (set through the core's k_param_set: the preset marked changed) */
            {   const int s0 = b.params[b.track][K_PAGE_SCL][0], s1 = b.params[b.track][K_PAGE_SCL][1];   /* a scale stored past its range
                                                 * (an old song's): SCL and ROLL draw it as the table keeps it (no index past the scales; ASan) */
                b.params[b.track][K_PAGE_SCL][0] = -40; b.params[b.track][K_PAGE_SCL][1] = 99;
                k_open_page(&b, K_PAGE_SCL); ui_draw(&b, &c, 1298); k_open_page(&b, K_PAGE_ROLL); ui_draw(&b, &c, 1298);
                b.params[b.track][K_PAGE_SCL][0] = s0; b.params[b.track][K_PAGE_SCL][1] = s1; }
            ui_touch(&b, 2, r.x + r.w / 2, r.y, 1298); }
        {   int k2, done = 0, x, y; KRect r = {0, 0, 0, 0};   /* option-click: a synth's value, any mixer strip's, back at its default */
            k_open_page(&b, K_PAGE_SOUND); ui_draw(&b, &c, 1299);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].index == 4) r = b.focus[k2].bounds;
            *k_param(&b, K_PAGE_SOUND, 4) = 1;
            assert(ui_reset_at(&b, r.x + r.w / 2, r.y + r.h / 2, 1299) && k_param_get(&b, K_PAGE_SOUND, 4) == k_engine_at(b.sound[b.track])->edit[4].def);
            k_open_page(&b, K_PAGE_MIXER); ui_draw(&b, &c, 1299);
            for (k2 = 0; k2 < K_TRACKS; k2++) b.params[k2][K_PAGE_MIXER][1] = 10;
            for (y = 40; y < 200 && !done; y++) for (x = 0; x < 640 && !done; x++)
                if (ui_reset_at(&b, x, y, 1299) && b.params[b.track][K_PAGE_MIXER][1] == 50) done = 1;
            assert(done);
            {   int e; for (e = 0; e < k_engine_count(); e++) if (k_engine_uid(e) == K_UID('K', 'A', 'N', 'L')) k_track_engine(&b, b.track, e); }
            b.params[b.track][K_PAGE_ENV1][0] = 70; b.params[b.track][K_PAGE_ENV1][2] = 10; b.params[b.track][K_PAGE_ENV1][4] = 80;
            assert(k_param_reset(&b, K_PAGE_ENV1, 0) && b.params[b.track][K_PAGE_ENV1][0] == 0);   /* an envelope: no attack, */
            assert(k_param_reset(&b, K_PAGE_ENV1, 2) && b.params[b.track][K_PAGE_ENV1][2] == 100);   /* full sustain, */
            k_param_reset(&b, K_PAGE_ENV1, 4); assert(b.params[b.track][K_PAGE_ENV_DEST][0] == 50); }   /* a destination: none */
        {   int k2;   /* a big screen: EDIT, ENV, LFO are in SOUND, not pages of their own (not listed; opening one opens SOUND) */
            k_open_page(&b, K_PAGE_ENV1); assert(b.page == K_PAGE_SOUND);
            k_dispatch(&b, (KAction){K_ACT_PAGE_MENU, 0, 0, 1299}); assert(b.menu_open && b.menu_group == K_GROUP_ALL);
            assert(k_menu_count(&b) == 12);
            for (k2 = 0; k2 < k_menu_count(&b); k2++) assert(!k_page_folded(&b, k_menu_page(&b, k2)));
            k_dispatch(&b, (KAction){K_ACT_BACK, 0, 0, 1299}); }
        {   int k2, hit = -1;   /* a pointer over a control: lit (ui_hover says what it is over changed); gone: nothing lit */
            k_open_page(&b, K_PAGE_SOUND); ui_draw(&b, &c, 1299);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].bounds.w) { hit = k2; break; }
            assert(hit >= 0);
            ui_hover(&b, -1, -1, 0);
            assert(ui_hover(&b, b.focus[hit].bounds.x + 2, b.focus[hit].bounds.y + 2, 0) == 1);
            assert(ui_hover(&b, b.focus[hit].bounds.x + 3, b.focus[hit].bounds.y + 2, 0) == 0);   /* (the same control: no new frame needed) */
            ui_draw(&b, &c, 1299);
            assert(ui_hover(&b, -1, -1, 0) == 1); }
        {   int k2, hit = -1, e, n; KRect r = {0, 0, 0, 0}, rows[24];   /* a right click: the menu; its rows picked by a left click */
            for (e = 0; e < k_engine_count(); e++) if (k_engine_uid(e) == K_UID('K', 'A', 'N', 'L')) k_track_engine(&b, b.track, e);
            k_open_page(&b, K_PAGE_SOUND); ui_draw(&b, &c, 1299);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].index == 1 && b.focus[k2].bounds.w) { hit = k2; r = b.focus[k2].bounds; }
            assert(hit >= 0);
            k_param_set(&b, K_PAGE_SOUND, 1, 77);
            assert(ui_context(&b, r.x + r.w / 2, r.y + r.h / 2, 1300) && ui_popup_open());
            ui_draw(&b, &c, 1301); n = ui_popup_rows(rows, 24);   /* (its rows placed): Reset, Copy, Paste */
            assert(n >= 3);
            ui_popup_click(&b, rows[1].x + 4, rows[1].y + 4, 1302); assert(!ui_popup_open());   /* Copy (77) */
            ui_popup_click(&b, 0, 0, 1303);
            ui_context(&b, r.x + r.w / 2, r.y + r.h / 2, 1304); ui_draw(&b, &c, 1305); ui_popup_rows(rows, 24);
            ui_popup_click(&b, rows[0].x + 4, rows[0].y + 4, 1306);   /* Reset */
            assert(k_param_get(&b, K_PAGE_SOUND, 1) == k_engine_at(b.sound[b.track])->edit[1].def);
            ui_context(&b, r.x + r.w / 2, r.y + r.h / 2, 1307); ui_draw(&b, &c, 1308); ui_popup_rows(rows, 24);
            ui_popup_click(&b, rows[2].x + 4, rows[2].y + 4, 1309);   /* Paste: 77 again */
            assert(k_param_get(&b, K_PAGE_SOUND, 1) == k_param_clamp(&k_engine_at(b.sound[b.track])->edit[1], 77));
            ui_context(&b, 300, 10, 1310); assert(!ui_popup_open()); }   /* (the header: none) */
        {   int t2, k2, x, y, got = 0; KRect rows[24];   /* the mixer: a click on an insert slot, the effects; one picked goes in */
            k_open_page(&b, K_PAGE_MIXER); ui_draw(&b, &c, 1320);
            for (y = 40; y < 140 && !got; y++) for (x = 0; x < 640 && !got; x++) {
                ui_touch(&b, 0, x, y, 1321);
                if (ui_popup_open()) got = 1; else { ui_touch(&b, 2, x, y, 1322); ui_draw(&b, &c, 1320); }
            }
            assert(got);
            ui_draw(&b, &c, 1323); k2 = ui_popup_rows(rows, 24);
            assert(k2 == k_effect_count() + 1);
            t2 = b.track;
            ui_popup_click(&b, rows[3].x + 4, rows[3].y + 2, 1324);
            assert(!ui_popup_open() && (b.params[t2][K_PAGE_FX][0] == 3 || b.params[t2][K_PAGE_FX][4] == 3)); }
        {   int k2, i2 = -1; uint16_t was;   /* a desktop's tools: the row in hand open, its slider dragged (tried at once), Apply keeps it */
            b.track = 0; k_track_engine(&b, 0, 0); k_open_page(&b, K_PAGE_ROLL); b.playing = 0;
            k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 1400});
            for (k2 = 0; k2 < k_tools_count(&b); k2++) if (k_tools_item(&b, k2) == K_TOOL_EUCLID) i2 = k2;
            b.tools_sel = (int8_t)i2; ui_draw(&b, &c, 1401);
            was = b.steps[0][b.pattern[0] & 3];
            {   KRect r2;   /* HITS: its slider's right end (16), then Apply */
                assert(ui_tool_part(0, &r2) && ui_tool_part(1, &r2) && ui_tool_part(3, &r2));
                ui_tool_part(0, &r2);
                ui_touch(&b, 0, r2.x + r2.w - 6, r2.y + r2.h / 2, 1402); ui_touch(&b, 2, r2.x + r2.w - 6, r2.y + r2.h / 2, 1403);
                assert(b.tool_x[K_TOOL_EUCLID] == 16 && b.tools_try == i2 && b.steps[0][b.pattern[0] & 3] == 0xFFFF);
                ui_touch(&b, 0, r2.x + 6 + (r2.w - 12) * 5 / 16, r2.y + r2.h / 2, 1404); ui_touch(&b, 2, r2.x + 6 + (r2.w - 12) * 5 / 16, r2.y + r2.h / 2, 1405);
                assert(b.tool_x[K_TOOL_EUCLID] == 5); }
            ui_draw(&b, &c, 1406);
            {   KRect ap; assert(ui_tool_part(3, &ap)); ui_tap(&b, ap.x + 4, ap.y + 4, 1407); }   /* Apply */
            assert(!b.tools_open && b.steps[0][b.pattern[0] & 3] == k_euclid(5, b.tool_y[K_TOOL_EUCLID]) && was != 0x1234); }
        {   int c2, x, y, got = 0; uint8_t col[K_TRACKS];   /* SONG: a scene's number selects its column; Cmd+C, then on another, Cmd+V */
            k_open_page(&b, K_PAGE_SONG); ui_draw(&b, &c, 1500);
            for (c2 = 0; c2 < K_TRACKS; c2++) col[c2] = b.song[2][c2];
            for (x = 0; x < 640 && !got; x++) for (y = 36; y < 56 && !got; y++) {   /* (the ruler: scene 3's number) */
                ui_touch(&b, 0, x, y, 1501); ui_touch(&b, 2, x, y, 1502);
                if (ui_edit(&b, 0, 1503)) got = 1;
            }
            assert(got);
            {   int g2 = 0, xx;   /* (copied whichever column the scan found first: paste it onto scene 6) */
                for (xx = 639; xx >= 0 && !g2; xx--) for (y = 36; y < 56 && !g2; y++) {
                    ui_touch(&b, 0, xx, y, 1504); ui_touch(&b, 2, xx, y, 1505);
                    if (ui_edit(&b, 1, 1506)) g2 = 1;
                }
                assert(g2); }
            (void)col; }
        {   int k2, n1 = -1, n2 = -1; uint16_t was;   /* DRUM: a step dragged across others, they take its new state */
            int e; for (e = 0; e < k_engine_count(); e++) if (k_engine_uid(e) == K_UID('D', 'R', 'U', 'M')) k_track_engine(&b, 1, e);
            b.track = 1; k_open_page(&b, K_PAGE_DRUM); ui_draw(&b, &c, 1510);
            b.lane_on[1][2] = 0;
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_STEP) { if (b.focus[k2].index == 2 * K_STEPS) n1 = k2; if (b.focus[k2].index == 2 * K_STEPS + 5) n2 = k2; }
            assert(n1 >= 0 && n2 >= 0);
            was = b.lane_on[1][2];
            ui_touch(&b, 0, b.focus[n1].bounds.x + 2, b.focus[n1].bounds.y + 2, 1511);
            {   int st; for (st = 1; st <= 5; st++) { const KRect r3 = b.focus[n1 + st].bounds; ui_touch(&b, 1, r3.x + 2, r3.y + 2, 1512 + (uint32_t)st); } }
            ui_touch(&b, 2, b.focus[n2].bounds.x + 2, b.focus[n2].bounds.y + 2, 1520);
            assert(was == 0 && b.lane_on[1][2] == 0x003F); }
        {   int v0, k2;   /* a pad: B up to the header, right to play, A plays; down back; A held + up turns the value */
            k_open_page(&b, K_PAGE_SOUND); b.playing = 0; ui_draw(&b, &c, 1600);
            ui_pad(&b, KP_B, 1, 1601); ui_pad(&b, KP_B, 0, 1602);
            ui_pad(&b, KP_RIGHT, 1, 1603);   /* (the track chip, then play) */
            ui_pad(&b, KP_A, 1, 1604); ui_pad(&b, KP_A, 0, 1605);
            assert(b.playing == 1);
            ui_pad(&b, KP_A, 1, 1606); ui_pad(&b, KP_A, 0, 1607); assert(b.playing == 0);
            ui_pad(&b, KP_DOWN, 1, 1608);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_PARAM && b.focus[k2].index == 1) b.focused = k2;
            v0 = k_param_get(&b, K_PAGE_SOUND, 1);
            ui_pad(&b, KP_A, 1, 1609); ui_pad(&b, KP_UP, 1, 1610); ui_pad(&b, KP_UP, 1, 1611); ui_pad(&b, KP_A, 0, 1612);
            assert(k_param_get(&b, K_PAGE_SOUND, 1) != v0 && b.playing == 0);   /* (turned, and A's release pressed nothing) */
            ui_pad(&b, KP_SELECT, 1, 1700); assert(!ui_pad_tick(&b, 1900)); assert(ui_pad_tick(&b, 2210) && ui_popup_open());   /* held: the menu */
            ui_pad(&b, KP_SELECT, 0, 2300); assert(ui_popup_open());   /* (its release: the menu stays) */
            ui_pad(&b, KP_DOWN, 1, 2310); ui_pad(&b, KP_B, 1, 2320); assert(!ui_popup_open());
            {   const int oct = b.octave;   /* the triggers the octave; SELECT held + START: SAVE (its release: no page list) */
                ui_pad(&b, KP_R2, 1, 2400); ui_pad(&b, KP_R2, 0, 2401); assert(b.octave == oct + 1);
                ui_pad(&b, KP_L2, 1, 2402); ui_pad(&b, KP_L2, 0, 2403); assert(b.octave == oct);
                ui_pad(&b, KP_SELECT, 1, 2410); ui_pad(&b, KP_START, 1, 2411); ui_pad(&b, KP_START, 0, 2412); ui_pad(&b, KP_SELECT, 0, 2413);
                assert(b.save_open && !b.menu_open); k_dispatch(&b, (KAction){K_ACT_BACK, 0, 0, 2414}); assert(!b.save_open); } }
        {   /* the page switcher's Config: the settings */
            int x, y, hit = 0;
            k_open_page(&b, K_PAGE_HOME); k_dispatch(&b, (KAction){K_ACT_PAGE_MENU, 0, 0, 1300}); ui_draw(&b, &c, 1310);
            assert(b.menu_open && b.menu_group == K_GROUP_ALL);
            for (y = 20; y < 56 && !hit; y++) for (x = 560; x < 640 && !hit; x++) { ui_tap(&b, x, y, 1320); if (b.settings_open) hit = 1; else if (!b.menu_open) { k_dispatch(&b, (KAction){K_ACT_PAGE_MENU, 0, 0, 1300}); ui_draw(&b, &c, 1310); } }
            assert(hit && !b.menu_open);
            k_dispatch(&b, (KAction){K_ACT_SETTINGS, 0, 0, 1400}); assert(!b.settings_open); }
        {   int k2, e0; KRect r = {0, 0, 0, 0};   /* the engine's bar: its left half the engine before, its right half the one after */
            b.track = 0; k_open_page(&b, K_PAGE_SOUND); ui_draw(&b, &c, 1500);
            for (k2 = 0; k2 < b.focus_count; k2++) if (b.focus[k2].kind == K_NODE_SOUND) r = b.focus[k2].bounds;
            assert(r.w > 0);
            e0 = b.sound[0];
            ui_tap(&b, r.x + r.w - 4, r.y + r.h / 2, 1510); assert(b.sound[0] == e0 + 1);
            ui_draw(&b, &c, 1520);
            ui_tap(&b, r.x + 4, r.y + r.h / 2, 1530); assert(b.sound[0] == e0); }
        ui_draw_shutdown();
    }
    {   /* a high-density screen: the canvas at 3 pixels a unit, every page drawn (the layout's units unchanged) */
        KApp d; KCursorVisual c = {0}; int pg; const GCanvas *cv;
        k_init(&d); k_set_layout(&d, k_profile_layout(k_profile_find("mac")));
        assert(ui_draw_init(k_profile_find("mac"), K_THEME_DARK) && ui_draw_set_scale(3));
        for (pg = 0; pg < K_PAGE_COUNT; pg++) { k_open_page(&d, (KPage)pg); ui_draw(&d, &c, 100 + (uint32_t)pg); }
        cv = ui_draw_canvas();
        assert(cv->w == 640 && cv->h == 360 && cv->s == 3 && cv->pw == 1920 && cv->ph == 1080 && cv->px[1080 * 1920 / 2 + 960] != 0);
        assert(ui_draw_set_scale(1) && ui_draw_canvas()->s == 1);
        ui_draw_shutdown();
    }
    puts("kreisel ui tests: OK");
    return 0;
}
