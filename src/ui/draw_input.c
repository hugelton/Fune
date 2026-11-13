/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Input: a tap, a touch's drag, a mouse's wheel and hover, the popup (a right click's menu, a list to pick from) and a
 * pad's (and a desktop's keys') walk over the page's targets. */
#include "draw_int.h"

/* a tap (touch, a mouse click) at x, y of the screen, as every platform sends it: on the overlays their rows, tabs and actions;
 * on a page the header's page name (the page menu) or a focus target */
static void tap_act(KApp *a, KActionType t, int x, int y, uint32_t ms) { k_dispatch(a, (KAction){t, x, y, ms}); }
struct cur_touch_s cur_touch;   /* (its fields: draw_int.h) */
static void touch_hides_cursor(const KApp *a, uint32_t ms) { if (!cur_touch.hide) cur_touch.ms = ms; cur_touch.hide = 1; cur_touch.seen = a->act_n; }
static void tap_body(KApp *a, int x, int y, uint32_t ms);
void ui_tap(KApp *a, int x, int y, uint32_t ms) { if (!roll_touch(a, -1, x, y, ms)) tap_body(a, x, y, ms); touch_hides_cursor(a, ms); }   /* (-1: a tap alone, as the web sends it) */
static void tap_body(KApp *a, int x, int y, uint32_t ms)
{
    int i;
    if (a->name_open) {   /* NAME: a letter, the cursor there (the one in hand: the next letter); Cancel, Save */
        for (i = 0; i < K_NAME_LEN; i++)
            if (inside(name_box(i), x, y)) {
                if (i == a->name_pos) tap_act(a, K_ACT_VALUE_DELTA, 1, 0, ms); else tap_act(a, K_ACT_POINTER, -1, i, ms);
                return;
            }
        if (name_btn(0).w && inside(name_btn(0), x, y)) { tap_act(a, K_ACT_BACK, 0, 0, ms); return; }
        if (name_btn(1).w && inside(name_btn(1), x, y)) { tap_act(a, K_ACT_ACCEPT, 0, 0, ms); return; }
        return;
    }
    if (a->save_open && !a->name_open) {
        const int kind = a->save_open - 1;
        for (i = 0; i < 2; i++) if (inside(save_tabs[i], x, y)) { if (i != kind) tap_act(a, K_ACT_LEFT, 0, 0, ms); return; }
        for (i = 0; i < save_rows_n; i++)
            if (inside(save_rows[i], x, y)) { tap_act(a, K_ACT_POINTER, -1, save_row_slot[i], ms); return; }
        for (i = 0; i < 4; i++)
            if (inside(save_cards[i], x, y)) {   /* an action: armed by the first tap, done by the second */
                tap_act(a, K_ACT_POINTER, -2, i + 1, ms);   /* (kreisel_save.c: armed, then done) */
                return;
            }
        return;   /* (inside the overlay, nothing there: nothing) */
    }
    if (a->settings_open) {
        for (i = 0; i < k_setting_tabs(a); i++)
            if (inside(set_tabs[i], x, y)) {
                const int d = i - k_setting_tab(a->settings_sel);
                if (d) tap_act(a, K_ACT_TYPE_DELTA, d, 0, ms);
                return;
            }
        i = ui_hit_menu_index(a, x, y);
        if (i >= 0) tap_act(a, K_ACT_POINTER, -1, i, ms);
        else if (!inside(set_box, x, y)) tap_act(a, K_ACT_BACK, 0, 0, ms);   /* outside the box: it closes */
        return;
    }
    if (header_tools.w && inside((KRect){header_tools.x - 4, 0, header_tools.w + 8, HEAD_H}, x, y) && !a->settings_open && !a->menu_open && !a->save_open && !a->save_ask) {
        tap_act(a, K_ACT_TOOLS, 0, 0, ms);   /* the header's TOOLS: open, or (open) close */
        return;
    }
    if ((prof->input & K_IN_TOUCH) && y < HEAD_H && !a->settings_open && !a->menu_open && !a->save_open && !a->save_ask)
        for (i = 0; i < hdr_tn; i++)   /* a touch screen: the header's play and track number are its buttons (a pad's cursor stands there) */
            if (inside((KRect){hdr_tgt[i].x - 4, 0, hdr_tgt[i].w + 8, HEAD_H}, x, y) && (hdr_kind[i] == HT_PLAY || hdr_kind[i] == HT_TRACK)) {
                if (hdr_kind[i] == HT_PLAY) tap_act(a, K_ACT_PLAY, 0, 0, ms); else tap_act(a, K_ACT_TRACK_DELTA, 1, 0, ms);
                return;
            }
    if (!a->settings_open && !a->menu_open && !a->save_open && !a->save_ask)
        for (i = 0; i < 4; i++) if (pat_chip[i].w && inside(pat_chip[i], x, y)) {   /* the rail's patterns: that one (the tools let go first) */
            int d = i - (a->pattern[a->track] & 3);
            if (a->tools_open) tap_act(a, K_ACT_BACK, 0, 0, ms);
            for (; d; d += d > 0 ? -1 : 1) tap_act(a, K_ACT_TYPE_DELTA, d > 0 ? 1 : -1, 0, ms);
            return;
        }
    if (a->tools_open && !a->settings_open && !a->menu_open && (prof->input & K_IN_MOUSE)) {   /* a desktop: the open row's buttons; its sliders are dragged */
        if (tool_btn[0].w && inside(tool_btn[0], x, y)) { tap_act(a, K_ACT_BACK, 0, 0, ms); return; }
        if (tool_btn[1].w && inside(tool_btn[1], x, y)) { tap_act(a, K_ACT_ACCEPT, 0, 0, ms); return; }
        if ((tool_slider[0].w && inside(tool_slider[0], x, y)) || (tool_slider[1].w && inside(tool_slider[1], x, y))) return;
        i = ui_hit_menu_index(a, x, y);
        if (i == a->tools_sel) return;   /* (its name: Apply does it, not a second click) */
        if (i < 0 && inside(tools_box, x, y)) return;   /* (the open row's room) */
    }
    if (a->tools_open && !a->settings_open && !a->menu_open) {   /* a row: in hand, then (again) done; off the list: it closes */
        i = ui_hit_menu_index(a, x, y);
        if (i >= 0) tap_act(a, K_ACT_POINTER, -1, i, ms);
        else if (!inside(tools_box, x, y)) tap_act(a, K_ACT_BACK, 0, 0, ms);
        return;
    }
    if (a->menu_open) {
        if (menu_config.w && inside(menu_config, x, y)) { tap_act(a, K_ACT_SETTINGS, 0, 0, ms); return; }   /* (it closes the menu) */
        i = ui_hit_menu_index(a, x, y);
        tap_act(a, i >= 0 ? K_ACT_POINTER : K_ACT_BACK, -1, i, ms);
        return;
    }
    if ((a->lay & K_LAY_SYNTH_ALL) && a->page == K_PAGE_SOUND && k_is_drum(a, a->track))   /* the Switch's DRUM: a lane's tab takes it */
        for (i = 0; i < K_LANES; i++)
            if (inside(sw_lane_tabs[i], x, y)) { tap_act(a, K_ACT_LANE_SELECT, i, 0, ms); return; }   /* (the pattern under them: a picture, no target) */
    if (no_cards(a) && a->page == K_PAGE_MIXER)   /* a strip of another track: that track first (its controls are then the targets) */
        for (i = 0; i < K_TRACKS; i++)
            if (i != a->track && inside(mix_strips[i], x, y)) { tap_act(a, K_ACT_TRACK_DELTA, i - a->track, 0, ms); return; }
    if (a->page == K_PAGE_FX) {   /* an insert's < or >: the effect before or after */
        int sl, d2;
        for (sl = 0; sl < K_FX_SLOTS; sl++) for (d2 = 0; d2 < 2; d2++)
            if (fx_arrows[sl][d2].w && inside(fx_arrows[sl][d2], x, y)) { k_fx_type_step(a, sl, d2 ? 1 : -1, 1); return; }   /* (round: NONE after the last) */
    }
    {   /* the engine's bar: its left half the engine before ("<"), its right half the one after (">") */
        const int hn = ui_hit_header(x, y) ? -1 : k_hit_focus(a, x, y);
        if (hn >= 0 && a->focus[hn].kind == K_NODE_SOUND) {
            const KRect r = a->focus[hn].bounds;
            a->focused = hn;
            tap_act(a, K_ACT_VALUE_DELTA, x < r.x + r.w / 2 ? -1 : 1, 0, ms);
            return;
        }
    }
    if (rail.w && inside(rail, x, y)) {   /* the tools' column, not in use: a row opens it with that row in hand (a second tap does it) */
        for (i = 0; i < menu_rows_n; i++)
            if (inside(menu_rows[i], x, y)) { tap_act(a, K_ACT_TOOLS, 0, 0, ms); if (i != a->tools_sel) tap_act(a, K_ACT_POINTER, -1, i, ms); return; }
        return;
    }
    if (ui_hit_header(x, y)) tap_act(a, K_ACT_PAGE_MENU, 0, 0, ms);   /* touching the page name opens the page list */
    else tap_act(a, K_ACT_POINTER, x, y, ms);
}

/* a touch that moves: down taps (ui_tap); then, when the finger landed on a value (a card, a knob, a fader, a song cell, the
 * engine's bar), sliding it up turns the value up, down turns it down: a step every 4 px of the canvas (8 physical px on a
 * Switch, whose 1280 x 720 shows 640 x 360 twice). phase 0 down, 1 moved, 2 up */
static struct { int on, drag, inv, y, x, horiz, ctl; } touch;   /* horiz: a card (wide, low) slides sideways, as its gauge runs */
/* a mouse's wheel: the value under the pointer turns (a card, a knob, a fader, a song cell, the engine's bar), the tools' row under it
 * (taken first) its first value; over ROLL's grid the rows scroll; a list open (the page menu, the settings): its rows */
void ui_wheel(KApp *a, int x, int y, int d, uint32_t ms)
{
    int hit;
    if (!d) return;
    if (header_master.w && inside(header_master, x, y)) { k_dispatch(a, (KAction){K_ACT_MASTER_DELTA, 2 * d, 0, ms}); return; }   /* the header's level knob, whatever is open */
    if (a->menu_open || a->settings_open || a->save_open || a->save_ask || a->name_open) { k_dispatch(a, (KAction){d > 0 ? K_ACT_FOCUS_PREV : K_ACT_FOCUS_NEXT, 0, 0, ms}); return; }
    if (a->tools_open && (prof->input & K_IN_MOUSE)) {   /* the open tool's slider: its value */
        int k;
        for (k = 0; k < 2; k++)
            if (tool_slider[k].w && inside(tool_slider[k], x, y)) { k_dispatch(a, (KAction){k ? K_ACT_TYPE_DELTA : K_ACT_VALUE_DELTA, d, 0, ms}); return; }
    }
    if (a->tools_open || (rail.w && inside(rail, x, y))) {
        int i, k;
        for (i = -1, k = 0; k < menu_rows_n; k++) if (inside(menu_rows[k], x, y)) i = k;   /* (the rail's rows, open or not) */
        if (i < 0 && !a->tools_open) return;
        if (!a->tools_open) k_dispatch(a, (KAction){K_ACT_TOOLS, 0, 0, ms});
        if (i >= 0 && i != a->tools_sel) k_dispatch(a, (KAction){K_ACT_POINTER, -1, i, ms});
        if (i >= 0 || inside(tools_box, x, y)) k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, d, 0, ms});
        return;
    }
    if (ui_hit_header(x, y)) return;
    hit = k_hit_focus(a, x, y);
    if (hit >= 0 && (a->focus[hit].kind == K_NODE_PARAM || a->focus[hit].kind == K_NODE_SCENE || a->focus[hit].kind == K_NODE_SOUND)) {
        a->focused = hit;
        k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, d, 0, ms});
        touch_hides_cursor(a, ms);
    } else if (a->page == K_PAGE_ROLL && !k_is_drum(a, a->track)) ui_roll_scroll(a, d);
}
/* ---- the popup (a desktop): a right click's menu (the value under the pointer: back to its default, copied, pasted; the page's
 * tools), or a list to pick from (an insert's effect). A left click on a row does it; anywhere else, Esc or a right click closes ---- */
