/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What the UI's drawing files share (src/ui/draw*.c, split from one draw.c): the includes, the layout's sizes, the canvas,
 * the theme and the profile, the layout's rectangles, the focus and touch targets kept for input, and the helpers each area
 * calls in another. Internal to the UI: the platforms see draw.h. */
#ifndef KREISEL_DRAW_INT_H
#define KREISEL_DRAW_INT_H
#include "draw.h"
#include "kreisel_save.h"
#include "kreisel_engine.h"
#include "kreisel_plugin.h"
#include "kreisel_arp.h"
#include "kreisel_audio.h"
#include "gfx.h"
#include "gfx_rec.h"   /* (the pills recorded in a G_RECORD build: tools/ui_pill_check.py) */
#include <math.h>
#include "kreisel_math.h"
#include "kreisel_mul.h"   /* (the FM-1: no 64-bit multiply inlined beside a 16-bit load, as the values are now) */
#include "kreisel_level.h"
#include "kreisel_engine_names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The screen is a header, a picture and a row of cards. The picture shows what the page edits (the steps, the wave, the
 * shape of an envelope); the cards are its parameters, small. Everything is placed from the screen size, not from a
 * device's knobs, so a narrower or wider profile just changes the grid. Panels and cards are flat: a SURF fill and a
 * 1 px edge. */
extern int head_h;   /* the header's height: 24, a desktop's 32 (thicker; ui_draw_init) */
#define HEAD_H head_h
#define HEAD2_H 28      /* the second header (a line above it, a line under it): the engine and the preset, on the synth pages */
#define LINE_Y (HEAD_H + 3)   /* the line under the header (27); the content starts 3 below it, and ends 2 above the line over the cards */
#define MARGIN 3
#define PAD 6
#define PIC_PAD 2       /* the picture has no box: it runs to the screen's margins, as the cards and the header do */
#define GAP 3
#define CARD_H 24       /* a card at rest: its label (centred) and a gauge, no box */
#define CARD_FULL 44    /* a card touched: the value and unit show too, growing upward over the picture */
#define NAME_BW 18
#define NAME_GAP 2
#define NAME_BTN_W 76
/* a big screen (the Switch's, a desktop's): the tools always shown, a column at the right of STEP, DRUM and ROLL */
#define RAIL_W 150
#ifdef KREISEL_BITMAP_FONT   /* (the FM-1: no pointer, no popup; its RAM is counted) */
#define CTX_ROWS 1
#else
#define CTX_ROWS 48
#endif

#define ICON 12
#define TICON 18        /* play and stop in the header: the triangle fills 45 % of its box across and 65 % down (smaller than 22) */
#define PICON 16        /* the page icon in the header */
#define BAT 28          /* the battery icon box in the header: the glyph fills 73 % of it across and 40 % down */
#define MENU_VEIL 210   /* a menu or a question over the page: how dark the page goes behind it (of 255) */
#define TOOLS_VEIL 150  /* the tools' list: less, the pattern a tool is tried on still to be seen */
enum { HT_TRACK, HT_PLAY, HT_PRESET, HT_TEMPO, HT_MASTER, HT_TOOLS, HT_PAGE, HT_CLOCK };
/* the scales' semitones (bit i: the note i above the root is in it), as SCL names them: CHR MAJ MIN DOR MIX PEN MPEN HARM PHRY LYD LOC MEL BLUES WHOLE DIMHW DIMWH */
#define MASK k_scale_masks   /* (kreisel_seqtool.c) */
/* an engine's skin: the colours of its panel (the UI's, by the engine's id; never the engine's code). ENV, LFO and EDIT take the
 * skin of the track's engine. ANALOG black and white with amber screens and wooden cheeks, after the classic monophonic panel;
 * TRIO (its sound chip style) a white case, red-capped knobs, a grey screen and a pastel rainbow; PHASE (phase distortion) the
 * blue-grey panel with yellow screens; ORGAN all wood (wood 2: the grain over the whole panel); another engine the blue-grey with
 * amber screens. */
