/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The sheets over a page: SAVE (and NAME), the settings, the tools, the page switcher's tiles and the page menu. */
#include "draw_int.h"

/* the settings (HOME held), after Felucca's MENU > SYSTEM: a box of rows, the name left and its value right, the row in hand lit.
 * ALGORITHM (or a cursor) picks the row, a knob or SELECT turns it, HOME closes. */
/* SAVE (kreisel_save.h): the SONG or SOUND side's slots, a window of them around the one in hand; under them the four actions
 * the knobs arm (K1 LOAD, K2 SAVE, K3 NAME, K4 ERASE) and, armed, what OCT+ will do. Over it, while naming, the NAME screen. */
KRect save_target;   /* where the cursor goes on SAVE: the slot in hand, or the NAME screen's letter */
void page_save(const KApp *a)
{
    const int kind = a->save_open - 1, n = kind ? K_USER_PRESETS : K_SONG_SLOTS, sel = a->save_sel[kind];
    const int ix = whole.x + PAD, iy = whole.y + PAD, iw = whole.w - 2 * PAD, bottom = whole.y + whole.h - PAD;
    const int wide = whole.w * 2 > whole.h * 3;   /* a wide screen: the actions in a column at the right, the slots beside them */
    const int colw = wide ? 74 : 0, lw = wide ? iw - colw - 6 : iw, cardh = 30, rh = 18, top = iy + 23, prompt = 16;
    const int list_end = wide || key_name(K_KEY_OK) ? bottom - prompt : bottom - cardh - prompt - 2;   /* (keys: no cards, the list longer) */
    const int rows = (list_end - top) / (rh + 1) < n ? (list_end - top) / (rh + 1) : n;
    int first = sel - rows / 2, i, k;
    char b[48];
    flat_box(whole);
    for (k = 0; k < 2; k++) {   /* the two sides, as tabs */
        const int tw = 64, tx = ix + 4 + k * (tw + 4);
        if (k == kind) flat_fill( tx, iy, tw, 18, 5, th.sel);
        g_text_box(&cv, tx, iy, tw, 18, GF_S, k ? "Sound" : "Song", k == kind ? th.ink : th.mid, 0);
        save_tabs[k] = (KRect){tx, iy, tw, 18};
    }
    if (kind) snprintf(b, sizeof b, "T%d %s", a->track + 1, k_engine_at(a->sound[a->track])->name);   /* where LOAD puts a sound */
    else snprintf(b, sizeof b, "%s", a->song_name[0] ? a->song_name : "");
    g_text_box(&cv, ix + iw / 2, iy, iw / 2 - 6, 18, GF_S, b, th.mid, 1);
    if (first > n - rows) first = n - rows;
    if (first < 0) first = 0;
    menu_rows_n = 0;
    save_rows_n = 0;
    for (i = 0; i < rows; i++) {
        const int s2 = first + i, on = s2 == sel, used = k_save_used(kind, s2);
        const KRect r = {ix, top + i * (rh + 1), lw, rh};
        save_rows[save_rows_n] = r; save_row_slot[save_rows_n++] = s2;
        const uint32_t ink = on ? th.ink : used ? th.text : th.dim;
        if (on) { flat_fill( r.x, r.y, r.w, r.h, 5, th.sel); save_target = r; }
        snprintf(b, sizeof b, "%d", s2 + 1);
        g_text_box(&cv, r.x + 4, r.y, 22, r.h, GF_S, b, on ? th.ink : th.mid, 1);
        g_text_box(&cv, r.x + 34, r.y, r.w - 100, r.h, GF_M, used ? (k_save_name(kind, s2)[0] ? k_save_name(kind, s2) : "(no name)") : "-", ink, -1);
        if (kind && used) {   /* a preset's engine at the right */
            const int e = k_engine_find(k_save_engine(s2));
            g_text_box(&cv, r.x + r.w - 70, r.y, 64, r.h, GF_S, e >= 0 ? k_engine_at(e)->name : "?", on ? th.ink : th.mid, 1);
        } else if (!kind && a->song_from == s2 + 1)   /* the song in hand came from here */
            g_rrect(&cv, r.x + r.w - 14, r.y + r.h / 2 - 3, 6, 6, 3, on ? th.ink : th.accent);
    }
    if (!key_name(K_KEY_OK)) {   /* a pointer's device: the actions as cards to tap, the armed one lit (a tap arms, again does it) */
        const int used = k_save_used(kind, sel);
        const int cw = wide ? colw : (iw - 3 * 4) / 4, ch = wide ? (bottom - iy - 3 * 4) / 4 : cardh;
        for (k = 0; k < 4; k++) {
            const int act = k + 1, can = act == K_SAVE_SAVE || used, armed = (a->save_act == K_SAVE_SURE ? K_SAVE_ERASE : a->save_act) == act;
            const int cx = wide ? ix + iw - colw : ix + k * (cw + 4), cy = wide ? iy + k * (ch + 4) : bottom - cardh;
            save_cards[k] = (KRect){cx, cy, cw, ch};
            if (armed) flat_fill( cx, cy, cw, ch, 5, th.theme);
            else g_rrect_frame(&cv, cx, cy, cw, ch, 5, th.edge, th.surf);
            g_text_box(&cv, cx, cy + (ch - 16) / 2, cw, 16, GF_S, a->save_act == K_SAVE_SURE && armed ? "Sure?" : k_save_act_name(act), armed ? th.ink : can ? th.text : th.dim, 0);
        }
    } else {   /* the device's keys (the FM-1): no knob picks an action; OCT+ on the slot opens a menu of them beside it,
                * SELECT or ALGORITHM its row, OCT+ does it, OCT- shuts it */
        const char *ok = key_name(K_KEY_OK), *no = key_name(K_KEY_CANCEL);
        for (k = 0; k < 4; k++) save_cards[k] = (KRect){0, 0, 0, 0};
        if (a->save_act) {
            const int nr = k_save_used(kind, sel) ? 4 : 1, mrh = 20, mw = 104, mh = nr * mrh + 8, mx = ix + lw - mw - 6;
            const int at = top + (sel - first) * (rh + 1) + rh + 2, my = at + mh <= bottom - prompt ? at : top + (sel - first) * (rh + 1) - mh - 2;
            const int cur = a->save_act == K_SAVE_SURE ? K_SAVE_ERASE : a->save_act;
            g_rrect_frame(&cv, mx, my, mw, mh, 7, th.edge, th.bg);
            for (k = 0; k < nr; k++) {
                const int act = k_save_menu_row(nr == 4, k), on = act == cur, ry = my + 4 + k * mrh;
                static const short SAVE_ICON[5] = {0, GI_LIBRARY, GI_SONG, GI_T_PASTE, GI_T_CLEAR};   /* Load Save Name Erase (placeholders: the owner draws) */
                if (on) flat_fill(mx + 4, ry, mw - 8, mrh, 5, th.sel);
                g_icon(&cv, mx + 10, ry + (mrh - 12) / 2, 12, SAVE_ICON[act], on ? th.ink : th.mid);
                g_text_box(&cv, mx + 28, ry, mw - 32, mrh, GF_M, on && a->save_act == K_SAVE_SURE ? "Sure?" : k_save_act_name(act), on ? th.ink : th.text, -1);
            }
        }
        if (!a->name_open) {   /* the foot: OCT- with its cross, OCT+ with its tick (as NAME's line); what OCT+ does, at the left (NAME: its own) */
            const int ky = bottom - 16;
            int x, w;
            if (a->save_act == K_SAVE_SURE) snprintf(b, sizeof b, "Erase %d?", sel + 1);
            else if (a->save_act) snprintf(b, sizeof b, "%s %d", k_save_act_name(a->save_act), sel + 1);
            else b[0] = 0;   /* (the slots: OCT+ opens the menu; nothing to say) */
            w = (b[0] ? g_text_w(GF_S, b) + 10 : 0) + (no ? g_text_w(GF_S, no) + 6 + 4 + 10 + 8 : 0) + g_text_w(GF_S, ok) + 6 + 4 + 10;
            x = ix + (lw - w) / 2;
            if (b[0]) { g_text(&cv, x, ky + (16 - g_text_h(GF_S)) / 2, GF_S, b, th.accent); x += g_text_w(GF_S, b) + 10; }
            if (no) {
                x += keycap_n(x, ky, no, 0) + 4;
                g_line(&cv, x + 1, ky + 4, x + 8, ky + 11, 2, th.mid); g_line(&cv, x + 8, ky + 4, x + 1, ky + 11, 2, th.mid);
                x += 10 + 8;
            }
            x += keycap_n(x, ky, ok, 1) + 4;
            g_line(&cv, x, ky + 8, x + 3, ky + 11, 2, th.accent); g_line(&cv, x + 3, ky + 11, x + 9, ky + 4, 2, th.accent);
        }
    }
    if (a->name_open) {   /* NAME: the letters in boxes, the cursor's lit */
        const int bw = NAME_BW, gx = NAME_GAP, tw = K_NAME_LEN * bw + (K_NAME_LEN - 1) * gx, px = whole.x + (key_name(K_KEY_OK) ? 2 : 8), pw = whole.w - (key_name(K_KEY_OK) ? 4 : 16), ph = key_name(K_KEY_OK) ? 84 : (prof->input & K_IN_TOUCH) ? 92 : 66, py = whole.y + (whole.h - ph) / 2;
        const int x0 = px + (pw - tw) / 2, y0 = py + 30;
        if (!key_name(K_KEY_OK)) g_rrect_a(&cv, whole.x, whole.y, whole.w, whole.h, 0, 0x000000, 225);   /* (a lower box: the list behind
                                                 * it sunk, not a row cut in half under it; the FM-1's box covers it) */
        g_rrect_frame(&cv, px, py, pw, ph, 8, th.edge, th.bg);
        g_text_box(&cv, px, py + 6, pw, 16, GF_S, a->name_open == 1 ? "Name" : "Rename", th.mid, 0);
        for (i = 0; i < K_NAME_LEN; i++) {
            char c[2] = {a->name_buf[i] ? a->name_buf[i] : ' ', 0};
            const int bx = x0 + i * (bw + gx), on = i == a->name_pos;
            if (on) { flat_fill( bx, y0, bw, 26, 4, th.theme); save_target = (KRect){bx, y0, bw, 26}; }
            else g_rrect_frame(&cv, bx, y0, bw, 26, 4, th.edge, th.surf);
            g_text_box(&cv, bx, y0, bw, 26, GF_M, c, on ? th.ink : th.text, 0);
        }
        name_x0 = (int16_t)x0; name_y0 = (int16_t)y0; name_bx = -1;
        if (!key_name(K_KEY_OK) && (prof->input & K_IN_TOUCH)) {   /* no key to do it, a screen to touch: Cancel and Save (Rename) to press
                                                                 * (a keyboard's Enter and Esc, a pad's A and B: no buttons) */
            const char *ok = a->name_open == 1 ? "Save" : "Rename";
            const int bw2 = NAME_BTN_W, bh2 = 22, gap = 8, bx2 = px + (pw - 2 * bw2 - gap) / 2, by2 = y0 + 34;
            name_bx = (int16_t)bx2; name_by = (int16_t)by2;
            g_rrect_frame(&cv, bx2, by2, bw2, bh2, 6, th.edge, th.surf);
            g_text_box(&cv, bx2, by2, bw2, bh2, GF_S, "Cancel", th.text, 0);
            flat_fill( bx2 + bw2 + gap, by2, bw2, bh2, 6, th.theme);
            g_text_box(&cv, bx2 + bw2 + gap, by2, bw2, bh2, GF_S, ok, th.ink, 0);
        }
        if (key_name(K_KEY_OK)) {   /* the device's keys, as caps, on one line: ARM1 the cursor (< >), ARM2 the letter,
                                     * OK writes, CANCEL lets it go */
            const char *k1 = key_name(K_KEY_ARM1), *k2 = key_name(K_KEY_ARM2), *ok = key_name(K_KEY_OK), *no = key_name(K_KEY_CANCEL);
            const int ly = y0 + 32, w1 = k1 && k2 ? (g_text_w(GF_S, k1) + 6) + 1 + 2 * 10 + 6 + (g_text_w(GF_S, k2) + 6) + 3 + g_text_w(GF_S, "A-Z") + 8 : 0,
                      w2 = (no ? (g_text_w(GF_S, no) + 6) + 4 + 10 + 6 : 0) + (g_text_w(GF_S, ok) + 6) + 4 + 10;
            int x = px + (pw - w1 - w2) / 2;
            if (k1 && k2) {
                x += keycap_n(x, ly, k1, 0) + 1;
                g_icon(&cv, x, ly + 3, 10, GI_CARET_L, th.mid); g_icon(&cv, x + 10, ly + 3, 10, GI_CARET_R, th.mid);
                x += 2 * 10 + 6;
                x += keycap_n(x, ly, k2, 0) + 3;
                g_text(&cv, x, ly + (16 - g_text_h(GF_S)) / 2, GF_S, "A-Z", th.mid);
                x += g_text_w(GF_S, "A-Z") + 8;
            }
            if (no) {   /* CANCEL first, then OK, each with its mark: a cross, a tick (drawn: no icon of the owner's yet) */
                x += keycap_n(x, ly, no, 0) + 4;
                g_line(&cv, x + 1, ly + 4, x + 8, ly + 11, 2, th.mid); g_line(&cv, x + 8, ly + 4, x + 1, ly + 11, 2, th.mid);
                x += 10 + 6;
            }
            x += keycap_n(x, ly, ok, 1) + 4;
            g_line(&cv, x, ly + 8, x + 3, ly + 11, 2, th.accent); g_line(&cv, x + 3, ly + 11, x + 9, ly + 4, 2, th.accent);
        }
    }
}
/* the settings (HOME held): a modal over the page in hand, the page dimmed behind; its tabs as SAVE's, the rows of the tab in hand
 * (the name left, the value right). CLEAR ALL armed: its value in red */
