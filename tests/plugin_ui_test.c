/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A plugin that lays out its own screens (FLAT, plugins/flat) as the host draws it: on each kind of screen its SOUND and EDIT are
 * its tables (each parameter a widget the touch and the cursor find, inside the screen), FM2 beside it keeps the vanilla view,
 * and the picture's calls are clipped and counted. An effect that lays out its insert (TILT, plugins/tilt): on the FX page its
 * widgets are the page's items (the knobs, the cursor, a wheel or a touch turn the value its widget shows, and no other), its
 * insert beside a built-in one keeps that one's view; it fits the FM-1's room with no float. */
#include "draw.h"
#include "kreisel_audio.h"
#include "kreisel_plugin.h"
#include "kreisel_plugin_load.h"
#include "kreisel_effect.h"
#include "kreisel_fx.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* KREISEL_PLUGIN_SHOTS=dir: each frame checked written there as a PPM (to look at; nothing is compared) */
static void dump(const char *prof, const char *what)
{
    const char *dir = getenv("KREISEL_PLUGIN_SHOTS");
    const GCanvas *g = ui_draw_canvas();
    char path[512];
    FILE *f;
    int i;
    if (!dir || snprintf(path, sizeof path, "%s/%s_%s.ppm", dir, prof, what) >= (int)sizeof path || !(f = fopen(path, "wb"))) return;
    fprintf(f, "P6 %d %d 255\n", g->pw, g->ph);
    for (i = 0; i < g->pw * g->ph; i++) { const uint32_t c = g->px[i]; fputc((int)(c >> 16 & 255), f); fputc((int)(c >> 8 & 255), f); fputc((int)(c & 255), f); }
    fclose(f);
}
static void check(const char *prof, int eng, KPage pg)
{
    const KProfile *p = k_profile_find(prof);
    KApp a;
    KCursorVisual c = {0};
    int i, n, set = 0;
    assert(p && ui_draw_init(p, 0));
    k_init(&a); k_set_shown(&a, p->shown); k_set_layout(&a, k_profile_layout(p));
    a.sound[0] = eng;
    for (i = 0; i < k_engine_at(eng)->nparams; i++) a.params[0][K_PAGE_SOUND][4 + i] = k_engine_at(eng)->edit[i].def;
    if (k_page_folded(&a, pg)) { ui_draw_shutdown(); return; }
    k_open_page(&a, pg);
    ui_draw(&a, &c, 5000);
    ui_draw(&a, &c, 5016);   /* (the bounds of the frame before) */
    {   char w[32]; snprintf(w, sizeof w, "%s_%s", k_engine_at(eng)->name, pg == K_PAGE_EDIT ? "edit" : "sound"); dump(prof, w); }
    n = k_params_visible(&a);
    for (i = 0; i < a.focus_count; i++) {
        const KRect r = a.focus[i].bounds;
        if (a.focus[i].kind != K_NODE_PARAM || a.focus[i].index >= k_engine_at(eng)->nparams) continue;
        if (r.w <= 0 || r.h <= 0) continue;
        assert(r.x >= 0 && r.y >= 0 && r.x + r.w <= ui_draw_canvas()->w && r.y + r.h <= ui_draw_canvas()->h);
        set++;
    }
    assert(set >= (n < 4 ? n : 4));   /* (the knobs' row at least: every widget bound to the page) */
    ui_draw_shutdown();
}

/* TILT in insert 1, DIST in insert 2, on the FX page: each widget of TILT's a node at its item, inside the screen; turning it changes
 * its value alone */