typedef struct {
    uint32_t panel, out, face, edge, ring, arc, mark, ink, bar, lcd, lcd_edge, line;
    uint8_t wood, rainbow;
} KSkin;
#define SK_PANEL sk.panel
#define SK_OUT sk.out
#define SK_WOOD 0x5C3B24
#define SK_LCD_WAVE sk.line
#define SK_ARC sk.arc
#define SK_INK sk.ink
#define LOG2E_Q30 1549082005   /* log2(e) */
#define NES_WHITE 0xFCFCFC
#define KO_WELL 0x1C1714   /* the drawbars' well */
/* part: a control as label, knob and value one over the other (its label, its value with the unit, where the knob points) */
typedef struct { const char *label; char shown[24]; int v, sw, wave; } KnobText;   /* sw: a two-way switch's state (0 / 1), -1 a knob; wave: a
                                                                         * value that names a wave (0 sine, 1 triangle, 2 saw, 3 square, 4 S&H), -1 none */

/* (the parts shared between the files) */
#if defined(__GNUC__) && !defined(_WIN32)
#pragma GCC visibility push(hidden)
#endif
/* the state the files share, by the file that defines it */
/* draw.c */
extern GCanvas cv;
extern KTheme th;
extern const KApp *hue_app;
extern const char *const PALETTE_NAME[K_PALETTE];
extern uint32_t card_open[K_ITEM_SLOTS];
extern uint32_t now, last_now;
extern KFeel feel;
extern const KProfile *prof;
extern int PW, PH;
extern KRect pic;
extern KRect cards_area;
extern KRect whole;
extern int sk_c;
extern const KApp *sk_app;
extern KRect header_page;
extern KRect menu_rows[K_PAGE_COUNT];
extern KRect mix_strips[K_TRACKS];
extern KRect mix_ctl[K_TRACKS][6];
extern KRect mix_ins[K_TRACKS][2];
extern uint32_t mix_ctl_at;
extern KRect sw_lane_tabs[K_LANES];
extern KRect sw_lane_rows[K_LANES];
extern KRect save_rows[K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS], save_tabs[2], save_cards[4], set_tabs[K_SETTING_TABS], set_box;
extern int16_t name_x0, name_y0, name_bx, name_by;
extern int name_pending;
extern int save_row_slot[K_USER_PRESETS > K_SONG_SLOTS ? K_USER_PRESETS : K_SONG_SLOTS], save_rows_n;
extern int menu_rows_n;
extern KRect tools_box;
extern KRect header_master;
extern KRect menu_config;
extern KRect fx_arrows[K_FX_SLOTS][2];
extern KRect header_preset;
extern KRect pat_chip[4];
extern KRect tool_slider[2], tool_btn[2];
extern KRect song_col_hdr[K_SONG_VIS], song_row_hdr[K_TRACKS];   /* (the window's columns: scene a->song_off + i) */
extern KRect lane_lab[K_LANES];
extern KRect header_tools;
extern KRect mix_fader;
extern uint32_t mix_fader_at;
extern KRect rail;
/* draw_input.c */
/* the cursor under a finger: a touch fades it out (the finger is the pointer); any other input (a pad's, a key's: an action not the
 * touch's own) brings it back at once (draw_cursor) */
extern struct cur_touch_s { int hide; uint32_t ms, seen; } cur_touch;
extern struct pad_s { int region, hsel, a_down, a_used, sel_down, sel_fired; uint32_t sel_ms; } pad;   /* a pad: region 1 the header, its part hsel */
extern struct ctx_s {
    int open, x, y, n, page, item, track, slot, psel;   /* psel: the row a pad (or the keys) stands on */
    KRect box, rows[CTX_ROWS];
    struct { char label[24]; int act, arg, on, icon; } it[CTX_ROWS];
} ctx;
/* draw_header.c */
extern KRect hdr_tgt[8]; extern int hdr_kind[8], hdr_tn;
/* draw_seq.c */
extern int roll_lo, roll_trk, roll_moving, roll_manual;
/* the piano roll under a finger or a pointer, as a DAW's: down on an empty cell draws a note there (the last length, sliding right
 * makes it longer); down on a note takes it, sliding moves it in time and pitch, from its right end sizes it, let go unmoved
 * deletes it; down on the left edge (the C names) and sliding scrolls the rows. The note heard while it is held. A tap alone (the
 * web's, phase -1): a note on an empty cell, none on a note. Not on a drum track (its roll is DRUM) or under an overlay. */