static int hov_x = -1, hov_y = -1, hov_down;   /* the pointer (ui_hover) */
struct pad_s pad;   /* (its fields: draw_int.h) */
static int pad_on;   /* the last input was a pad's (or the keys'): its row lit in a list, the pointer's not */
enum { CA_SEP, CA_RESET, CA_COPY, CA_PASTE, CA_TOOL, CA_FX, CA_FACTORY, CA_USER, CA_HEAD, CA_EDIT, CA_TRACK, CA_BPM, CA_CLOCK, CA_SWING, CA_PAGE };
struct ctx_s ctx;   /* (its fields: draw_int.h) */
int ui_edit(KApp *a, int op, uint32_t ms);   /* a value copied (any value: pasted within the range of the one it goes to) */
static void ctx_add(const char *label, int act, int arg, int on)
{
    if (ctx.n >= CTX_ROWS) return;
    snprintf(ctx.it[ctx.n].label, sizeof ctx.it[ctx.n].label, "%s", label);
    ctx.it[ctx.n].act = act; ctx.it[ctx.n].arg = arg; ctx.it[ctx.n].on = on; ctx.it[ctx.n].icon = -1;
    ctx.n++;
}
static void ctx_fx_list(KApp *a, int slot)   /* the effects, the one in the slot marked */
{
    const int cur = a->params[a->track][K_PAGE_FX][slot * 4];
    int e;
    ctx.slot = slot;
    for (e = 0; e <= k_effect_count(); e++) { ctx_add(fx_name(e), CA_FX, e, e == cur); ctx.it[ctx.n - 1].icon = fx_icon(e); }
}
static void ctx_preset_list(KApp *a)   /* the track's engine's factory presets, then the user presets (any engine: it goes with them) */
{
    const int t = a->track, nf = k_track_missing(a, t) ? 0 : k_factory_count(k_engine_uid(a->sound[t]));
    int i, any = 0;
    ctx_add("Factory", CA_HEAD, 0, 0);
    for (i = 0; i < nf; i++) ctx_add(k_factory_name(k_engine_uid(a->sound[t]), i), CA_FACTORY, i, a->factory[t] == i);
    for (i = 0; i < K_USER_PRESETS; i++) {
        if (!k_save_used(K_STORE_PRESET, i)) continue;
        if (!any) { ctx_add("", CA_SEP, 0, 0); ctx_add("User", CA_HEAD, 0, 0); any = 1; }
        {   char nm[24]; const char *s2 = k_save_name(K_STORE_PRESET, i);
            snprintf(nm, sizeof nm, "%s", s2 && s2[0] ? s2 : "(no name)");
            ctx_add(nm, CA_USER, i, a->preset_slot[t] == i); }
    }
}
/* a desktop's header, clicked (as the preset's name): the tracks to pick; the tempo; the clock (its source, the swing) */
static void ctx_track_list(KApp *a)
{
    int t;
    for (t = 0; t < K_TRACKS; t++) {
        char nm[32];
        snprintf(nm, sizeof nm, "%d  %s", t + 1, k_track_missing(a, t) ? "-" : k_engine_at(a->sound[t])->name);
        ctx_add(nm, CA_TRACK, t, t == a->track);
    }
}
static void ctx_tempo_list(KApp *a)
{
    static const int BPM[] = {70, 80, 90, 100, 110, 120, 124, 128, 135, 140, 160, 174};
    int i;
    char nm[16];
    snprintf(nm, sizeof nm, "-1  (%d)", a->bpm - 1); ctx_add(nm, CA_BPM, a->bpm - 1, 0);
    snprintf(nm, sizeof nm, "+1  (%d)", a->bpm + 1); ctx_add(nm, CA_BPM, a->bpm + 1, 0);
    ctx_add("", CA_SEP, 0, 0);
    for (i = 0; i < (int)(sizeof BPM / sizeof BPM[0]); i++) { snprintf(nm, sizeof nm, "%d", BPM[i]); ctx_add(nm, CA_BPM, BPM[i], BPM[i] == a->bpm); }
}
static void ctx_clock_list(KApp *a)
{
    const KParamDesc *d = &k_page_desc(K_PAGE_CLOCK)[2];
    const int src = k_param_get(a, K_PAGE_CLOCK, 2), sw = k_param_get(a, K_PAGE_CLOCK, 1);
    int i;
    ctx_add("Clock", CA_HEAD, 0, 0);
    for (i = d->min; i <= d->max && d->names && d->names[i]; i++) ctx_add(d->names[i], CA_CLOCK, i, i == src);
    (void)sw;   /* (the swing: in the tempo's balloon) */
}
static void ctx_page_list(KApp *a)   /* a desktop's page icon: the pages, as a list (a dropdown) */
{
    const uint8_t g = a->menu_group;   /* (every page, as the switcher's ALL; the menu's own group put back) */
    int i, n;
    a->menu_group = K_GROUP_ALL;
    n = k_menu_count(a);
    for (i = 0; i < n; i++) { const KPage q = k_menu_page(a, i); ctx_add(k_page_name(q), CA_PAGE, (int)q, q == a->page); }
    a->menu_group = g;
}
/* a desktop's tempo, clicked: a balloon with controls in it: the tempo large, its slider, the swing's under it */
static struct { int open, drag; KRect box, sl[2]; } tp;
int ui_tempo_open(void) { return tp.open; }
static void tp_set(KApp *a, int which, int x)
{
    const KRect r = tp.sl[which];
    const int lo = which ? 0 : 40, hi = which ? 100 : 240, v = lo + (x - r.x) * (hi - lo) / (r.w > 1 ? r.w - 1 : 1);
    k_param_set(a, K_PAGE_CLOCK, which ? 1 : 0, v < lo ? lo : v > hi ? hi : v);
}
static void tp_slider(KRect r, int v, int lo, int hi)
{
    const int f = (v - lo) * r.w / (hi - lo), cy = r.y + r.h / 2;
    g_rrect(&cv, r.x, cy - 2, r.w, 4, 2, k_mix(th.bg, th.text, 18));
    if (f > 0) g_rrect(&cv, r.x, cy - 2, f, 4, 2, th.accent);
    g_rrect(&cv, r.x + f - 7, cy - 7, 14, 14, 7, th.text);
}
void draw_tempo_pop(const KApp *a)
{
    char b[16];
    const KRect bx = tp.box;
    if (!tp.open) return;
    g_rrect_frame(&cv, bx.x, bx.y, bx.w, bx.h, 8, k_mix(th.bg, th.text, 20), th.surf);
    snprintf(b, sizeof b, "%d.0", a->bpm);
    g_text_box(&cv, bx.x, bx.y + 6, bx.w, g_text_h(GF_L), GF_L, b, th.text, 0);
    tp_slider(tp.sl[0], a->bpm, 40, 240);
    snprintf(b, sizeof b, "Swing %d%%", k_param_get(a, K_PAGE_CLOCK, 1));
    g_text_box(&cv, bx.x + 12, tp.sl[1].y - 18, bx.w - 24, 14, GF_S, b, th.mid, -1);
    tp_slider(tp.sl[1], k_param_get(a, K_PAGE_CLOCK, 1), 0, 100);
}
static void tp_show(int x)
{
    const int w = 220, h = 132;
    tp.open = 1; tp.drag = -1;
    tp.box = (KRect){(int16_t)(x + w > cv.w - 4 ? cv.w - 4 - w : x), (int16_t)(HEAD_H + 2), (int16_t)w, (int16_t)h};
    tp.sl[0] = (KRect){(int16_t)(tp.box.x + 16), (int16_t)(tp.box.y + 50), (int16_t)(w - 32), 20};
    tp.sl[1] = (KRect){(int16_t)(tp.box.x + 16), (int16_t)(tp.box.y + 100), (int16_t)(w - 32), 20};
}
int ui_popup_open(void) { return ctx.open; }
void ui_popup_close(void) { ctx.open = 0; }
int ui_popup_rows(KRect *out, int max) { int i; for (i = 0; i < ctx.n && i < max; i++) out[i] = ctx.rows[i]; return ctx.open ? ctx.n : 0; }   /* (as last drawn: tests) */
static void ctx_show(int x, int y)
{
    int i;
    ctx.open = ctx.n > 0; ctx.x = x; ctx.y = y; ctx.psel = -1;
    for (i = 0; i < ctx.n; i++) if (ctx.it[i].act != CA_SEP && ctx.it[i].act != CA_HEAD) { ctx.psel = i; break; }
    for (i = 0; i < ctx.n; i++) if (ctx.it[i].on) { ctx.psel = i; break; }   /* (a list: from the one in use) */
}
/* Copy, Paste, Clear (a desktop's Cmd+C, Cmd+V, Delete; the menu's): SONG's selected column or row, DRUM's lane selected by its name,
 * else the value in hand (Clear: back to its default); 1 when something was done */