static int tilt;
static int fx_item_for(const KApp *a, int slot, int param)
{
    int item, s2, p2;
    for (item = 0; item < k_page_count(a, K_PAGE_FX); item++) { s2 = -1; p2 = -3; k_fx_item(a, item, &s2, &p2); if (s2 == slot && p2 == param) return item; }
    return -1;
}
static int node_of(const KApp *a, int item)
{
    int i;
    for (i = 0; i < a->focus_count; i++) if (a->focus[i].kind == K_NODE_PARAM && a->focus[i].index == item) return i;
    return -1;
}
static void same_but(const KVal *was, const KVal *now, int st)
{
    int k;
    for (k = 0; k < K_FX_ITEMS; k++) if (k != st) assert(was[k] == now[k]);
    assert(was[st] != now[st]);
}
static uint32_t ms;
static KCursorVisual cur;
/* insert 1's parameter (K_WP_MIX: its MIX) taken where its widget is and turned: 1, or 0 when it is not in the knobs' window */
static int turn_one(KApp *a, const KProfile *p, int param)
{
    const int item = fx_item_for(a, 0, param), st = param == K_WP_MIX ? 3 : k_fx_store_param(0, param);
    const int cw = ui_draw_canvas()->w, ch = ui_draw_canvas()->h, d = a->params[0][K_PAGE_FX][st] >= 90 ? -1 : 1;
    KVal was[K_FX_ITEMS];
    int nd, x, y;
    KRect r;
    assert(item >= 0);
    if ((nd = node_of(a, item)) < 0) return 0;
    r = a->focus[nd].bounds;
    assert(r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x + r.w <= cw && r.y + r.h <= ch);
    x = r.x + r.w / 2; y = r.y + r.h / 2;
    assert(k_hit_focus(a, x, y) == nd);   /* (the widget's cell is the node a touch finds) */
    memcpy(was, a->params[0][K_PAGE_FX], sizeof was);
    if (p->input & K_IN_KNOBS) k_dispatch(a, (KAction){K_ACT_PARAM_DELTA, item - a->win, 2 * d, ms += 100});   /* the knob that holds it */
    else if (k_profile_cursor(p)) {   /* the cursor taken there (a tap), then the value turned */
        ui_tap(a, x, y, ms += 100);
        assert(a->focus[a->focused].kind == K_NODE_PARAM && a->focus[a->focused].index == item);
        k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, d, 0, ms += 100});
    } else ui_wheel(a, x, y, d, ms += 100);   /* a pointer: the wheel over it */
    same_but(was, a->params[0][K_PAGE_FX], st);
    ui_draw(a, &cur, ms += 16); ui_draw(a, &cur, ms += 16);
    return 1;
}
static void check_fx(const char *prof)
{
    static const int P[5] = {0, 1, 2, 3, K_WP_MIX};
    const KProfile *p = k_profile_find(prof);
    const KPlugin *tp = k_effect_at(tilt);
    KApp a;
    int k, set = 0, tries;
    assert(p && ui_draw_init(p, 0));
    k_init(&a); k_set_shown(&a, p->shown); k_set_layout(&a, k_profile_layout(p));
    a.params[0][K_PAGE_FX][0] = tilt;
    for (k = 0; k < 4; k++) a.params[0][K_PAGE_FX][k_fx_store_param(0, k)] = tp->desc->edit[k].def;
    a.params[0][K_PAGE_FX][4] = 1;   /* (DIST: a built-in, its view as it was) */
    k_open_page(&a, K_PAGE_FX);
    assert(a.page == K_PAGE_FX);
    ms = 5000;
    ui_draw(&a, &cur, ms); ui_draw(&a, &cur, ms += 16);
    dump(prof, "tilt_fx");
    for (k = 0; k < 5; k++) set += turn_one(&a, p, P[k]);
    if (!strcmp(prof, "mac")) assert(set == 5);   /* (a rack: every control a target) */
    else assert(set >= 3);   /* (the knobs' row: TILT DRV HOLD MIX, or the effect's name, TILT DRV MIX) */
    for (tries = 0; tries < 12 && node_of(&a, fx_item_for(&a, 0, 3)) < 0; tries++) {   /* the row of extras: LVL there (SELECT; a cursor's right past the row) */
        k_dispatch(&a, (KAction){(p->input & K_IN_KNOBS) ? K_ACT_SELECT_DELTA : K_ACT_RIGHT, 1, 0, ms += 100});
        ui_draw(&a, &cur, ms += 16); ui_draw(&a, &cur, ms += 16);
    }
    dump(prof, "tilt_fx_lvl");
    assert(turn_one(&a, p, 3));
    printf("fx layout on %s: %d widgets taken and turned, LVL on its row\n", prof, set);
    ui_draw_shutdown();
}