extern struct rd_s { int mode, slot, grab, start, moved, note, y0, lo0; } rd;   /* mode 0 none, 1 moving, 2 sizing, 3 drawn, 4 scrolling */
/* draw_synth.c */
extern int plug_scope_on;
extern int plug_fx, plug_fx_lit;
/* draw_skin.c */
extern KSkin sk;
extern int sk_kr;
extern int sk_gap;
extern int sk_base;
extern int sk_cw;
extern int ko_step;
/* draw_sheets.c */
extern KRect save_target;
/* the drawing and input the files share, by the file that defines it */
/* draw.c */
void flat_box(KRect r);
void flat_fill(int x, int y, int w, int h, int r, uint32_t col);
uint32_t track_hue(int t);
KRect name_box(int i);
KRect name_btn(int k);
int phase_env_track(const KApp *a);
const char *track_page_name(const KApp *a, KPage page);
int drum_track(const KApp *a);
int no_cards(const KApp *a);
int tools_rail(const KApp *a);
int inside(KRect r, int x, int y);
void sk_row_frame(KApp *a);
/* draw_input.c */
void draw_popup(void);
void draw_tempo_pop(const KApp *a);   /* a desktop's tempo balloon: the tempo and the swing as sliders */
int ui_tempo_open(void);
void draw_hover(KApp *a);
/* draw_labels.c */
int phase_env_ui(const KApp *a);
int engine_icon(int k);
const char *preset_name(int engine, int track);
const char *gone_word(const KApp *a, int t);
const char *trk_engine(const KApp *a, int t);
const char *trk_preset(const KApp *a, int t);
int preset_icon(const KApp *a, int t, int x, int cy, int size, uint32_t col);
const char *fx_name(int type);
int fx_icon(int type);
int fx_type(const KApp *a, int i);
uint32_t fx_accent(int type);
void db_text(int v, char *b, size_t n);
GIcon page_icon(KPage p);
/* draw_header.c */
void draw_header2(const KApp *a);
void draw_balloon(const KApp *a);
void draw_hint(const KApp *a);
int keycap(int x, int y, const char *label, int lit);
int keycap_w(const char *label);
int keycap_n(int x, int y, const char *label, int lit);
const char *key_name(int role);
void draw_save_ask(const KApp *a);
void draw_header(const KApp *a);
/* draw_cards.c */
void face(int x, int y, int w, int h, int r, uint32_t col, int light, int dark);
void panel_box(KRect r);
void ghost_box(int x, int y, int w, int h);
void knob(int cx, int cy, int r, int v, int bip, uint32_t arc, uint32_t ring, uint32_t mark);
void slot(int cx, int y, int h);
void well(KRect r);
void curve_glow16(const int *p16, int n, uint32_t col);
int touched_recent(const KApp *a, int i);
int cards_open(const KApp *a);
int card_wanted(const KApp *a, int i);
int hot_param(const KApp *a, int i);
const char *card_label(const char *s, char *out, size_t n);
int mod_unused(const KApp *a, KPage pg, int i);
void cards(KApp *a);
int cards_step(const KApp *a);
/* draw_seq.c */
int step_top(const KApp *a, int t, int pat, int st);
void pic_step(KApp *a);
const char *lane_label(const KApp *a, int t, int l);
void pic_drum(KApp *a);
int table_val(const KApp *a, KPage page, int i);
void pic_roll(KApp *a);
void pic_rows(KApp *a);
int roll_touch(KApp *a, int phase, int x, int y, uint32_t ms);   /* the piano roll's own (pic_roll): 1 when it took the touch */
/* draw_synth.c */
void plug_layout(KApp *a, const KLayout *L, KRect area, int mode);
int plugin_layout_view(KApp *a);
void preset_line(const KApp *a, int x, int y, int xr);
void pic_synth_vanilla(KApp *a);
/* draw_skin.c */
void sk_skin(const KApp *a);
uint32_t sk_row_text(void);
void sk_pill(int px, int py, int pw, int tw, int bw, const char *label);
void sk_control(KApp *a, KPage pg, int i, int cx, int ly, int ky, const char *label, int lit);
int lfo_wave16(int kind, int cyc, int f);
void sk_line16(const int *p16, int n, int lit);
int sk_wave_i(int kind, int w, uint32_t t, uint32_t dcw);
int lrdiv(int n, int d);
void sk_wave_w(KApp *a, int i, int x, int ly, int y, int w, const char *label, int lit, int kind, int dcw);
KRect sk_frame(const KApp *a);
/* the engines' own views laid out on a screen (sk_geom): the FM-1's 240 x 240 as it was hand-laid, and a 320-wide screen
 * (the Cardputer Zero's 320 x 170, the H700's 320 x 240) from its size, so the same views, labels, little screens and switches
 * show there. Columns at SK_CX(c), a row's labels at SK_LY(r) and its knobs' centres at SK_KY(r); a little screen in a column
 * at SK_WX(c), SK_WW wide, sg_wd under its label and sg_wh tall; the engine's bar at sg_bar_y, sg_bar_h tall */