int ui_edit(KApp *a, int op, uint32_t ms)
{
    if (a->menu_open || a->settings_open || a->save_open || a->save_ask || a->name_open || a->tools_open) return 0;
    if (op != 0 && k_selected(a, K_SEL_SCENE) < 0 && k_selected(a, K_SEL_ROW) < 0 && k_selected(a, K_SEL_LANE) < 0 &&
        a->focus_count && a->focus[a->focused].kind == K_NODE_PARAM) {   /* the value in hand, pasted or cleared: its card opens */
        const int i = a->focus[a->focused].index;
        if (i < 64) { a->touched |= (uint64_t)1 << i; a->touch_ms[i] = ms; }
    }
    return k_edit(a, op);   /* (the core's: what is selected, else the value in hand) */
}
/* a right click at (x, y): what is under it decides the rows; 1 when the menu opened */
int ui_context(KApp *a, int x, int y, uint32_t ms)
{
    int hit, t, c;
    (void)ms;
    ctx.n = 0; ctx.open = 0; ctx.item = -1; ctx.page = a->page;
    if (a->menu_open || a->settings_open || a->save_open || a->save_ask || a->name_open || a->tools_open || y < HEAD_H) return 0;   /* (the header: none) */
    if (a->page == K_PAGE_SONG || a->page == K_PAGE_DRUM) {   /* a scene's number, a track's chip, a lane's name: selected, and its menu */
        int kind = 0, idx = 0;
        if (a->page == K_PAGE_SONG) {
            for (c = 0; c < K_SONG_VIS; c++) if (song_col_hdr[c].w && inside(song_col_hdr[c], x, y)) { kind = 1; idx = a->song_off + c; }
            for (t = 0; t < K_TRACKS; t++) if (song_row_hdr[t].w && inside(song_row_hdr[t], x, y)) { kind = 2; idx = t; }
            if (kind) k_select(a, kind == 1 ? K_SEL_SCENE : K_SEL_ROW, idx);
        } else for (c = 0; c < K_LANES; c++) if (lane_lab[c].w && inside(lane_lab[c], x, y)) { kind = 3; k_dispatch(a, (KAction){K_ACT_LANE_SELECT, c, 0, ms}); k_select(a, K_SEL_LANE, c); }
        if (kind) {
            const char *what = kind == 1 ? "scene" : kind == 2 ? "track" : "lane";
            char l1[24], l2[24], l3[24];
            snprintf(l1, sizeof l1, "Copy %s", what); snprintf(l2, sizeof l2, "Paste %s", what); snprintf(l3, sizeof l3, "Clear %s", what);
            ctx_add(l1, CA_EDIT, 0, 0); ctx_add(l2, CA_EDIT, 1, 0); ctx_add(l3, CA_EDIT, 2, 0);
            ctx_show(x, y); return ctx.open;
        }
    }
    if (a->page == K_PAGE_MIXER && mix_ctl_at == now) {
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 2; c++)
            if (mix_ins[t][c].w && inside(mix_ins[t][c], x, y)) {
                if (t != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
                ctx_fx_list(a, c); ctx_show(x, y); return ctx.open;
            }
        for (t = 0; t < K_TRACKS && ctx.item < 0; t++) for (c = 0; c < 4; c++) {
            const KRect r = c ? mix_ctl[t][c] : (KRect){mix_ctl[t][0].x, (int16_t)(mix_ctl[t][0].y - 12), mix_ctl[t][0].w, (int16_t)(mix_ctl[t][0].h + 24)};
            if (mix_ctl[t][c].w && inside(r, x, y)) {
                if (t != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
                ctx.item = c; break;
            }
        }
    }
    if (ctx.item < 0) {
        hit = k_hit_focus(a, x, y);
        if (hit >= 0 && a->focus[hit].kind == K_NODE_PARAM) {
            ctx.item = a->focus[hit].index;
            a->focused = hit;
            if (a->page == K_PAGE_FX) {   /* an insert's name: its effects */
                int slot = 0, param = -3;
                k_fx_item(a, ctx.item, &slot, &param);
                if (param == -1) { ctx_fx_list(a, slot); ctx_show(x, y); return ctx.open; }
            }
        }
    }
    ctx.track = a->track;
    if (ctx.item >= 0) {
        ctx_add("Reset to default", CA_RESET, 0, 0);
        ctx_add("Copy value", CA_COPY, 0, 0);
        ctx_add("Paste value", CA_PASTE, 0, 0);
    }
    /* (the page's tools are not here: a right click is about the thing under the pointer, the tools about the page) */
    ctx_show(x, y);
    return ctx.open;
}
/* a left click while it is open: a row done, or (anywhere else) closed; 1 always (the click is the popup's) */
int ui_popup_click(KApp *a, int x, int y, uint32_t ms)
{
    int i;
    for (i = 0; i < ctx.n; i++) {
        if (ctx.it[i].act == CA_SEP || !inside(ctx.rows[i], x, y)) continue;
        switch (ctx.it[i].act) {
        case CA_RESET: k_param_reset(a, (KPage)ctx.page, ctx.item); break;
        case CA_COPY: k_value_copy(a, (KPage)ctx.page, ctx.item); break;
        case CA_PASTE: k_value_paste(a, (KPage)ctx.page, ctx.item); break;
        case CA_TOOL: { const KTool t = (KTool)ctx.it[i].arg; k_seq_tool(a, t, a->tool_x[t], a->tool_y[t]); } break;
        case CA_FX: k_fx_type_set(a, ctx.slot, ctx.it[i].arg); break;
        case CA_FACTORY: if (k_factory_apply(a, a->track, ctx.it[i].arg) == 0) k_balloon_show(&a->balloon, K_BALLOON_PRESET, ms); break;
        case CA_USER: k_save_preset_load(a, ctx.it[i].arg); break;
        case CA_HEAD: continue;   /* (a heading: not a row to pick) */
        case CA_EDIT: ui_edit(a, ctx.it[i].arg, ms); break;
        case CA_TRACK: if (ctx.it[i].arg != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, ctx.it[i].arg - a->track, 0, ms}); break;
        case CA_BPM: k_param_set(a, K_PAGE_CLOCK, 0, ctx.it[i].arg); break;
        case CA_CLOCK: k_param_set(a, K_PAGE_CLOCK, 2, ctx.it[i].arg); break;
        case CA_SWING: k_param_set(a, K_PAGE_CLOCK, 1, ctx.it[i].arg); break;
        case CA_PAGE: k_open_page(a, (KPage)ctx.it[i].arg); break;
        default: break;
        }
        if (ctx.item >= 0 && ctx.item < 64 && ctx.it[i].act != CA_TOOL && ctx.it[i].act != CA_FX && ctx.it[i].act != CA_EDIT && ctx.it[i].act < CA_TRACK) { a->touched |= (uint64_t)1 << ctx.item; a->touch_ms[ctx.item] = ms; }
        break;
    }
    ctx.open = 0;
    return 1;
}
void draw_popup(void)
{
    const int pad = 4, rh = g_text_h(GF_S) + (ctx.n > 10 ? 3 : 8), sh = 7, maxh = cv.h - 8;
    int i, w = 120, h = 2 * pad, x, y, cols = 1, col_h = 2 * pad, top;
    if (!ctx.open) return;
    for (i = 0; i < ctx.n; i++) {   /* (a long list: in columns, each as tall as the screen allows) */
        const int tw = g_text_w(GF_S, ctx.it[i].label) + 34 + (ctx.it[i].act == CA_FX ? 20 : 0), rr = ctx.it[i].act == CA_SEP ? sh : rh;
        if (tw > w) w = tw;
        if (col_h + rr > maxh) { cols++; col_h = 2 * pad; }
        col_h += rr;
        if (col_h > h) h = col_h;
    }
    x = ctx.x + 2; y = ctx.y + 2;
    if (x + w * cols > cv.w - 4) x = ctx.x - w * cols - 2;   /* (to the left of the pointer when there is no room at its right) */
    if (x < 4) x = 4;
    if (y + h > cv.h - 4) y = cv.h - 4 - h;
    if (y < 4) y = 4;
    ctx.box = (KRect){(int16_t)x, (int16_t)y, (int16_t)(w * cols), (int16_t)h};
    g_shadow(&cv, x, y, w * cols, h, 7, 0, 3, 5, 0x000000, 50);
    g_rrect_frame(&cv, x, y, w * cols, h, 7, k_mix(th.surf, th.text, 22), k_mix(th.surf, th.bg, 10));
    top = y; y += pad;
    for (i = 0; i < ctx.n; i++) {
        const int rr = ctx.it[i].act == CA_SEP ? sh : rh;
        if (y + rr > top + h - pad + 1) { x += w; y = top + pad; }   /* the next column */
        if (ctx.it[i].act == CA_SEP) { g_fill(&cv, x + 8, y + sh / 2, w - 16, 1, k_mix(th.surf, th.text, 16)); ctx.rows[i] = (KRect){0, 0, 0, 0}; y += sh; continue; }
        if (ctx.it[i].act == CA_HEAD) { g_text(&cv, x + 10, y + (rh - g_text_h(GF_S)) / 2, GF_S, ctx.it[i].label, th.mid); ctx.rows[i] = (KRect){0, 0, 0, 0}; y += rh; continue; }
        ctx.rows[i] = (KRect){(int16_t)(x + pad), (int16_t)y, (int16_t)(w - 2 * pad), (int16_t)rh};
        {   const int over = (hov_x >= 0 && inside(ctx.rows[i], hov_x, hov_y)) || (pad_on && i == ctx.psel), dim = ctx.it[i].act == CA_PASTE && !(hue_app && hue_app->clip_kind == K_CLIP_VALUE);   /* (nothing copied yet: Paste dim) */
            if (over && !dim) flat_fill( x + pad, y, w - 2 * pad, rh, 5, th.sel);
            if (ctx.it[i].on) g_rrect(&cv, x + 10, y + rh / 2 - 3, 6, 6, 3, over ? th.ink : th.accent);   /* the one in use: a dot */
            {   int tx = x + 22;
                if (ctx.it[i].icon >= 0) { g_icon(&cv, tx, y + (rh - 14) / 2, 14, (GIcon)ctx.it[i].icon, over ? th.ink : ctx.it[i].act == CA_FX ? fx_accent(ctx.it[i].arg) : th.mid); tx += 20; }
                else if (ctx.it[i].act == CA_FX) tx += 20;   /* (NONE: under the others' names) */
                g_text(&cv, tx, y + (rh - g_text_h(GF_S)) / 2, GF_S, ctx.it[i].label, dim ? th.dim : over ? th.ink : th.text); }
        }
        y += rh;
    }
}
/* a pointer (a desktop's mouse) over something it can work: that lit faintly, so the hand knows before it clicks (ui_hover) */
static KRect hover_rect(KApp *a, int x, int y)
{
    int i, t, c;
    const KRect none = {0, 0, 0, 0};
    if (x < 0) return none;
    if (a->tools_open) { int k; for (k = 0; k < 2; k++) { if (tool_btn[k].w && inside(tool_btn[k], x, y)) return tool_btn[k]; if (tool_slider[k].w && inside(tool_slider[k], x, y)) return tool_slider[k]; } }
    if (a->menu_open || a->tools_open || (rail.w && inside(rail, x, y))) {   /* a list's row, a tile */
        for (i = 0; i < menu_rows_n; i++) if (menu_rows[i].w && inside(menu_rows[i], x, y)) return menu_rows[i];
        if (a->menu_open && menu_config.w && inside(menu_config, x, y)) return menu_config;
        return none;
    }
    if (a->settings_open || a->save_open || a->save_ask || a->name_open) return none;
    if (header_master.w && inside(header_master, x, y)) return header_master;
    if (header_tools.w && inside(header_tools, x, y)) return header_tools;
    if (header_preset.w && inside(header_preset, x, y)) return header_preset;
    if (ui_hit_header(x, y)) return (KRect){(int16_t)(header_page.x - 5), 1, (int16_t)(header_page.w + 10), (int16_t)HEAD_H};
    if (a->page == K_PAGE_FX) for (t = 0; t < K_FX_SLOTS; t++) for (c = 0; c < 2; c++) if (fx_arrows[t][c].w && inside(fx_arrows[t][c], x, y)) return fx_arrows[t][c];
    if (a->page == K_PAGE_MIXER && mix_ctl_at == now)
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 2; c++) if (mix_ins[t][c].w && inside(mix_ins[t][c], x, y)) return mix_ins[t][c];
    if (a->page == K_PAGE_MIXER && mix_ctl_at == now)
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 6; c++) {
            const KRect r = c ? mix_ctl[t][c] : (KRect){mix_ctl[t][0].x, (int16_t)(mix_ctl[t][0].y - 12), mix_ctl[t][0].w, (int16_t)(mix_ctl[t][0].h + 24)};
            if (mix_ctl[t][c].w && inside(r, x, y)) return r;
        }
    i = k_hit_focus(a, x, y);
    return i >= 0 ? a->focus[i].bounds : none;
}
void draw_hover(KApp *a)
{
    KRect r;
    if (!(prof->input & K_IN_MOUSE) || hov_down || hov_x < 0) return;
    r = hover_rect(a, hov_x, hov_y);
    if (r.w <= 0 || r.h <= 0) return;
    if (r.y + r.h <= HEAD_H + 2) { g_rrect_a(&cv, r.x, r.y, r.w, r.h, 6, th.text, 22); return; }   /* (the header's: its buttons all the bar's height, as they are) */
    g_rrect_a(&cv, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 6, th.text, 22);   /* (a veil of the text's colour: light on dark, dark on light) */
}
/* the pointer moved to (x, y) (-1: it left the window), its button down or not: 1 when what it is over changed (draw again) */
int ui_hover(KApp *a, int x, int y, int down)
{
    const KRect was = hover_rect(a, hov_x, hov_y);
    KRect now_r;
    hov_x = x; hov_y = y; hov_down = down;
    if (x >= 0) pad_on = 0;
    now_r = hover_rect(a, x, y);
    return was.x != now_r.x || was.y != now_r.y || was.w != now_r.w || was.h != now_r.h;
}
/* an option-click (a desktop): the value under the pointer back at its default (k_param_reset); a mixer strip's, whichever strip */
int ui_reset_at(KApp *a, int x, int y, uint32_t ms)
{
    int hit;
    if (a->menu_open || a->settings_open || a->tools_open || a->save_open || a->save_ask || a->name_open || ui_hit_header(x, y)) return 0;
    if (a->page == K_PAGE_MIXER && mix_ctl_at == now) {
        int t, c;
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 4; c++) {
            const KRect r = c ? mix_ctl[t][c] : (KRect){mix_ctl[t][0].x, mix_ctl[t][0].y - 12, mix_ctl[t][0].w, mix_ctl[t][0].h + 24};
            if (!mix_ctl[t][c].w || !inside(r, x, y)) continue;
            if (t != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
            k_param_reset(a, K_PAGE_MIXER, c);
            a->touched |= (uint64_t)1 << c; a->touch_ms[c] = ms;
            return 1;
        }
    }
    hit = k_hit_focus(a, x, y);
    if (hit < 0 || a->focus[hit].kind != K_NODE_PARAM) return 0;
    a->focused = hit;
    if (!k_param_reset(a, a->page, a->focus[hit].index)) return 0;
    if (a->focus[hit].index < 64) { a->touched |= (uint64_t)1 << a->focus[hit].index; a->touch_ms[a->focus[hit].index] = ms; }
    touch_hides_cursor(a, ms);
    return 1;
}
/* ORGAN's drawbar in hand (touch.ctl, its rectangle drawbar_r: ko_drawbar's bounds): pulled out to where the finger is, 0 in .. 8 */
static KRect drawbar_r;
static void slider_to(KApp *a, int k, int x)   /* the open tool's value k where the pointer is along its slider */
{
    const KTool t = k_tools_item(a, a->tools_sel);
    const KRect r = tool_slider[k];
    int lo, hi;
    if (!r.w || !k_tool_range(a, t, k, &lo, &hi)) return;
    k_tools_value(a, k, lo + ((x - r.x - 6) * (hi - lo) + (r.w - 12) / 2) / (r.w - 12 > 0 ? r.w - 12 : 1));
}
static void drawbar_to(KApp *a, int y, uint32_t ms)
{
    const int step = (drawbar_r.h - 33) / 8 > 0 ? (drawbar_r.h - 33) / 8 : 1;
    int v = (y - (drawbar_r.y + 3 + 8) + step / 2) / step;
    v = v < 0 ? 0 : v > 8 ? 8 : v;
    if (k_param_get(a, K_PAGE_SOUND, touch.ctl) != v) k_param_set(a, K_PAGE_SOUND, touch.ctl, v);   /* (the core's: kept in the drawbar's range, the preset marked changed) */
    a->touched |= (uint64_t)1 << touch.ctl; a->touch_ms[touch.ctl] = ms;
    touch_hides_cursor(a, ms);
}
void ui_touch(KApp *a, int phase, int x, int y, uint32_t ms)
{
    if (tp.open) {   /* the tempo's balloon: its sliders dragged; outside it, it closes */
        int k;
        if (phase == 0) {
            tp.drag = -1;
            for (k = 0; k < 2; k++) if (inside((KRect){tp.sl[k].x - 8, tp.sl[k].y - 6, (int16_t)(tp.sl[k].w + 16), (int16_t)(tp.sl[k].h + 12)}, x, y)) { tp.drag = k; tp_set(a, k, x); }
            if (tp.drag < 0 && !inside(tp.box, x, y)) tp.open = 0;
        } else if (phase == 1 && tp.drag >= 0) tp_set(a, tp.drag, x);
        else if (phase == 2) tp.drag = -1;
        return;
    }
    if (roll_touch(a, phase, x, y, ms)) { touch_hides_cursor(a, ms); return; }
    if (a->name_open) {   /* NAME: a letter touched takes the cursor; up or down along it, the letter changes (8 px a letter); the
                           * one in hand touched and let go unmoved, the next letter; Cancel and Save as a tap */
        int i;
        if (phase == 0) {
            touch.on = 0; touch.drag = 0; name_pending = 0;
            for (i = 0; i < K_NAME_LEN; i++)
                if (inside(name_box(i), x, y)) {
                    if (i != a->name_pos) k_dispatch(a, (KAction){K_ACT_POINTER, -1, i, ms}); else name_pending = 1;
                    touch.on = 1; touch.drag = 10; touch.y = y;
                    touch_hides_cursor(a, ms);
                    return;
                }
            ui_tap(a, x, y, ms);
            return;
        }
        if (touch.drag == 10 && touch.on && phase == 1) {
            const int steps = (touch.y - y) / 8;
            for (i = 0; i < (steps < 0 ? -steps : steps); i++) k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, steps > 0 ? 1 : -1, 0, ms});
            if (steps) { touch.y -= steps * 8; name_pending = 0; }
            return;
        }
        if (phase == 2) {
            if (touch.drag == 10 && name_pending) k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, 1, 0, ms});
            touch.on = 0; touch.drag = 0; name_pending = 0;
        }
        return;
    }
    if (phase == 0 && (prof->input & K_IN_MOUSE) && !a->menu_open && !a->settings_open && !a->tools_open && !a->save_open && !a->save_ask) {
        int c;
        if (a->page == K_PAGE_SONG) {   /* a scene's number: its column; a track's chip: its row (a second click: none); elsewhere: none */
            for (c = 0; c < K_SONG_VIS; c++) if (song_col_hdr[c].w && inside(song_col_hdr[c], x, y)) { k_select(a, k_selected(a, K_SEL_SCENE) == a->song_off + c ? K_SEL_NONE : K_SEL_SCENE, a->song_off + c); return; }
            for (c = 0; c < K_TRACKS; c++) if (song_row_hdr[c].w && inside(song_row_hdr[c], x, y)) { k_select(a, k_selected(a, K_SEL_ROW) == c ? K_SEL_NONE : K_SEL_ROW, c); return; }
        } else if (a->page == K_PAGE_DRUM) {   /* a lane's name: that lane in hand, selected (a copy, a paste take it) */
            for (c = 0; c < K_LANES; c++) if (lane_lab[c].w && inside(lane_lab[c], x, y)) { k_dispatch(a, (KAction){K_ACT_LANE_SELECT, c, 0, ms}); k_select(a, K_SEL_LANE, c); return; }
        }
        k_select(a, K_SEL_NONE, 0);   /* (elsewhere: nothing selected) */
    }
    if (phase == 0 && (prof->input & K_IN_MOUSE) && y < HEAD_H && !a->menu_open && !a->settings_open && !a->save_open && !a->save_ask) {
        int h;
        for (h = 0; h < hdr_tn; h++)   /* a desktop: the track's number, the tempo, the beat dots: each its balloon (as the preset's) */
            if ((hdr_kind[h] == HT_TRACK || hdr_kind[h] == HT_TEMPO || hdr_kind[h] == HT_CLOCK || hdr_kind[h] == HT_PAGE) &&
                inside((KRect){hdr_tgt[h].x - 4, 0, hdr_tgt[h].w + 8, HEAD_H}, x, y)) {
                ctx.n = 0; ctx.item = -1; ctx.page = a->page; ctx.track = a->track;
                if (hdr_kind[h] == HT_TEMPO) { tp_show(hdr_tgt[h].x - 40); touch.on = 0; touch.drag = 0; return; }
                if (hdr_kind[h] == HT_TRACK) ctx_track_list(a); else if (hdr_kind[h] == HT_PAGE) ctx_page_list(a); else ctx_clock_list(a);
                ctx_show(hdr_kind[h] == HT_PAGE ? hdr_tgt[h].x - 100 : hdr_tgt[h].x, HEAD_H);
                touch.on = 0; touch.drag = 0;
                return;
            }
    }
    if (phase == 0 && (prof->input & K_IN_MOUSE) && header_preset.w && inside(header_preset, x, y) && !a->menu_open && !a->settings_open && !a->save_open && !a->save_ask) {
        ctx.n = 0; ctx.item = -1; ctx.page = a->page; ctx.track = a->track;   /* the preset's name: the presets to pick from */
        ctx_preset_list(a); ctx_show(x, HEAD_H);
        touch.on = 0; touch.drag = 0;
        return;
    }
    if (phase == 0 && (prof->input & K_IN_MOUSE) && a->page == K_PAGE_MIXER && mix_ctl_at == now && !a->tools_open && !a->menu_open &&
        !a->settings_open && !a->save_open && !a->save_ask) {   /* a desktop: any strip's control, its track taken with it (no strip to pick first) */
        int t, c;
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 2; c++)   /* an insert slot: the effects to pick from */
            if (mix_ins[t][c].w && inside(mix_ins[t][c], x, y)) {
                if (t != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
                ctx.n = 0; ctx.item = -1; ctx.page = a->page; ctx.track = a->track;
                ctx_fx_list(a, c); ctx_show(x, y);
                touch.on = 0; touch.drag = 0;
                return;
            }
        for (t = 0; t < K_TRACKS; t++) for (c = 0; c < 6; c++) {
            const KRect r = c ? mix_ctl[t][c] : (KRect){mix_ctl[t][0].x, mix_ctl[t][0].y - 12, mix_ctl[t][0].w, mix_ctl[t][0].h + 24};
            if (!mix_ctl[t][c].w || !inside(r, x, y)) continue;
            if (c >= 4) { if (c == 4) k_mute_toggle(a, t); else k_solo_toggle(a, t); touch.on = 0; touch.drag = 0; return; }   /* M, S */
            if (t != a->track) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
            if (c == 0) { mix_fader = mix_ctl[t][0]; touch.drag = 3; break; }   /* the fader: under the pointer (below) */
            touch.on = 1; touch.drag = 5; touch.ctl = c; touch.y = y; touch_hides_cursor(a, ms);   /* a knob: up and down */
            return;
        }
        if (touch.drag == 3) goto fader;
    }
    if (touch.drag == 5 && phase == 1 && touch.on) {
        const int steps = (touch.y - y) / 4;
        if (steps) {
            k_param_set(a, K_PAGE_MIXER, touch.ctl, k_param_get(a, K_PAGE_MIXER, touch.ctl) + steps);   /* (within its range: the values' table) */
            a->touched |= (uint64_t)1 << touch.ctl; a->touch_ms[touch.ctl] = ms;
            touch.y -= steps * 4;
        }
        return;
    }
    if (touch.drag == 5 && phase == 2) { touch.on = 0; touch.drag = 0; return; }
    if (phase == 0 && a->page == K_PAGE_MIXER && mix_fader.h > 0 && mix_fader_at == now && !a->tools_open && !a->menu_open && !a->settings_open &&
        inside((KRect){mix_fader.x, mix_fader.y - 12, mix_fader.w, mix_fader.h + 24}, x, y)) touch.drag = 3;   /* the fader: the handle goes where the finger is */
    else if (phase == 0) touch.drag = 0;