void page_settings(const KApp *a)
{
    const int bx = MARGIN + 6, by = HEAD_H + 10, bw = cv.w - 2 * (MARGIN + 6), room = cv.h - by - 8;
    const int tab = k_setting_tab(a->settings_sel), ix = bx + 8, iw = bw - 16, top = by + 8 + 18 + 6;
    const uint32_t fill = 0x0A0A0C, rim = k_mix(fill, th.text, 16), hi = k_mix(fill, th.text, 14);   /* black: the box, a faint rim, the row in hand a grey */
    int i, k, most = 0, rh, bh;
    for (k = 0; k < k_setting_tabs(a); k++) {   /* the rows of the fullest tab: the box keeps its size across the tabs */
        int c = 0;
        for (i = 0; i < K_SETTINGS; i++) c += k_setting_visible(a, i) && k_setting_tab(i) == k;
        if (c > most) most = c;
    }
    rh = (by + room - 6 - top) / (most > 4 ? most : 4) - 2;
    if (rh > 24) rh = 24;
    bh = top - by + most * (rh + 2) + 6;   /* as tall as its rows: no line of how to use it at the foot */
    if (bh > room) bh = room;
    g_rrect_a(&cv, 0, 0, cv.w, cv.h, 0, 0x000000, MENU_VEIL);   /* the page behind it sunk (flat: no shadow) */
    g_rrect_frame(&cv, bx, by, bw, bh, 10, rim, fill);
    set_box = (KRect){bx, by, bw, bh};
    for (k = 0; k < k_setting_tabs(a); k++) {   /* the tabs */
        const int tw = 70, tx = ix + k * (tw + 4);
        set_tabs[k] = (KRect){tx, by + 8, tw, 18};
        if (k == tab) g_rrect(&cv, tx, by + 8, tw, 18, 5, hi);
        g_text_box(&cv, tx, by + 8, tw, 18, GF_S, k_setting_tab_name(k), k == tab ? th.text : th.dim, 0);
    }
    menu_rows_n = 0;
    for (i = 0; i < K_SETTINGS && i < (int)(sizeof menu_rows / sizeof menu_rows[0]); i++) menu_rows[i] = (KRect){0, 0, 0, 0};   /* (the other tab's rows: no target) */
    for (i = 0, k = 0; i < K_SETTINGS; i++) {
        KRect r;
        int sel = i == a->settings_sel;
        const char *v = k_setting_value(a, i);
        if (k_setting_tab(i) != tab || !k_setting_visible(a, i)) continue;
        r = (KRect){ix, top + k * (rh + 2), iw, rh};
        k++;
        if (i < (int)(sizeof menu_rows / sizeof menu_rows[0])) { menu_rows[i] = r; if (i + 1 > menu_rows_n) menu_rows_n = i + 1; }
        if (sel) g_rrect(&cv, r.x, r.y, r.w, r.h, 5, hi);
        g_text_box(&cv, r.x + 8, r.y, r.w - 16, r.h, GF_M, k_setting_name(i), sel ? th.text : th.mid, -1);   /* (the longest pair, CLEAR ALL */
        g_text_box(&cv, r.x + 8, r.y, r.w - 16, r.h, i == K_SET_ABOUT ? GF_S : GF_M, v,                       /* and AGAIN: CLEAR, fits 190 px) */
                   i == K_SET_CLEAR && a->set_ask ? th.rec : i == K_SET_ABOUT ? th.dim : sel ? th.accent : k_mix(fill, th.accent, 70), 1);
    }
}
/* the tools' list (a long press on STEP, DRUM, ROLL: kreisel_seqtool.c), drawn so the pattern stays in sight as a tool is tried on it:
 * a column on the right of a wide screen, a box over the cards at the foot of a narrow one (the FM-1). A row: the
 * tool's name, its values on the right; the row in hand filled. The rows scroll to keep it in sight. */
