/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The frame: the profiles, the UI's state and the layout of a page, the theme, the cursor's corners, and ui_draw (one
 * frame: the header, the page's picture, its cards, the overlays). The areas are in the other draw_*.c files (draw_int.h). */
#include "draw_int.h"

/* the profiles: one a target (its screen, its controls), from platforms/targets/ (tools/kbuild.py --profiles). Any other
 * screen: k_profile_custom (kreisel --size, --input) */
#include "profiles_gen.h"
static KProfile custom_prof;
const KProfile *k_profile_custom(int w, int h, unsigned input)
{
    custom_prof = (KProfile){"custom", w < 160 ? 160 : w, h < 120 ? 120 : h, input, 0, 4};
    return &custom_prof;
}
const KProfile *k_profile_find(const char *n)
{
    size_t i;
    for (i = 0; i < sizeof PROFILES / sizeof *PROFILES; i++)
        if (!strcmp(PROFILES[i].name, n)) return &PROFILES[i];
    return NULL;
}
int k_profile_cursor(const KProfile *p) { return (p->input & (K_IN_PAD | K_IN_KEYBOARD | K_IN_TOUCH)) != 0; }
float k_profile_scale(const KProfile *p)
{
    if (p->diag <= 0 || p->px_w <= 0) return 1.0f;
    return sqrtf((float)p->px_w * p->px_w + (float)p->px_h * p->px_h) / p->diag / 160.0f;
}
void k_profile_size(const KProfile *p, int *w, int *h)
{
#ifndef KREISEL_BITMAP_FONT   /* (a screen of a known density, in float; the FM-1's profile has none, and it has no FPU) */
    if (p->diag > 0 && p->px_w > 0) {
        const float s = k_profile_scale(p);
        *w = (int)(p->px_w / s + .5f);
        *h = (int)(p->px_h / s + .5f);
        return;
    }
#endif
    *w = p->w; *h = p->h;
}
KTier k_profile_tier(const KProfile *p)
{
    int w, h;
    k_profile_size(p, &w, &h);
    return w < 450 ? K_TIER_S : w < 780 ? K_TIER_M : K_TIER_L;
}
const KProfile *k_profile_default(void) { return &PROFILES[0]; }
int k_profile_count(void) { return (int)(sizeof PROFILES / sizeof *PROFILES); }
const KProfile *k_profile_at(int i) { return &PROFILES[i]; }

GCanvas cv;
KTheme th;
static KThemeId theme_id;
/* each track has a colour of its own: its chip in the header, its strip's label in the mixer, its row on HOME */
/* a menu's parts, flat (): its box a plain face with a faint edge, what is in hand a plain fill */
void flat_box(KRect r) { g_rrect_frame(&cv, r.x, r.y, r.w, r.h, 8, k_mix(th.bg, th.text, 14), th.surf); }
void flat_fill(int x, int y, int w, int h, int r, uint32_t col) { g_rrect(&cv, x, y, w, h, r, col); }
const KApp *hue_app;   /* the app being drawn: the tracks' colours are its */
static const uint32_t PALETTE[K_PALETTE] = {   /* the owner's table (2026-11-05): full colours, the panel shows them pale otherwise */
    0xFF0000, 0xFF7300, 0xFFDC00, 0x00FF46, 0x00AFFF, 0x006EFF, 0xCD00FF, 0xFF008C};
const char *const PALETTE_NAME[K_PALETTE] = {"Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink"};
uint32_t track_hue(int t)
{
    uint32_t c = PALETTE[hue_app ? hue_app->tcolor[t % K_TRACKS] % K_PALETTE : t % K_PALETTE];
    return c;
}
uint32_t card_open[K_ITEM_SLOTS];   /* how far each parameter's card is open, 0..65536 (Q16) */
uint32_t now, last_now;
KFeel feel = {90u, 160u, 1300u};
KFeel *ui_feel(void) { return &feel; }     /* the platform's clock, set at the start of ui_draw */
const KProfile *prof;
int PW, PH;         /* the canvas size */
static int line_y = 27;   /* (LINE_Y, set again by layout_for) the line under the header (lower on a page with a second header) */
KRect pic;          /* the panel of the picture */
KRect cards_area;   /* the row of cards under it */
KRect whole;        /* both: the lists and the song table use all of it */
int sk_c;   /* the FM-1's synth pages, header C (mockups/2026-11-12_header_v2.png): no preset line, the panel up to
                   * the header, the engine's bar and the little screen 10 px taller; ui_draw lifts `whole` 14 px for them */
const KApp *sk_app;   /* (the page's tabs, drawn in the little screen) */
KRect header_page;
KRect menu_rows[K_PAGE_COUNT];
KRect mix_strips[K_TRACKS];
KRect mix_ctl[K_TRACKS][6];
KRect mix_ins[K_TRACKS][2];   /* each strip's two insert slots (the big mixer): a desktop's click opens the effects (the popup) */   /* each strip's controls (the big mixer): fader travel, pan, B1, B2, M, S; drawn at mix_ctl_at */
uint32_t mix_ctl_at;
KRect sw_lane_tabs[K_LANES];   /* the Switch's DRUM: its lanes' tabs (switch_view_drum) */   /* the mixer's strips on a screen without cards (pic_mixer_wide): a touch picks the track */
#ifndef KREISEL_BITMAP_FONT
KRect sw_lane_rows[K_LANES];   /* (and its rows: not in the FM-1's build) */
#endif
/* what a tap can hit on the overlays (ui_tap): SAVE's slot rows (and which slot each is), its two sides, its four actions; the
 * settings' tabs and box */
KRect save_rows[K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS], save_tabs[2], save_cards[4], set_tabs[K_SETTING_TABS], set_box;
int16_t name_x0, name_y0, name_bx = -1, name_by;   /* NAME: where its letters start, and its Cancel and Save (-1 none: the device
                                                       * has keys for them), to touch; their boxes from there (name_box, name_btn) */
KRect name_box(int i) { return (KRect){(int16_t)(name_x0 + i * (NAME_BW + NAME_GAP)), name_y0, NAME_BW, 26}; }
KRect name_btn(int k) { return name_bx < 0 ? (KRect){0, 0, 0, 0} : (KRect){(int16_t)(name_bx + k * (NAME_BTN_W + 8)), name_by, NAME_BTN_W, 22}; }
int name_pending;   /* the letter in hand touched again: the next letter when it is let go unmoved */
int save_row_slot[K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS], save_rows_n;
int menu_rows_n;
KRect tools_box;   /* the tools' list (page_tools): a tap off it closes it */
KRect header_master;   /* a desktop's level knob in the header (K_IN_MOUSE) */
KRect menu_config;   /* the page switcher's Config (the tiles: a touch screen, a mouse): the settings */
KRect fx_arrows[K_FX_SLOTS][2];   /* FX: each slot's < and > (a pointer or a finger: the effect before, after) */
KRect header_preset;   /* the desktop's header: the preset's name (a click: the presets to pick from) */
KRect pat_chip[4];   /* a desktop's tools rail on SEQ: the patterns A .. D, a click picks one (page_tools) */
KRect tool_slider[2], tool_btn[2];   /* a desktop's open tool: its sliders (x, y), and Cancel, Apply (page_tools) */
KRect song_col_hdr[K_SONG_VIS], song_row_hdr[K_TRACKS];   /* SONG (a desktop): the scenes' numbers, the tracks' chips: a click selects the column, the row */
KRect lane_lab[K_LANES];   /* DRUM: the lanes' names (a click: that lane in hand, selected for a copy) */
KRect header_tools;   /* the header's TOOLS button (a touch screen, a page with tools): it opens and closes them */
KRect mix_fader;   /* the mixer's fader in hand: its handle's travel (y: 100, y + h: 0), drawn at mix_fader_at */
uint32_t mix_fader_at;

/* the ENV pages (the LFO page is not there yet) have a second header: the engine and the preset */
int phase_env_track(const KApp *a)
{
    return a && !k_track_missing(a, a->track) &&
        k_engine_uid(a->sound[a->track]) == K_UID('K','P','H','S');
}
const char *track_page_name(const KApp *a, KPage page)
{
    if (phase_env_track(a) && (page == K_PAGE_ENV1 || page == K_PAGE_ENV2)) return page == K_PAGE_ENV1 ? "DCO" : "DCA";   /* (PHASE's one page of
                                                 * DCO DCW DCA: ENV 1 opens it at DCO, ENV 2 at DCA; both were "EG", alike in the menu) */
    return k_page_name(page);
}
int drum_track(const KApp *a)   /* the track in hand plays Kreisel's DRUM (its ENV is its lane's, its LFO reaches a lane) */
{
    return k_engine_uid(a->sound[a->track]) == K_UID('D', 'R', 'U', 'M') && !k_track_missing(a, a->track);
}
static int panel_page(const KApp *a)   /* a synth page drawn as the panel (no second header, no cards): on the 240 x 240 screen, and
                                       * ENV 1 and 2 paired on a wide one */
{
    if (a && (a->lay & K_LAY_ENV_PAIR) && a->page == K_PAGE_ENV1 && PW >= 300 && PH <= 200) return 1;
    if (a && phase_env_track(a) && a->page == K_PAGE_ENV1) return 1;   /* (PHASE's three envelopes: no cards on any screen) */
    if (a && PW >= 300 && PH <= 200 && (a->page == K_PAGE_LFO || (a->page == K_PAGE_ENV1 && drum_track(a)))) return 1;   /* (the wide panel: wide_panel_view) */
    if (a && (a->lay & K_LAY_SYNTH_ALL) && a->page == K_PAGE_SOUND) return 1;   /* (the Switch's synth: switch_synth_view) */
    return a && PW == 240 && PH == 240 && (a->page == K_PAGE_ENV1 || a->page == K_PAGE_ENV2 || a->page == K_PAGE_LFO);
}
static int has_head2(const KApp *a)
{
    if (panel_page(a)) return 0;
    return a && (a->page == K_PAGE_ENV1 || a->page == K_PAGE_ENV2);   /* (the synth page has its engine row instead) */
}
/* a page drawn as its own controls, no cards under it: a screen with no knobs to stand for (the Switch), on the pages that have
 * such a view (the mixer's strips) */
int no_cards(const KApp *a) { return a && (a->page == K_PAGE_SONG || (a->page == K_PAGE_ROLL && (a->lay & K_LAY_ROLL_CELLS)) || (a->page == K_PAGE_MIXER && (a->lay & K_LAY_MIX_KEYS)) || (prof && !prof->knobs && cv.w >= 600 && (a->page == K_PAGE_MIXER || a->page == K_PAGE_FX))); }   /* (a big screen: every control its own target; SONG: none, its knobs are its tracks) */
/* the FX page on a screen with cards, the insert in hand an effect with a layout of its own for this screen (K_LPAGE_FX): its
 * widgets are its controls, no cards under them (as SOUND's with a plugin's layout) */
static int fx_own(const KApp *a)   /* the FX page: no cards on any device (): its slots' knobs are the controls, as SOUND's rows */
{
    return a && (a->page == K_PAGE_FX || a->page == K_PAGE_BUS1 || a->page == K_PAGE_BUS2) && !no_cards(a);   /* (BUS too) */
}
KRect rail;
int tools_rail(const KApp *a) { return a && PW >= 600 && (a->page == K_PAGE_STEP || a->page == K_PAGE_DRUM || a->page == K_PAGE_ROLL) && !panel_page(a); }
static void layout_for(const KApp *a)
{
    line_y = LINE_Y + (has_head2(a) ? HEAD2_H : 0);
    whole = (KRect){MARGIN, line_y + 3, PW - 2 * MARGIN, PH - (line_y + 3) - MARGIN};
    cards_area = (KRect){MARGIN, PH - MARGIN - CARD_H, PW - 2 * MARGIN, CARD_H};
    pic = (KRect){MARGIN, whole.y, whole.w, cards_area.y - 5 - whole.y - (cards_open(a) ? CARD_FULL - CARD_H + 1 : 0)};   /* (open cards rise into that room) */
    if (a && (a->page == K_PAGE_SOUND || a->page == K_PAGE_EDIT || panel_page(a) || no_cards(a) || fx_own(a))) pic = whole;   /* SOUND, EDIT: their rows are the controls, no cards under them */
    rail = (KRect){0, 0, 0, 0};
    if (tools_rail(a)) { rail = (KRect){pic.x + pic.w - RAIL_W, pic.y, RAIL_W, pic.h}; pic.w -= RAIL_W + 4; }   /* (the pattern beside it, all in sight) */
}
void ui_draw_set_theme(KThemeId id) { theme_id = id; k_theme(&th, id); }
KThemeId ui_draw_theme(void) { return theme_id; }
unsigned k_profile_layout(const KProfile *p)
{
    int pw, ph;
    k_profile_size(p, &pw, &ph);
    return (pw > ph ? K_LAY_ENV_PAIR : 0u) | (p->input & K_IN_KNOBS ? K_LAY_FX_ALGO : 0u) | (!p->knobs && pw >= 600 ? K_LAY_SYNTH_ALL : 0u) |
           (p->input == K_IN_KEYBOARD ? K_LAY_MIX_KEYS | K_LAY_ROLL_CELLS : 0u) |   /* (a keyboard alone: the Zero) */
           (p->input & K_IN_KNOBS ? 0u : p->input & K_IN_MOUSE ? K_LAY_SHEET_POINTER : K_LAY_SHEET_CURSOR) |   /* (the action sheets' kind: knobs, a pointer, a cursor) */
           ((K_LAY_CONNECT | p->lay_add) & ~p->lay_drop);   /* (and the profile's own: the FM-1's) */
}
int head_h = 24;
int ui_draw_init(const KProfile *p, KThemeId id)
{
    int pw, ph;
    prof = p;
    head_h = (p->input & K_IN_MOUSE) ? 32 : 24;   /* (a desktop: a thicker bar, its parts easier to click) */
    k_profile_size(p, &pw, &ph);
    if (!g_init(&cv, pw, ph)) return 0;
    ui_draw_set_theme(id);
    g_font_vector(p->font != K_FONT_BITMAP);   /* the device's text (a build with no vector faces keeps the bitmaps) */
    PW = pw; PH = ph;
    cards_area = (KRect){MARGIN, ph - MARGIN - CARD_H, pw - 2 * MARGIN, CARD_H};
    layout_for(NULL);
    k_host_set_screen(pw, ph, p->input, p->knobs);   /* (what a plugin is told: kreisel_plugin_ui.h) */
    return 1;
}
void ui_draw_shutdown(void)
{
    g_free(&cv);
}
/* a desktop's high-density screen: the canvas at s pixels a unit (the layout as it was); 1: back to a pixel a unit */
int ui_draw_set_scale(int s)
{
    GCanvas n;
    if (s < 1) s = 1;
    if (s > 6) s = 6;
    if (cv.px && (cv.s > 1 ? cv.s : 1) == s) return 1;
    if (!g_init_scaled(&n, PW, PH, s)) return 0;
    g_free(&cv);
    cv = n;
    return 1;
}
const GCanvas *ui_draw_canvas(void) { return &cv; }
int inside(KRect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
int ui_hit_header(int x, int y) { return inside((KRect){header_page.x - 6, 0, header_page.w + 12, HEAD_H}, x, y); }
int ui_hit_menu_index(const KApp *a, int x, int y)
{
    int i;
    if (!a->menu_open && !a->settings_open && !a->tools_open) return -1;   /* (the settings' and the tools' rows are kept in the same list) */
    for (i = 0; i < menu_rows_n; i++)
        if (inside(menu_rows[i], x, y)) return i;
    return -1;
}

/* -------------------------------------------------------------- cursor --- */
/* The row in hand on an engine's page: a thin frame round its four parts (the union of their bounds), quieter than the cursor's
 * corners, which still mark the one part in hand. Nothing else is dimmed. */
/* the rows of a panel: the row in hand is told by its labels' colour (sk_in_row); nothing over the panel */
static int sk_row_pulsing;
void sk_row_frame(KApp *a) { (void)a; sk_row_pulsing = 0; }
/* Four corners that glide to the focused item. */
static int draw_cursor(const KApp *a, KCursorVisual *c)   /* 1 while it is still gliding */
{
    KRect t = a->save_open ? save_target : a->settings_open && a->settings_sel < menu_rows_n ? menu_rows[a->settings_sel] :
              a->menu_open && a->menu_selected < menu_rows_n ? menu_rows[a->menu_selected] : k_focus_rect(a);
    if (pad.region == 1 && !a->save_open && !a->settings_open && !a->menu_open && pad.hsel >= 0 && pad.hsel < hdr_tn)   /* (a pad up in the header: its part) */
        t = (KRect){(int16_t)(hdr_tgt[pad.hsel].x - 3), (int16_t)(hdr_tgt[pad.hsel].y - 3), (int16_t)(hdr_tgt[pad.hsel].w + 6), (int16_t)(hdr_tgt[pad.hsel].h + 6)};
    int x, y, w, h, s = 5, al = 255, fading = 0, dx, dy, dw, dh;
    if (t.w <= 0) return 0;
    if (!c->initialized) { c->x = t.x * 256; c->y = t.y * 256; c->w = t.w * 256; c->h = t.h * 256; c->initialized = 1; }
    dx = t.x * 256 - c->x; dy = t.y * 256 - c->y; dw = t.w * 256 - c->w; dh = t.h * 256 - c->h;
    c->x += dx * 7 / 25; c->y += dy * 7 / 25; c->w += dw * 7 / 25; c->h += dh * 7 / 25;   /* 28 % of the way a frame (1/256 px) */
    if (cur_touch.hide && a->act_n != cur_touch.seen) cur_touch.hide = 0;   /* an input not the finger's: back at once */
    if (cur_touch.hide) {   /* touched: out in 250 ms (it still follows the focus, unseen: back where it belongs) */
        const uint32_t d = now - cur_touch.ms;
        al = d >= 250u ? 0 : 255 - (int)(d * 255u / 250u);
        fading = al > 0;
        if (!al) return 0;
    }
    x = ((c->x + 128) >> 8) - 1; y = ((c->y + 128) >> 8) - 1; w = ((c->w + 128) >> 8) + 2; h = ((c->h + 128) >> 8) + 2;
    if (al == 255) {   /* (at rest, as ever: plain fills, the FM-1's cost) */
        g_fill(&cv, x, y, s, 1, th.accent);           g_fill(&cv, x, y, 1, s, th.accent);
        g_fill(&cv, x + w - s, y, s, 1, th.accent);   g_fill(&cv, x + w - 1, y, 1, s, th.accent);
        g_fill(&cv, x, y + h - 1, s, 1, th.accent);   g_fill(&cv, x, y + h - s, 1, s, th.accent);
        g_fill(&cv, x + w - s, y + h - 1, s, 1, th.accent); g_fill(&cv, x + w - 1, y + h - s, 1, s, th.accent);
    } else {
        g_rrect_a(&cv, x, y, s, 1, 0, th.accent, al);           g_rrect_a(&cv, x, y, 1, s, 0, th.accent, al);
        g_rrect_a(&cv, x + w - s, y, s, 1, 0, th.accent, al);   g_rrect_a(&cv, x + w - 1, y, 1, s, 0, th.accent, al);
        g_rrect_a(&cv, x, y + h - 1, s, 1, 0, th.accent, al);   g_rrect_a(&cv, x, y + h - s, 1, s, 0, th.accent, al);
        g_rrect_a(&cv, x + w - s, y + h - 1, s, 1, 0, th.accent, al); g_rrect_a(&cv, x + w - 1, y + h - s, 1, s, 0, th.accent, al);
    }
    return fading || abs(t.x * 256 - c->x) > 77 || abs(t.y * 256 - c->y) > 77 || abs(t.w * 256 - c->w) > 77 || abs(t.h * 256 - c->h) > 77;   /* (more than 0.3 px from it) */
}

/* Kreisel's primary colour is blue: the identity colour (values, gauges, selections, the surfaces' tint) is that blue on every
 * page, whatever the engine; everything derived from it follows. (The drum lanes keep their own colours.) */
static void tint_theme(const KApp *a)
{
    uint32_t hue;
    k_theme(&th, theme_id);
    hue = 0xC2C6CC;   /* the app's grey (was cyan, before that blue 0x4C8DFF). Every page the same: a track's colour names a track, never a page's tint */
    (void)a;
    th.bg = 0x111214;   /* a near-black charcoal: the panels, menus and cards stand off it (;
                         * was Logic's 0x1C1D1F) (ink stays black: it is the text on colour) */
    th.theme = hue;
    th.sel = k_mix(th.bg, hue, 80);
    th.tint = k_mix(th.bg, 0xFFFFFF, 7);   /* the panels and menus a neutral grey, not blue ():
                                         * the blue only for what is in hand (sel) and the accent */
    th.surf = k_mix(th.bg, 0xFFFFFF, 5);
    th.raise = k_mix(th.surf, th.text, 12);
}
static int engine_panel(const KApp *a)   /* SOUND or EDIT is an engine's own panel (as ui_draw's dispatch below): it covers the picture */
{
    uint32_t u;
    if (cv.w != 240 || cv.h != 240 || k_track_missing(a, a->track) || (a->page != K_PAGE_SOUND && a->page != K_PAGE_EDIT)) return 0;
    u = k_engine_uid(a->sound[a->track]);
    return u == K_UID('K', 'A', 'N', 'L') || u == K_UID('K', 'L', 'O', 'F') || u == K_UID('K', 'T', 'R', 'I') || u == K_UID('K', 'P', 'H', 'S') ||
           u == K_UID('K', 'O', 'G', 'N') || u == K_UID('K', 'E', 'N', 'S') || u == K_UID('D', 'R', 'U', 'M');
}
static int ground_well(const KApp *a)   /* the page sits in a well over its picture (as ui_draw's switch below) */
{
    switch (a->page) {
    case K_PAGE_SOUND: case K_PAGE_EDIT: case K_PAGE_FX: case K_PAGE_MIXER: case K_PAGE_HOME: case K_PAGE_TRACKS: case K_PAGE_SONG: return 0;
    default: return !panel_page(a);
    }
}
/* a page the engine does without (k_page_unused: ORGAN's ENV and LFO): the well, and why it is empty */
static void pic_unused(KApp *a)
{
    char l1[32];
    const char *what = a->page == K_PAGE_LFO ? "LFO" : "ENV";
    snprintf(l1, sizeof l1, "This engine has no %s", what);
    g_fill(&cv, pic.x, pic.y, pic.w, pic.h, th.bg);   /* (the ground is not painted under a panel page: the last page would show) */
    if (cv.w == 240 && cv.h == 240 && !k_track_missing(a, a->track)) {   /* the FM-1: the engine's panel, as its other pages, the little screen saying so */
        KRect r;
        sk_frame(a);
        r = sk_screen(a->page == K_PAGE_LFO ? "LFO" : a->page == K_PAGE_ENV2 ? "ENV 2" : "ENV 1", 0, 0);
        g_text_box(&cv, r.x, r.y + 8, r.w, r.h - 8, GF_S, l1, k_mix(sk.lcd, SK_LCD_WAVE, 70), 0);
        return;
    }
    if (panel_page(a)) well(pic);
    g_text_box(&cv, pic.x, pic.y, pic.w, pic.h, GF_M, l1, th.text, 0);
}
void (*ui_mark)(int section);
int ui_draw(KApp *a, KCursorVisual *c, uint32_t now_ms)
{
    int busy;
    sk_row_pulsing = 0;
    plug_scope_on = 0;
    now = now_ms;
    hue_app = a;
    tint_theme(a);
    busy = cards_step(a) | (a->playing != 0);   /* the beat dots move while playing */
    layout_for(a);
    {   /* the ground: charcoal, a little lighter at the top (flat: the gradients are off). Not under the picture where the page
         * covers it opaquely (a panel; a well, but for its rounded corners, which blend over the ground): a full-screen fill saved */
        const uint32_t g0 = k_mix(th.bg, 0xFFFFFF, 3), g1 = k_mix(th.bg, 0x000000, 22);
        const int panel = panel_page(a) || engine_panel(a), covered = !a->save_open && !a->settings_open && !a->menu_open && (panel || ground_well(a)), in = panel ? 0 : 8;
        if (covered) g_fill_around(&cv, 0, 0, cv.w, cv.h, pic.x + in, pic.y + in, pic.w - 2 * in, pic.h - 2 * in, g_mid(g0, g1));
        else g_vgrad(&cv, 0, 0, cv.w, cv.h, g0, g1);
    }
    if (ui_mark) ui_mark(1);
    draw_header(a);
    if (ui_mark) ui_mark(2);
    if (has_head2(a)) draw_header2(a);
    if (has_head2(a)) g_fill(&cv, MARGIN, line_y, cv.w - 2 * MARGIN, 1, th.line);   /* and one under the second header (the synth's boxes need no line) */
    menu_rows_n = 0;
    if (a->save_open) {
        page_save(a);
    } else if (a->menu_open) {
        page_menu(a);
    } else {
        if (!panel_page(a)) switch (a->page) {   /* the pictures with data in them sit in a well (not a panel page) */
        case K_PAGE_SOUND: case K_PAGE_EDIT: case K_PAGE_FX: case K_PAGE_MIXER: case K_PAGE_HOME: case K_PAGE_TRACKS: case K_PAGE_SONG: break;
        default: well(pic); break;
        }
        sk_c = cv.w == 240 && cv.h == 240 && (a->page == K_PAGE_SOUND || a->page == K_PAGE_EDIT || a->page == K_PAGE_ENV1 || a->page == K_PAGE_ENV2 ||
                                               a->page == K_PAGE_LFO);   /* (header C: the panel lifted into the preset line's room) */
        if (sk_c) { whole.y -= 14; whole.h += 14; }
        sk_cw = 56; sk_kr = 10; sk_gap = 1; ko_step = 7; sk_side = 0;   /* (the FM-1's sizes; sk_geom lays them to another screen) */
        if (k_page_unused(a, a->page)) pic_unused(a); else
        switch (a->page) {
        case K_PAGE_HOME: pic_home(a); break;
        case K_PAGE_SONG: page_song(a); break;
        case K_PAGE_TRACKS: pic_tracks(a); break;
        case K_PAGE_STEP: if (a->rows_view) pic_rows(a); else pic_step(a); break;
        case K_PAGE_DRUM: if (a->rows_view) pic_rows(a); else pic_drum(a); break;
        case K_PAGE_ROLL: if (a->rows_view) pic_rows(a); else { pic_roll(a); busy |= roll_moving; } break;
        case K_PAGE_SOUND:   /* an engine's own view, by its id (the UI's, never the engine's code); else the vanilla one */
            {   uint32_t u = k_engine_uid(a->sound[a->track]);
                if (!plugin_layout_view(a) && !switch_synth_view(a) && !wide_panel_view(a) && !(u == K_UID('K', 'A', 'N', 'L') && synth_view_kanalog(a)) && !(u == K_UID('K', 'L', 'O', 'F') && lofi_view_sel(a)) &&
                    !(u == K_UID('K', 'T', 'R', 'I') && synth_view_ktrio(a)) && !(u == K_UID('K', 'P', 'H', 'S') && synth_view_kphase(a)) &&
                    !(u == K_UID('K', 'O', 'G', 'N') && synth_view_korgan(a)) && !(u == K_UID('K', 'E', 'N', 'S') && synth_view_kensemble(a)) &&
                    !(u == K_UID('K', 'F', 'M', '2') && synth_view_kfm2(a)) &&
                    !(u == K_UID('D', 'R', 'U', 'M') && drum_view_sel(a))) pic_synth_vanilla(a);
                else sk_row_frame(a); }
            break;
        case K_PAGE_EDIT: {   /* EDIT: each engine's own view, else the vanilla one */
            uint32_t u = k_engine_uid(a->sound[a->track]);
            if (!plugin_layout_view(a) && !wide_panel_view(a) && !(u == K_UID('K', 'A', 'N', 'L') && edit_view_kanalog(a)) && !(u == K_UID('K', 'L', 'O', 'F') && lofi_view_edit(a)) &&
                !(u == K_UID('K', 'T', 'R', 'I') && edit_view_ktrio(a)) && !(u == K_UID('K', 'P', 'H', 'S') && edit_view_kphase(a)) &&
                !(u == K_UID('K', 'O', 'G', 'N') && edit_view_korgan(a)) && !(u == K_UID('K', 'E', 'N', 'S') && edit_view_kensemble(a)) &&
                !(u == K_UID('K', 'F', 'M', '2') && edit_view_kfm2(a)) &&
                !(u == K_UID('D', 'R', 'U', 'M') && drum_view_edit(a))) pic_synth_vanilla(a);
            else sk_row_frame(a);
            break; }
        case K_PAGE_ENV1: if (!phase_eg_view(a) && !wide_panel_view(a) && !env_pair_view(a) && !drum_env_view(a) && !env1_view(a)) pic_env(a, 0); break;
        case K_PAGE_ENV2: if (!env2_view(a)) pic_env(a, 1); break;
        case K_PAGE_MIXER: pic_mixer(a); break;
        case K_PAGE_BUS1: case K_PAGE_BUS2: if (no_cards(a)) pic_levels(a, 0); else pic_bus(a); break;
        case K_PAGE_FX: pic_fx(a); break;
        case K_PAGE_LFO: if (!wide_panel_view(a) && !lfo_view(a)) pic_lfo(a); break;
        case K_PAGE_ARP: pic_arp(a); busy |= a->arp_n[a->track] && now - a->arp_ms[a->track] < 2100u; break;   /* (the arp playing: drawn as it plays) */
        case K_PAGE_SCL: pic_scl(a); break;
        default: pic_levels(a, 1); break;
        }
        if (sk_c) { whole.y += 14; whole.h -= 14; sk_c = 0; }
        if (ui_mark) ui_mark(3);
        if (a->page != K_PAGE_SOUND && a->page != K_PAGE_EDIT && !panel_page(a) && !k_page_unused(a, a->page) && !no_cards(a) && !fx_own(a)) cards(a);   /* (the mixer's cards are the selected track's LEVEL, PAN and sends; SYNTH's are its rows) */
    }
    if (ui_mark) ui_mark(4);
    if (k_profile_cursor(prof) && !a->tools_open) busy |= draw_cursor(a, c);
    if (a->boom && a->boom_ms) {   /* Dynamite: black and white rings from the page's middle over the whole screen (after Kid Pix) */
        const int t = (int)(now_ms - a->boom_ms) > (int)K_BOOM_MS ? (int)K_BOOM_MS : (int)(now_ms - a->boom_ms), band = cv.w >= 400 ? 14 : 9;
        const int cx = whole.x + whole.w / 2, cy = whole.y + whole.h / 2, rmax = (cv.w + cv.h) * 6 / 10 + band;
        const int R = rmax * t / (int)K_BOOM_MS, shift = (t / 40) & 1;   /* (growing; its bands trading places as it goes) */
        int r, k = 0;
        for (r = R; r > 0; r -= band, k++)
            g_rrect(&cv, cx - r, cy - r, 2 * r, 2 * r, r, ((k + shift) & 1) ? 0xFFFFFF : 0x000000);
        busy = 1;
    } else if (a->boom) busy = 1;
    if ((a->tools_open || rail.w) && !a->menu_open && !a->save_open) page_tools(a);   /* over the page, the pattern left in sight (a big screen: always) */
    if (!ctx.open) draw_hover(a);   /* (a pointer: what it is over, lit; over the tools too) */
    if (a->settings_open) page_settings(a);   /* a modal over the page */
    draw_hint(a);   /* over everything, the cursor too */
    draw_balloon(a);   /* (the others: from the top, the same box) */
    draw_save_ask(a);   /* (a menu fills its selected row, cursor or not) */
    draw_popup();   /* (a desktop's right click, an insert's list: over everything) */
    draw_tempo_pop(a);
    busy |= a->group_held >= 0 || k_balloon_active(&a->balloon, now);   /* a hold waits on the clock; a popup fades, and one frame more draws it gone */
    if (!busy && (sk_row_pulsing || plug_scope_on || a->out_lv[0] || a->out_lv[1])) return 2;   /* only the row in hand breathing, or the meter falling: a slow
                                                 * animation (a frame every 100 ms will do) */
    return busy;
}