int main(void)
{
    static const char *const P[] = {"fm1", "zero", "handheld", "h700", "mac"};
    static const KDeviceCaps desk = {44100u, 1u << 20, 2000u, K_AUDIO_VOICES, K_NEED_FLOAT};
    int fl, fm, k, kit;
    k_plugin_set_caps(&desk);
    assert(k_plugin_load_dir(KREISEL_PLUGIN_DIR) == 3);   /* FM2, FLAT, TILT */
    fl = k_engine_find(K_UID('f', 'l', 'a', 't'));
    fm = k_engine_find(K_UID('k', 'f', 'm', '2'));
    assert(fl >= K_ENGINE_COUNT && fm >= K_ENGINE_COUNT);
    {   /* a synth that is a drum (FLAT's sound, eight lanes): its SOUND takes its DRUM layout (four knobs), not its SOUND one (a label) */
        static const char *const LN[K_LANES] = {"BD", "SD", "CH", "OH", "CP", "LT", "HT", "RS"};
        static const KWidget DW[] = {{K_W_KNOB, 0, 0, 0, 0, 1, 1, 0}, {K_W_KNOB, 0, 1, 1, 0, 1, 1, 0}, {K_W_KNOB, 0, 2, 2, 0, 1, 1, 0},
                                     {K_W_KNOB, 0, 3, 3, 0, 1, 1, 0}, {K_W_ENV, K_ST_FRAME, 4, 0, 1, 4, 1, 0}};
        static const KWidget SW[] = {{K_W_LABEL, 0, -1, 0, 0, 1, 1, "no"}};
        static const KLayout DL[] = {{K_SCR_ANY, K_LPAGE_SOUND, 1, 1, 1, SW}, {K_SCR_ANY, K_LPAGE_DRUM, 4, 2, 5, DW}};
        static KPluginExt dx;
        static KEngine dd;
        static KPlugin dp;
        dd = *k_engine_at(fl); dd.name = "KIT"; dd.uid = K_UID('k', 'i', 't', '1'); dd.drum = 1;
        for (k = 0; k < K_LANES; k++) dd.lanes[k] = LN[k];
        dx = *k_plugin_at(fl)->ext; dx.layouts = DL; dx.nlayouts = 2;
        dp = *k_plugin_at(fl); dp.desc = &dd; dp.ext = &dx;
        kit = k_plugin_register(&dp);
        assert(kit >= K_ENGINE_COUNT && k_plugin_layout(kit, K_LPAGE_DRUM, K_SCR_SQUARE) == &DL[1]);
    }
    for (k = 0; k < 5; k++) {
        check(P[k], kit, K_PAGE_SOUND);
        check(P[k], fl, K_PAGE_SOUND);
        check(P[k], fl, K_PAGE_EDIT);
        check(P[k], fm, K_PAGE_SOUND);
    }
    {   /* the host told FLAT the screen (the last one drawn): a kind, not a name */
        const KProfile *p = k_profile_find("fm1");
        ui_draw_init(p, 0);
        assert(k_host_api()->screen == K_SCR_SQUARE && k_host_api()->knobs == 4 && (k_host_api()->input & K_IN_KNOBS));
        ui_draw_shutdown();
    }
    tilt = k_effect_find(K_UID('t', 'i', 'l', 't'));
    assert(tilt > (int)K_FX_BUILTINS);
    for (k = 0; k < 5; k++) check_fx(P[k]);
    {   /* the FM-1's rules: it fits its room, no float; and it sounds (a sine through it at its defaults, tilted, held) */
        static const KDeviceCaps fm1 = {44100u, 16u * 1024u, 120u, 6u, 0u};
        const KPlugin *tp = k_effect_at(tilt);
        const KEffectDSP *d = (const KEffectDSP *)tp->dsp;
        static uint8_t st[256];
        int32_t buf[32];
        int64_t e = 0;
        int i, blk;
        assert(!(tp->needs & K_NEED_FLOAT) && tp->ram && k_plugin_fits(tp, &fm1, 0) == 0 && d->state_size <= sizeof st && !d->mem_size(44100));
        d->init(st, 0, 44100);
        for (k = 0; k < 4; k++) d->set_param(st, (unsigned)k, tp->desc->edit[k].def * 32767 / 100);
        d->set_param(st, 0, 32767); d->set_param(st, 2, 16384);
        for (blk = 0; blk < 40; blk++) {
            for (i = 0; i < 32; i++) buf[i] = (int32_t)((((blk * 32 + i) * 2000) & 0xFFFF) - 32768) / 2;   /* (a saw) */
            d->process(st, buf, buf, 32);
            for (i = 0; i < 32; i++) { assert(buf[i] > -65536 && buf[i] < 65536); e += (int64_t)buf[i] * buf[i]; }
        }
        assert(e > 0);
    }
    puts("kreisel plugin ui test: OK");
    return 0;
}