static GIcon tool_icon(KTool t)
{
    static const GIcon I[K_TOOL_COUNT] = {[K_TOOL_CLEAR] = GI_T_CLEAR, [K_TOOL_FILL] = GI_T_FILL, [K_TOOL_RANDOM] = GI_T_RANDOM,
        [K_TOOL_EUCLID] = GI_T_EUCLID, [K_TOOL_SHIFT] = GI_T_SHIFT, [K_TOOL_REVERSE] = GI_T_REVERSE, [K_TOOL_TRANSPOSE] = GI_P_TRANSPOSE,
        [K_TOOL_COPY] = GI_T_COPY, [K_TOOL_PASTE] = GI_T_PASTE, [K_TOOL_HUMANIZE] = GI_T_HUMANIZE, [K_TOOL_UNDO] = GI_T_UNDO,
        [K_TOOL_SC_INSERT] = GI_T_INSERT, [K_TOOL_SC_DUP] = GI_T_COPY, [K_TOOL_SC_DELETE] = GI_T_DELETE, [K_TOOL_SC_CLEAR] = GI_T_CLEAR, [K_TOOL_DUP] = GI_T_COPY, [K_TOOL_SN_DUP] = GI_T_COPY, [K_TOOL_FX_DUP] = GI_T_COPY,
        [K_TOOL_SN_INIT] = GI_T_INIT, [K_TOOL_SN_RANDOM] = GI_T_RANDOM, [K_TOOL_SN_COPY] = GI_T_COPY, [K_TOOL_SN_PASTE] = GI_T_PASTE,
        [K_TOOL_MX_LEVELS] = GI_PAGE_MIXER, [K_TOOL_MX_PANS] = GI_T_PAN, [K_TOOL_MX_SENDS] = GI_PAGE_BUS, [K_TOOL_MX_UNMUTE] = GI_T_UNMUTE,
        [K_TOOL_FX_SWAP] = GI_T_SWAP, [K_TOOL_FX_INIT] = GI_T_INIT, [K_TOOL_FX_COPY] = GI_T_COPY, [K_TOOL_FX_PASTE] = GI_T_PASTE,
        [K_TOOL_VIEW] = GI_PAGE_PATTERN};
    return (unsigned)t < K_TOOL_COUNT ? I[t] : GI_P_GENERIC;
}
/* a desktop's tools: the row in hand opens (an accordion) into what it does, a slider a value, and Cancel / Apply */
static const char *tool_says(KTool t)
{
    switch (t) {
    case K_TOOL_FILL: return "A note every N steps"; case K_TOOL_EUCLID: return "Hits spread evenly";
    case K_TOOL_RANDOM: return "Random notes"; case K_TOOL_SHIFT: return "Move steps, round";
    case K_TOOL_TRANSPOSE: return "Notes up or down"; case K_TOOL_HUMANIZE: return "Velocities loosened";
    case K_TOOL_REVERSE: return "Back to front"; case K_TOOL_COPY: return "Keep it to paste";
    case K_TOOL_PASTE: return "The copy over this"; case K_TOOL_CLEAR: return "Remove the notes";
    case K_TOOL_UNDO: return "The last tool undone"; case K_TOOL_SC_INSERT: return "An empty scene here";
    case K_TOOL_SC_DUP: return "This scene over another"; case K_TOOL_SC_DELETE: return "Remove this scene";
    case K_TOOL_SC_CLEAR: return "Scenes emptied"; case K_TOOL_DUP: return "This one over another";
    case K_TOOL_SN_DUP: return "This sound over another"; case K_TOOL_FX_DUP: return "These inserts on another track"; case K_TOOL_SN_INIT: return "The engine's defaults";
    case K_TOOL_SN_RANDOM: return "Values moved at random"; case K_TOOL_SN_COPY: return "Keep the sound";
    case K_TOOL_SN_PASTE: return "The copied sound here"; case K_TOOL_MX_LEVELS: return "Every level at 0 dB";
    case K_TOOL_MX_PANS: return "Every pan centred"; case K_TOOL_MX_SENDS: return "Nothing sent to B1, B2";
    case K_TOOL_MX_UNMUTE: return "No mute, no solo"; case K_TOOL_FX_SWAP: return "Slot 1 and 2 swapped";
    case K_TOOL_FX_INIT: return "Slots emptied"; case K_TOOL_FX_COPY: return "Keep the inserts";
    case K_TOOL_FX_PASTE: return "The copied inserts here";
    default: return "";
    }
}
int ui_tool_part(int k, KRect *r) { *r = k < 2 ? tool_slider[k] : tool_btn[k - 2]; return r->w > 0; }   /* (tests: 0, 1 the sliders, 2 Cancel, 3 Apply) */
static int tools_open_h(const KApp *a, KTool t)   /* the open row's extra height: what it does, its sliders, the buttons */
{
    return 18 + 30 * k_tool_vals(t, k_is_drum(a, a->track)) + 30;
}
void page_tools(const KApp *a)
{
    const int wide = cv.w >= 320, n = k_tools_count(a), drum = k_is_drum(a, a->track), big = cv.h >= 300;
    const GFont f = big ? GF_M : GF_S;
    const int rh = g_text_h(f) + (big ? 10 : 6), head = g_text_h(GF_S) + 6;
    const int knobs = !(a->lay & (K_LAY_SHEET_CURSOR | K_LAY_SHEET_POINTER));   /* (the sheet's kind) */
    const int foot = a->tools_open && key_name(K_KEY_OK) && !rail.w && cv.w < 320 ? 20 + (knobs ? 22 : 0) : 0;   /* a device with keys for yes and
                                                 * no: what OCT+ will do, at the foot (the keys alone said nothing); a
                                                 * knobs device: the tool's values over it, each where its knob is */
    const int acc = (prof->input & K_IN_MOUSE) && a->tools_open, ex = acc ? tools_open_h(a, k_tools_item(a, a->tools_sel)) : 0;
    KRect b;
    int i, vis, first;
    char v0[12], v1[12], vv[28], title[24];
    if (rail.w) b = rail;   /* (always shown there: lit while in use) */
    else if (wide) { const int w = big ? RAIL_W : 118; b = (KRect){whole.x + whole.w - w, whole.y, w, whole.h}; }
    else { const int h = head + 4 * (rh + 2) + 6 + foot; b = (KRect){whole.x, whole.y + whole.h - h, whole.w, h}; }
    tools_box = b;
    if (!rail.w) g_rrect_a(&cv, 0, HEAD_H, cv.w, cv.h - HEAD_H, 0, 0x000000, TOOLS_VEIL);   /* over the page: the page sunk, still seen
                                                 * (a tool tried on it shows), the header as it is (PLAY, the keys work under it);
                                                 * flat, no shadow, its face a step lighter than the page */
    g_rrect_frame(&cv, b.x, b.y, b.w, b.h, 7, rail.w ? k_mix(th.surf, th.text, 16) : k_mix(th.surf, th.text, 22), rail.w ? k_mix(th.bg, th.surf, 70) : th.surf);
    if (a->page == K_PAGE_SONG) {   /* (what the tools work on: the scene, the sound, every track, the inserts, the lane, the pattern) */
        int sc = a->song_scene;
        if (a->focus_count && a->focus[a->focused].kind == K_NODE_SCENE) sc = a->focus[a->focused].index % K_SCENES;
        snprintf(title, sizeof title, "Tools  Scene %d", sc + 1);
    } else if (a->page == K_PAGE_MIXER || a->page == K_PAGE_TRACKS) snprintf(title, sizeof title, "Tools  All tracks");
    else if (a->page == K_PAGE_FX) snprintf(title, sizeof title, "Tools  FX %d", a->track + 1);
    else if ((a->page == K_PAGE_SOUND || a->page == K_PAGE_EDIT) && !drum) snprintf(title, sizeof title, "Tools  Sound %d", a->track + 1);
    else if (drum) snprintf(title, sizeof title, "Tools  Lane %d", a->cur_lane[a->track] + 1);
    else snprintf(title, sizeof title, "Tools  Pattern %c", 'A' + (a->pattern[a->track] & 3));
    memset(pat_chip, 0, sizeof pat_chip);
    if (rail.w && (a->page == K_PAGE_ROLL || a->page == K_PAGE_STEP) && !drum) {   /* the rail: "Tools" and the patterns as chips, the one
                                                 * playing filled (a pointer picks a pattern) */
        const int cw = g_text_w(GF_S, "D") + 10, chh = g_text_h(GF_S) + 2;
        int k;
        snprintf(title, sizeof title, "Tools");
        for (k = 0; k < 4; k++) {
            const int x = b.x + b.w - 8 - (4 - k) * (cw + 2), on = (a->pattern[a->track] & 3) == k;
            const char nm[2] = {(char)('A' + k), 0};
            pat_chip[k] = (KRect){(int16_t)x, (int16_t)(b.y + 2), (int16_t)cw, (int16_t)chh};
            g_rrect(&cv, x, b.y + 2, cw, chh, 3, on ? th.accent : k_mix(th.bg, th.text, 12));
            g_text_box(&cv, x, b.y + 2, cw, chh, GF_S, nm, on ? th.ink : th.mid, 0);
        }
    }
    g_text(&cv, b.x + 8, b.y + 3, GF_S, title, th.mid);
    if (foot) {   /* the foot: [OCT+] what it will do (the tool, its value: "Duplicate -> 2" with the arrow, "Clear One"), [OCT-] */
        const KTool t = k_tools_item(a, a->tools_sel);
        const char *ok = key_name(K_KEY_OK), *no = key_name(K_KEY_CANCEL);
        char what[32], v0[12], v1[12];
        const int dup = t == K_TOOL_DUP || t == K_TOOL_SC_DUP || t == K_TOOL_SN_DUP || t == K_TOOL_FX_DUP, fy = b.y + b.h - 20;
        int w, x;
        if (knobs) {   /* the values, a box each over its knob (K1 the first; K2 the second where there are two: tools_turn) */
            const int nv = k_tool_vals(t, drum), sw = (b.w - 8) / 4, sy = fy - 22;
            int k;
            for (k = 0; k < 4; k++) {
                const int sx = b.x + 4 + k * sw;
                char vt[12];
                if (k >= nv) { g_rrect_frame(&cv, sx + 1, sy, sw - 2, 18, 4, k_mix(th.surf, th.text, 10), th.surf); continue; }
                k_tool_val_text(a, t, k, vt, sizeof vt);
                g_rrect(&cv, sx + 1, sy, sw - 2, 18, 4, k_mix(th.surf, th.text, 12));
                {   const char *ln = wcase(k_tool_val_name(t, k, drum));
                    if (g_text_w(GF_S, ln) + g_text_w(GF_S, vt) + 6 <= sw - 10) g_text_box(&cv, sx + 5, sy, sw - 10, 18, GF_S, ln, th.mid, -1); }   /* (no room: the value alone) */
                g_text_box(&cv, sx + 5, sy, sw - 10, 18, GF_S, vt, a->tools_try == a->tools_sel ? th.rec : th.accent, 1);
            }
        }
        k_tool_val_text(a, t, 0, v0, sizeof v0); k_tool_val_text(a, t, 1, v1, sizeof v1);
        if (knobs) snprintf(what, sizeof what, "%s", k_tool_name(t));   /* (its values: over it) */
        else if (dup) snprintf(what, sizeof what, "%s \x86 %s", k_tool_name(t), v0);   /* (0x86: Inter's arrow, baked: tools/gen_aa_font.py CODE) */
        else if (v1[0]) snprintf(what, sizeof what, "%s %s %s", k_tool_name(t), v0, v1);
        else if (v0[0]) snprintf(what, sizeof what, "%s %s", k_tool_name(t), v0);
        else snprintf(what, sizeof what, "%s", k_tool_name(t));
        g_fill(&cv, b.x + 6, fy - 1, b.w - 12, 1, k_mix(th.bg, th.text, 14));
        w = keycap_w(ok) + 6 + g_text_w(GF_S, what) + (no ? 12 + keycap_w(no) : 0);
        x = b.x + (b.w - w) / 2;
        x += keycap(x, fy + 2, ok, 1) + 6;
        g_text(&cv, x, fy + 2 + (16 - g_text_h(GF_S)) / 2, GF_S, what, th.accent);
        if (no) keycap(x + g_text_w(GF_S, what) + 12, fy + 2, no, 0);
    }
    vis = (b.h - head - foot - 4 - ex) / (rh + 2);
    tool_slider[0] = tool_slider[1] = tool_btn[0] = tool_btn[1] = (KRect){0, 0, 0, 0};
    if (vis > n) vis = n;
    if (vis < 1) vis = 1;
    first = a->tools_sel - vis / 2;
    if (first > n - vis) first = n - vis;
    if (first < 0) first = 0;
    menu_rows_n = n;
    for (i = 0; i < n; i++) menu_rows[i] = (KRect){0, 0, 0, 0};   /* (a row scrolled out: no target) */
    for (i = first; i < first + vis; i++) {
        const KTool t = k_tools_item(a, i);
        const KRect r = {b.x + 4, b.y + head + (i - first) * (rh + 2) + (acc && i > a->tools_sel ? ex : 0), b.w - 8, rh};
        const int sel = i == a->tools_sel && a->tools_open, nv = k_tool_vals(t, drum), ic = rail.w || big ? ICON + 6 : 0;   /* (a big screen: an icon a row) */
        const uint32_t vc = sel ? th.ink : a->tools_open ? th.accent : k_mix(th.surf, th.accent, 60);
        menu_rows[i] = r;
        if (i > first) g_fill(&cv, r.x + 4, r.y - 1, r.w - 8, 1, k_mix(th.bg, th.text, 14));   /* a line between the rows */
        if (sel) flat_fill( r.x, r.y, r.w, r.h, 5, th.sel);
        if (ic) g_icon(&cv, r.x + 6, r.y + (r.h - ICON) / 2, ICON, tool_icon(t), sel ? th.ink : a->tools_open ? th.text : th.mid);
        g_text_box(&cv, r.x + 6 + ic, r.y, r.w - 12 - ic, r.h, f, k_tool_name(t), sel ? th.ink : a->tools_open ? th.text : th.mid, -1);
        k_tool_val_text(a, t, 0, v0, sizeof v0); k_tool_val_text(a, t, 1, v1, sizeof v1);
        if (nv == 2) snprintf(vv, sizeof vv, "%s  %s", v0, v1); else snprintf(vv, sizeof vv, "%s", v0);
        (void)vv; (void)vc;   /* (the names only: a tool's values show when it is opened) */
        if (acc && sel) {   /* the open row: what it does, a slider a value (its label, its value: red while tried), Cancel / Apply */
            const int x0 = r.x + 6, w0 = r.w - 12;
            int y = r.y + rh + 4, k;
            g_rrect(&cv, r.x, r.y + rh, r.w, ex - 2, 5, k_mix(th.bg, th.surf, 40));
            g_text(&cv, x0, y, GF_S, tool_says(t), th.mid);
            y += 18;
            for (k = 0; k < nv; k++) {
                int lo = 0, hi = 1, v = k ? a->tool_y[t] : a->tool_x[t], kx;
                char vt[12];
                k_tool_range(a, t, k, &lo, &hi);
                k_tool_val_text(a, t, k, vt, sizeof vt);
                g_text(&cv, x0, y, GF_S, k_tool_val_name(t, k, drum), th.text);
                g_text_box(&cv, x0, y, w0, g_text_h(GF_S), GF_S, vt, a->tools_try == i ? th.rec : th.accent, 1);
                kx = x0 + 6 + (hi > lo ? (v - lo) * (w0 - 12) / (hi - lo) : 0);
                g_rrect(&cv, x0, y + 18, w0, 4, 2, k_mix(th.bg, th.text, 18));   /* the track, then the part up to the value */
                g_rrect(&cv, x0, y + 18, kx - x0, 4, 2, th.accent);
                g_rrect(&cv, kx - 5, y + 15, 10, 10, 5, th.text);
                tool_slider[k] = (KRect){(int16_t)x0, (int16_t)(y + 12), (int16_t)w0, 16};
                y += 30;
            }
            {   const int bw = (w0 - 6) / 2;
                tool_btn[0] = (KRect){(int16_t)x0, (int16_t)y, (int16_t)bw, 22};
                tool_btn[1] = (KRect){(int16_t)(x0 + bw + 6), (int16_t)y, (int16_t)bw, 22};
                g_rrect_frame(&cv, x0, y, bw, 22, 5, k_mix(th.surf, th.text, 25), k_mix(th.bg, th.surf, 70));
                g_text_box(&cv, x0, y, bw, 22, GF_S, "Cancel", th.text, 0);
                flat_fill( x0 + bw + 6, y, bw, 22, 5, th.accent);
                g_text_box(&cv, x0 + bw + 6, y, bw, 22, GF_S, "Apply", th.ink, 0);
            }
        }
    }
    if ((a->lay & K_LAY_SHEET_CURSOR) && a->tools_open && a->tools_stage) {   /* a cursor's second box: the tool's values */
        const KTool t = k_tools_item(a, a->tools_sel);
        const int nv = k_tool_vals(t, drum), lh = g_text_h(GF_S) + 7, bw = cv.w * 3 / 5 < 180 ? 180 : cv.w * 3 / 5 > 300 ? 300 : cv.w * 3 / 5;
        const int bh = 24 + nv * (lh + 2) + 26, bx = (cv.w - bw) / 2, by = HEAD_H + (cv.h - HEAD_H - bh) / 2;
        int k;
        g_rrect_a(&cv, 0, HEAD_H, cv.w, cv.h - HEAD_H, 0, 0x000000, 90);
        g_rrect_frame(&cv, bx, by, bw, bh, 7, k_mix(th.surf, th.text, 26), th.surf);
        g_text(&cv, bx + 10, by + 5, GF_M, k_tool_name(t), th.text);
        for (k = 0; k < nv; k++) {
            const int y = by + 26 + k * (lh + 2), on = k == a->tools_which;
            char vt[12];
            k_tool_val_text(a, t, k, vt, sizeof vt);
            if (on) g_rrect(&cv, bx + 4, y, bw - 8, lh, 4, k_mix(th.surf, th.text, 14));
            g_text_box(&cv, bx + 12, y, bw - 24, lh, GF_S, wcase(k_tool_val_name(t, k, drum)), on ? th.text : th.mid, -1);
            g_text_box(&cv, bx + 12, y, bw - 42, lh, GF_S, vt, a->tools_try == a->tools_sel ? th.rec : th.accent, 1);   /* (its right end 30 in) */
            if (on) {   /* the arrows that turn it, a space either side of the value */
                g_text_box(&cv, bx + bw - 24, y, 12, lh, GF_S, ">", th.mid, 0);
                g_text_box(&cv, bx + bw - 30 - g_text_w(GF_S, vt) - 18, y, 12, lh, GF_S, "<", th.mid, 0);
            }
        }
        {   const int yb = by + bh - 22, w1 = g_text_w(GF_S, "Do") + 24, w2 = g_text_w(GF_S, "Back") + 20;
            flat_fill(bx + bw - 10 - w2 - 6 - w1, yb, w1, 16, 8, th.accent);
            g_text_box(&cv, bx + bw - 10 - w2 - 6 - w1, yb, w1, 16, GF_S, "Do", th.ink, 0);
            g_rrect_frame(&cv, bx + bw - 10 - w2, yb, w2, 16, 8, k_mix(th.surf, th.text, 30), th.surf);
            g_text_box(&cv, bx + bw - 10 - w2, yb, w2, 16, GF_S, "Back", th.text, 0);
        }
    }
}
/* the page menu on a touch screen: tiles, four across (a row of them each up / down: KApp.menu_cols), the page's icon over its name */
static void page_tiles(KApp *a)
{
    int ix = whole.x + PAD, iy = whole.y + PAD, iw = whole.w - 2 * PAD, ih = whole.h - 2 * PAD;
    const int n = k_menu_count(a), cols = n < 4 ? n : 4, rows = (n + cols - 1) / cols, gap = 4, top = iy + g_text_h(GF_S) + 8;
    const int tw = (iw - (cols - 1) * gap) / cols, big = cv.h >= 300;
    int i, th_ = (iy + ih - top - (rows - 1) * gap) / rows, isz = big ? 24 : ICON;
    const GFont f = big ? GF_M : GF_S;
    flat_box(whole);
    if (th_ > 96) th_ = 96;
    if (th_ < isz + g_text_h(f) + 10) isz = ICON;
    a->menu_cols = cols;
    g_text(&cv, ix + 8, iy + 2, GF_S, k_group_name(a->menu_group), th.mid);
    menu_config = (KRect){0, 0, 0, 0};
    if (a->menu_group == K_GROUP_ALL && K_SETTINGS > 0) {   /* the page switcher: the settings too, top right (Config) */
        const int cw = g_text_w(GF_S, "Config") + ICON + 16, chh = g_text_h(GF_S) + 6;
        const int on = a->menu_selected == n;   /* (the cursor on it: after the pages) */
        menu_config = (KRect){ix + iw - cw, iy - 1, cw, chh};
        if (on) flat_fill(menu_config.x, menu_config.y, cw, chh, 5, th.sel);
        else g_rrect_frame(&cv, menu_config.x, menu_config.y, cw, chh, 5, k_mix(th.surf, th.text, 14), k_mix(th.surf, th.text, 5));
        g_icon(&cv, menu_config.x + 5, menu_config.y + (chh - ICON) / 2, ICON, GI_CONFIG, on ? th.ink : th.mid);
        g_text(&cv, menu_config.x + 5 + ICON + 4, menu_config.y + 3, GF_S, "Config", on ? th.ink : th.text);
    }
    menu_rows_n = n;
    if (menu_config.w && n < (int)(sizeof menu_rows / sizeof menu_rows[0])) { menu_rows[n] = menu_config; menu_rows_n = n + 1; }   /* (Config: the cursor's, after the pages) */
    for (i = 0; i < n; i++) {
        const KRect r = {ix + (i % cols) * (tw + gap), top + (i / cols) * (th_ + gap), tw, th_};
        const int sel = i == a->menu_selected, ch = isz + 4 + g_text_h(f), cy = r.y + (r.h - ch) / 2;
        menu_rows[i] = r;
        if (sel) flat_fill( r.x, r.y, r.w, r.h, 6, th.sel);
        else g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 6, k_mix(th.surf, th.text, 14), k_mix(th.surf, th.text, 5));
        if (r.h < ICON + g_text_h(f) + 8) {   /* a short tile (the Zero's): the icon at the left, the name beside it */
            g_icon2(&cv, r.x + 5, r.y + (r.h - ICON) / 2, ICON, page_icon(k_menu_page(a, i)), k_mix(sel ? th.accent : th.surf, sel ? th.ink : th.text, 25), sel ? th.ink : th.mid);
            g_text_box(&cv, r.x + 5 + ICON + 4, r.y, r.w - ICON - 12, r.h, f, track_page_name(a, k_menu_page(a, i)), sel ? th.ink : th.text, -1);
            continue;
        }
        g_icon2(&cv, r.x + (r.w - isz) / 2, cy, isz, page_icon(k_menu_page(a, i)), k_mix(sel ? th.accent : th.surf, sel ? th.ink : th.text, 25), sel ? th.ink : th.mid);
        g_text_box(&cv, r.x + 3, cy + isz + 4, r.w - 6, g_text_h(f), f, track_page_name(a, k_menu_page(a, i)), sel ? th.ink : th.text, 0);
    }
}
void page_menu(KApp *a)
{
    int ix = whole.x + PAD, iy = whole.y + PAD, iw = whole.w - 2 * PAD, ih = whole.h - 2 * PAD;
    int i, n = k_menu_count(a), top = iy + g_text_h(GF_S) + 8, rh = (iy + ih - top - n * 2) / (n + (a->menu_group == K_GROUP_ALL && K_SETTINGS > 0));   /* (Config: one row more) */
    if ((prof->input & K_IN_TOUCH) || rh < 18) { page_tiles(a); return; }   /* (a short screen, the Zero's: the rows would overlap: tiles) */
    a->menu_cols = 1;
    flat_box(whole);
    if (rh > 26) rh = 26;
    g_text(&cv, ix + 8, iy + 2, GF_S,
           a->menu_is_hold ? "HOLD, TURN, RELEASE" : k_group_name(a->menu_group), th.mid);
    menu_rows_n = n;
    if (a->menu_group == K_GROUP_ALL && K_SETTINGS > 0 && n < (int)(sizeof menu_rows / sizeof menu_rows[0])) {   /* Config: a row after the pages */
        const KRect r = {ix, top + n * (rh + 2), iw, rh};
        const int sel = a->menu_selected == n;
        if (sel) flat_fill(r.x, r.y, r.w, r.h, 5, th.sel);
        g_icon2(&cv, r.x + 10, r.y + (r.h - ICON) / 2, ICON, GI_CONFIG, k_mix(sel ? th.accent : th.surf, sel ? th.ink : th.text, 25), sel ? th.ink : th.mid);
        g_text_box(&cv, r.x + 10 + ICON + 8, r.y, r.w - 20 - ICON - 8, r.h, GF_M, "Config", sel ? th.ink : th.text, -1);
        menu_rows[n] = r; menu_rows_n = n + 1;
    }
    for (i = 0; i < n; i++) {
        KRect r = {ix, top + i * (rh + 2), iw, rh};
        int sel = i == a->menu_selected;
        menu_rows[i] = r;
        if (sel) flat_fill( r.x, r.y, r.w, r.h, 5, th.sel);
        g_icon2(&cv, r.x + 10, r.y + (r.h - ICON) / 2, ICON, page_icon(k_menu_page(a, i)), k_mix(sel ? th.accent : th.surf, sel ? th.ink : th.text, 25), sel ? th.ink : th.mid);
        g_text_box(&cv, r.x + 10 + ICON + 8, r.y, r.w - 20 - ICON - 8, r.h, GF_M, track_page_name(a, k_menu_page(a, i)), sel ? th.ink : th.text, -1);
    }
}