extern int sg_x0, sg_cw, sg_y0, sg_rp, sg_kd, sg_wd, sg_wh, sg_ww, sg_bar_y, sg_bar_h, sg_mid, sg_lane_h, sg_fm1;
int sk_geom(void);
extern int sk_side;   /* 1: this screen takes the engines' own views (and their geometry is set); 0 another view */
#define SK_CX(c) (whole.x + sg_x0 + sg_cw * (c))
#define SK_LY(r) (whole.y + sg_y0 + sg_rp * (r))
#define SK_KY(r) (SK_LY(r) + sg_kd)
#define SK_WX(c) (SK_CX(c) - sg_ww / 2)
#define SK_WW sg_ww
const char *wcase(const char *s);
KRect sk_screen(const char *tag, const char *right, int lit);
void sk_rows(KApp *a, KPage pg, const char *const *L, int n);
void sk_engine_bar(KApp *a, int row);
void sk_switch_v(KApp *a, KPage pg, int i, int cx, int ly, const char *label);
int synth_view_kanalog(KApp *a);
uint32_t lofi_dim(uint32_t c, int lit);
void lofi_item_small(KApp *a, KPage pg, int i, int cx, int y, const char *label, uint32_t col, int lit);
void lofi_scope(const KVal *P, int x0, int y0, int w, int h, uint32_t col);
int lofi_view_sel(KApp *a);
int lofi_view_edit(KApp *a);
void filter_pts16(int ft, int c, int r, int x0, int w, int yb, int hh, int *p16);
int edit_view_kanalog(KApp *a);
int edit_view_ktrio(KApp *a);
void ko_drawbar(KApp *a, int i, int cx, int y0, int lit);
void sk_switch_at(KApp *a, KPage pg, int i, int lx, int ly, int x, int ty, int w, const char *label, const char *const *shown, int lit);
void sk_switch_h(KApp *a, KPage pg, int i, int x, int ly, int w, const char *label, const char *const *shown, int lit);
void ko_vibcho_at(KApp *a, int i, int lx, int ly, int x, int ty, int w, int th);
void ko_vibcho(KApp *a, int i, int x, int ly, int lit);
void wide_head(KApp *a, KPage pg, int row, int mx, const KEngine *en, int t);
/* draw_wide.c */
int switch_synth_view(KApp *a);
int synth_view_korgan_wide(KApp *a);
int wide_panel_view(KApp *a);
/* draw_engines.c */
int synth_view_korgan(KApp *a);
int edit_view_korgan(KApp *a);
int drum_view_sel(KApp *a);
int drum_view_edit(KApp *a);
int drum_env_view(KApp *a);
int synth_view_ktrio(KApp *a);
int synth_view_kphase(KApp *a);
int edit_view_kphase(KApp *a);
int synth_view_kensemble(KApp *a);
int edit_view_kensemble(KApp *a);
int synth_view_kfm2(KApp *a);
int edit_view_kfm2(KApp *a);
void env1_curve(const KVal *E, KRect r, int lit);
int phase_eg_view(KApp *a);
int env1_view(KApp *a);
int env2_view(KApp *a);
int env_pair_view(KApp *a);
int lfo_view(KApp *a);
void pic_env(KApp *a, int second);
/* draw_mixer.c */
void pic_levels(KApp *a, int faders);
void text_fit(int x, int y, GFont f, const char *str, int maxw, uint32_t col);
void pic_tracks(KApp *a);
void pic_home(KApp *a);
void pic_mixer(KApp *a);
int fx_slot_controls(const KApp *a, int slot, int *items, int max);
int fx_type_item(const KApp *a, int slot);
void knob_text(const KApp *a, KPage page, int item, KnobText *k);
void knob_stack(const KnobText *k, int x, int w, int lh, int ly, int cy, int r, int vy, uint32_t hue, int lit, uint32_t lcol, uint32_t vcol);
void pic_bus(KApp *a);
/* draw_fx.c */
void pic_fx(KApp *a);
void pic_lfo(KApp *a);
void pic_arp(KApp *a);
void pic_scl(KApp *a);
/* draw_song.c */
void page_song(KApp *a);
/* draw_sheets.c */
void page_save(const KApp *a);
void page_settings(const KApp *a);
void page_tools(const KApp *a);
void page_menu(KApp *a);
#if defined(__GNUC__) && !defined(_WIN32)
#pragma GCC visibility pop
#endif
#endif