fader:
    if (touch.drag == 3 && phase != 2) {
        const int v = (mix_fader.y + mix_fader.h - y) * 100 / mix_fader.h;
        int i;
        touch.on = 1;
        for (i = 0; i < a->focus_count; i++) if (a->focus[i].kind == K_NODE_PARAM && a->focus[i].index == 0) a->focused = i;
        k_param_set(a, K_PAGE_MIXER, 0, v);   /* (set, not stepped: where the handle is drawn; kept in 0..100) */
        a->touched |= 1u; a->touch_ms[0] = ms;
        touch_hides_cursor(a, ms);
        return;
    }
    if (phase == 2 && touch.drag == 3) { touch.on = 0; touch.drag = 0; return; }
    if (phase == 0 && a->tools_open && (prof->input & K_IN_MOUSE)) {   /* the open tool's slider: its value under the pointer */
        int k;
        for (k = 0; k < 2; k++)
            if (tool_slider[k].w && inside(tool_slider[k], x, y)) { touch.on = 1; touch.drag = 8; touch.ctl = k; slider_to(a, k, x); return; }
    }
    if (touch.drag == 8 && touch.on && phase == 1) { slider_to(a, touch.ctl, x); return; }
    if (touch.drag == 8 && phase == 2) { touch.on = 0; touch.drag = 0; return; }
    if (phase == 0 && header_master.w && inside(header_master, x, y)) { touch.on = 1; touch.drag = 2; touch.inv = 0; touch.y = y; return; }   /* the level knob: dragged */
    if (phase == 1 && touch.on && touch.drag == 2) {
        const int steps = (touch.y - y) / 2;
        if (steps) { k_dispatch(a, (KAction){K_ACT_MASTER_DELTA, steps, 0, ms}); touch.y -= steps * 2; }
        return;
    }
    if (phase == 0) {   /* a drag turns only what the finger landed on: a value node it hit, on the same page, no overlay before or after */
        const int over = a->menu_open || a->settings_open || a->save_open || a->save_ask || a->tools_open;
        const int hit = over || ui_hit_header(x, y) ? -1 : k_hit_focus(a, x, y);
        const KPage pg = a->page;
        const KNodeKind k = hit >= 0 ? a->focus[hit].kind : K_NODE_STEP;
        static uint32_t tap_ms; static int tap_track = -1;
        if (K_IS_HOME(pg) && hit >= 0 && k == K_NODE_TRACK) {   /* a track tapped twice (a double click): its sequencer */
            const int t = a->focus[hit].index;
            if (t == tap_track && (uint32_t)(ms - tap_ms) < 400u) {
                tap_track = -1; touch.on = 0; touch.drag = 0;
                if (a->track != t) k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, t - a->track, 0, ms});
                k_open_page(a, k_seq_page(a));
                touch_hides_cursor(a, ms);
                return;
            }
            tap_track = t; tap_ms = ms;
        } else tap_track = -1;
        ui_tap(a, x, y, ms);
        touch.on = 1; touch.y = y;
        touch.drag = hit >= 0 && a->page == pg && !a->menu_open && !a->settings_open && !a->save_open && !a->save_ask &&
                     a->focused == hit && (k == K_NODE_PARAM || k == K_NODE_SCENE || k == K_NODE_SOUND);
        /* ORGAN's drawbars are pulled down (0 in, 8 out): a finger down pulls one out, the value up */
        touch.inv = touch.drag && k == K_NODE_PARAM && pg == K_PAGE_SOUND && !k_track_missing(a, a->track) &&
                    k_engine_uid(a->sound[a->track]) == K_UID('K', 'O', 'G', 'N') && a->focus[hit].index >= 3 && a->focus[hit].index < 12;
        touch.x = x;
        touch.horiz = touch.drag && (k == K_NODE_SOUND || (k == K_NODE_PARAM && a->focus[hit].bounds.w >= 2 * a->focus[hit].bounds.h));
        if (touch.inv) { touch.drag = 6; touch.ctl = a->focus[hit].index; drawbar_r = a->focus[hit].bounds; drawbar_to(a, y, ms); }   /* a drawbar: its cap under the finger */
        if (hit >= 0 && k == K_NODE_STEP && pg == K_PAGE_DRUM && a->page == pg) {   /* a drum step: dragged on, the others it passes take its new state */
            const int idx = a->focus[hit].index;
            touch.drag = 9; touch.x = idx; touch.ctl = (a->lane_on[a->track][idx / K_STEPS] >> (idx % K_STEPS)) & 1;
        }
    } else if (phase == 1 && touch.on && touch.drag == 9) {
        const int h2 = k_hit_focus(a, x, y);
        if (h2 >= 0 && a->focus[h2].kind == K_NODE_STEP && a->focus[h2].index != touch.x) {
            const int idx = a->focus[h2].index, l = idx / K_STEPS, st = idx % K_STEPS;
            k_lane_step_set(a, a->track, l, st, touch.ctl);   /* (it takes the state the first step got) */
            touch.x = idx;
        }
    } else if (phase == 1 && touch.on && touch.drag == 6) {
        drawbar_to(a, y, ms);
    } else if (phase == 1 && touch.on && touch.drag && touch.horiz) {   /* a card, the engine's bar: right is more */
        const int steps = (x - touch.x) / 4;
        if (steps) { k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, steps, 0, ms}); touch.x += steps * 4; touch_hides_cursor(a, ms); }
    } else if (phase == 1 && touch.on && touch.drag) {
        const int steps = (touch.y - y) / 4;
        if (steps) { k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, touch.inv ? -steps : steps, 0, ms}); touch.y -= steps * 4; touch_hides_cursor(a, ms); }
    } else if (phase == 2) touch.on = 0;
}
/* ---- a pad: the d-pad walks the grid (the page's targets, or the header's parts); A tapped presses what is under the
 * cursor, A held with a direction turns its value (up / right more); B goes up to the header, and from it back down (in a list or
 * an overlay: back); SELECT tapped the page list, held the menu for what the cursor is on; START plays; L R the track; X the tools;
 * Y records. The keys of a desktop come here too (arrows, Enter as A, Esc as B, Tab as SELECT). ---- */
static int pad_overlay(const KApp *a) { return a->menu_open || a->settings_open || a->save_open || a->save_ask || a->name_open || a->tools_open; }
static void pad_hsort(void)   /* (the header's parts left to right: the tools button is drawn after the page icon) */
{
    int i, j;
    for (i = 1; i < hdr_tn; i++) for (j = i; j > 0 && hdr_tgt[j].x < hdr_tgt[j - 1].x; j--) {
        KRect r = hdr_tgt[j]; int k = hdr_kind[j];
        hdr_tgt[j] = hdr_tgt[j - 1]; hdr_kind[j] = hdr_kind[j - 1]; hdr_tgt[j - 1] = r; hdr_kind[j - 1] = k;
    }
}
static void popup_step(int d)
{
    int i = ctx.psel, k;
    for (k = 0; k < ctx.n; k++) {
        i = (i + d + ctx.n) % ctx.n;
        if (ctx.it[i].act != CA_SEP && ctx.it[i].act != CA_HEAD) { ctx.psel = i; return; }
    }
}
static void pad_press(KApp *a, uint32_t ms)   /* A tapped */
{
    if (ctx.open) {
        if (ctx.psel >= 0 && ctx.rows[ctx.psel].w) ui_popup_click(a, ctx.rows[ctx.psel].x + 2, ctx.rows[ctx.psel].y + 2, ms);
        else ui_popup_close();
        return;
    }
    if (pad.region == 1 && !pad_overlay(a) && pad.hsel >= 0 && pad.hsel < hdr_tn) {
        const KRect r = hdr_tgt[pad.hsel];
        switch (hdr_kind[pad.hsel]) {
        case HT_TRACK: k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, 1, 0, ms}); break;
        case HT_PLAY: k_dispatch(a, (KAction){K_ACT_PLAY, 0, 0, ms}); break;
        case HT_PRESET: ctx.n = 0; ctx.item = -1; ctx.page = a->page; ctx.track = a->track; ctx_preset_list(a); ctx_show(r.x, HEAD_H); break;
        case HT_TOOLS: pad.region = 0; k_dispatch(a, (KAction){K_ACT_TOOLS, 0, 0, ms}); break;
        case HT_PAGE: pad.region = 0; k_dispatch(a, (KAction){K_ACT_PAGE_MENU, 0, 0, ms}); break;
        default: break;   /* (the tempo, the level: A held and a direction) */
        }
        return;
    }
    k_dispatch(a, (KAction){K_ACT_ACCEPT, 0, 0, ms});
}
static void pad_turn(KApp *a, int d, uint32_t ms)   /* A held and a direction: the value under the cursor */
{
    if (pad.region == 1 && !pad_overlay(a) && pad.hsel >= 0 && pad.hsel < hdr_tn) {
        switch (hdr_kind[pad.hsel]) {
        case HT_TRACK: k_dispatch(a, (KAction){K_ACT_TRACK_DELTA, d, 0, ms}); break;
        case HT_TEMPO: k_param_set(a, K_PAGE_CLOCK, 0, a->bpm + d); break;
        case HT_MASTER: k_dispatch(a, (KAction){K_ACT_MASTER_DELTA, 2 * d, 0, ms}); break;
        case HT_PRESET: k_dispatch(a, (KAction){K_ACT_SOUND_DELTA, d, 1, ms}); break;   /* (1: the header shows the preset) */
        default: break;
        }
        return;
    }
    k_dispatch(a, (KAction){K_ACT_VALUE_DELTA, d, 0, ms});
    touch_hides_cursor(a, ms); cur_touch.hide = 0;   /* (the cursor stays: it is the pad's) */
}
int ui_pad(KApp *a, int b, int down, uint32_t ms)
{
    pad_on = 1;
    if (b == KP_A) {
        if (down) { pad.a_down = 1; pad.a_used = 0; return 1; }
        if (pad.a_down && !pad.a_used) pad_press(a, ms);
        pad.a_down = 0;
        return 1;
    }
    if (b == KP_SELECT) {
        if (down) { pad.sel_down = 1; pad.sel_fired = 0; pad.sel_ms = ms; return 1; }
        if (pad.sel_down && !pad.sel_fired) {   /* tapped: the page list (the popup up: it closes) */
            if (ctx.open) ui_popup_close();
            else { pad.region = 0; k_dispatch(a, (KAction){K_ACT_PAGE_MENU, 0, 0, ms}); }
        }
        pad.sel_down = 0;
        return 1;
    }
    if (!down) return 1;
    if (pad.sel_down && (b == KP_START || b == KP_Y || b == KP_L || b == KP_R)) {   /* SELECT held + START: SAVE, + Y: undo, + L R: the presets
                                                 * (the interface audit's A-1, 2026-11-12: the pad had no way to them) */
        pad.sel_fired = 1;   /* (its release opens nothing) */
        if (b == KP_START) k_dispatch(a, (KAction){K_ACT_SAVE, 0, 0, ms});
        else if (b == KP_Y) k_dispatch(a, (KAction){K_ACT_UNDO, 0, 0, ms});
        else k_dispatch(a, (KAction){K_ACT_SOUND_DELTA, b == KP_L ? -1 : 1, 1, ms});
        return 1;
    }
    if (b == KP_L2 || b == KP_R2) { k_dispatch(a, (KAction){K_ACT_OCTAVE_DELTA, b == KP_L2 ? -1 : 1, 0, ms}); return 1; }   /* the triggers: the octave
                                                 * (where something asks: no, yes) */
    if (ctx.open) {   /* a list up: up / down its rows, B closes */
        if (b == KP_UP || b == KP_LEFT) popup_step(-1);
        else if (b == KP_DOWN || b == KP_RIGHT) popup_step(1);
        else if (b == KP_B) ui_popup_close();
        return 1;
    }
    if (b <= KP_RIGHT) {
        const int d = b == KP_UP || b == KP_RIGHT ? 1 : -1;
        if (pad.a_down) { pad.a_used = 1; pad_turn(a, d, ms); return 1; }
        if (pad.region == 1 && !pad_overlay(a)) {   /* the header: left and right its parts, down back to the page */
            pad_hsort();
            if (b == KP_LEFT && pad.hsel > 0) pad.hsel--;
            else if (b == KP_RIGHT && pad.hsel < hdr_tn - 1) pad.hsel++;
            else if (b == KP_DOWN) pad.region = 0;
            return 1;
        }
        k_dispatch(a, (KAction){b == KP_UP ? K_ACT_UP : b == KP_DOWN ? K_ACT_DOWN : b == KP_LEFT ? K_ACT_LEFT : K_ACT_RIGHT, 0, 0, ms});
        return 1;
    }
    switch (b) {
    case KP_B:
        if (pad_overlay(a)) k_dispatch(a, (KAction){K_ACT_BACK, 0, 0, ms});
        else if (pad.region == 0) { pad.region = 1; pad_hsort(); if (pad.hsel < 0 || pad.hsel >= hdr_tn) pad.hsel = 0; }
        else pad.region = 0;
        return 1;
    case KP_START: k_dispatch(a, (KAction){K_ACT_PLAY, 0, 0, ms}); return 1;
    case KP_L: case KP_R:   /* the track; over a sheet with tabs (the settings, SAVE): the tab (ALGORITHM's) */
        k_dispatch(a, (KAction){a->settings_open ? K_ACT_TYPE_DELTA : K_ACT_TRACK_DELTA, b == KP_L ? -1 : 1, 0, ms}); return 1;
    case KP_X: pad.region = 0; k_dispatch(a, (KAction){K_ACT_TOOLS, 0, 0, ms}); return 1;
    case KP_Y: k_dispatch(a, (KAction){K_ACT_REC, 0, 0, ms}); return 1;
    default: return 0;
    }
}
/* SELECT held 500 ms: the menu for what the cursor is on (the right click's); called each frame. 1 when it opened */
int ui_pad_tick(KApp *a, uint32_t ms)
{
    if (!pad.sel_down || pad.sel_fired || (uint32_t)(ms - pad.sel_ms) < 500u) return 0;
    pad.sel_fired = 1;
    if (pad_overlay(a) || ctx.open) return 0;
    {   const KRect r = k_focus_rect(a);
        pad.region = 0;
        if (r.w <= 0) return 0;
        return ui_context(a, r.x + r.w / 2, r.y + r.h / 2, ms); }
}
