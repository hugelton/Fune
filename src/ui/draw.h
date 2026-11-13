/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_DRAW_H
#define KREISEL_DRAW_H
#include <stddef.h>
#include "kreisel.h"
#include "theme.h"
#include "gfx.h"

/* A profile is what a device gives the UI: its screen, and the kinds of input it has (a note for now: the UI does not
 * change with them yet). knobs: how many rotary knobs. shown: how many parameter cards it shows at once (its knobs when
 * it has them; else what its width holds). */
/* The input adapters follow these: a device is only given the controls it has. KEYS: a music key bed; KEYBOARD: a text
 * keyboard (arrows, Enter); PAD: a D-pad and buttons; TOUCH: a touch screen or a pointer on it. */
/* K_IN_*: kreisel_plugin_ui.h (a plugin is told them too). A mouse: clicks as touches, its wheel turns what is under it */
#include "kreisel_plugin_ui.h"
typedef struct {
    const char *name;
    int w, h;                 /* the logical size, when the profile is a fixture with no physical screen */
    unsigned input;
    int knobs, shown;
    int px_w, px_h; float diag;   /* a real screen: its pixels and its diagonal in inches; the logical size is then in dp */
    unsigned char font;       /* K_FONT_*: how its text is drawn (ui_draw_init sets it) */
    const char *const *keys;  /* its own keys by what they do (K_KEY_*), as their caps say ("OCT+"), 0 where it has none: drawn as
                               * keys where the screen needs one; a device with none is given buttons to press instead */
    unsigned lay_add, lay_drop;   /* K_LAY_* its own, over what its size and inputs give (k_profile_layout): the FM-1's LEDs, ... */
} KProfile;
enum { K_KEY_ARM1, K_KEY_ARM2, K_KEY_ARM3, K_KEY_ARM4,   /* the four that arm or turn a row's things (the FM-1's K1 .. K4) */
       K_KEY_OK, K_KEY_CANCEL, K_KEY_SAVE, K_KEY_ROLES };   /* do it, let it go, SAVE */
enum { K_FONT_VECTOR, K_FONT_BITMAP };   /* outlines filled at any size (a device with a fast FPU); the baked bitmaps (the FM-1) */
const char *ui_param_name(KPage page, int index);
int ui_param_icon(KPage page, int index);   /* the icon of a page item (a GIcon), or -1: after Felucca 2.0, by what the item is */
const char *ui_item_name(const KApp *a, KPage page, int i);   /* what a card says: a page's own label, or by the effect in an insert */
int ui_item_icon(const KApp *a, KPage page, int i);
int ui_item_value(const KApp *a, KPage page, int i, char *b, size_t n, const char **unit);   /* the value as text; 0: not a number */
int ui_param_track(KPage page, int index);   /* the track a page item is for (1..), or 0: the mixer items run across the tracks */
typedef enum { K_TIER_S, K_TIER_M, K_TIER_L } KTier;   /* small: 240 to 400 dp wide; medium: about 500 to 700; large: 850 up */
const KProfile *k_profile_find(const char *name);     /* NULL if unknown */
/* any screen at run time (a device with no profile of its own yet, a window of any size): a logical size and its inputs (K_IN_*);
 * the one made last (one at a time). The layout follows from them as for the table's */
const KProfile *k_profile_custom(int w, int h, unsigned input);
unsigned k_profile_layout(const KProfile *p);          /* how its screen joins pages (K_LAY_*, for k_set_layout): wide ones pair ENV 1 and 2 */
const KProfile *k_profile_default(void);
int k_profile_count(void);
const KProfile *k_profile_at(int i);
/* The size the UI is laid out in: dp, where 1 dp is a pixel at 160 ppi (scale = ppi / 160). A fixture with no diagonal
 * is already in dp. */
void k_profile_size(const KProfile *profile, int *w, int *h);
float k_profile_scale(const KProfile *profile);       /* real pixels per dp; 1 for a fixture */
KTier k_profile_tier(const KProfile *profile);
/* Whether the screen shows a focus cursor: only a device with a D-pad, a keyboard or a touch screen moves one. A device
 * of knobs and keys (the FM-1) edits what a knob or a key is set to, so it has none; a menu fills its selected row. */
int k_profile_cursor(const KProfile *profile);

/* How the screen moves, for tuning by hand: how long a touched card takes to open and to shut, and how long it stays. */
typedef struct { unsigned open_ms, close_ms, hold_ms; } KFeel;
KFeel *ui_feel(void);

typedef struct { int32_t x, y, w, h; int initialized; } KCursorVisual;   /* the cursor's corners as they glide, in 1/256 px */
int ui_draw_init(const KProfile *profile, KThemeId theme);
void ui_draw_set_theme(KThemeId theme);
KThemeId ui_draw_theme(void);
/* draws the screen into the canvas; the platform shows it. now_ms is the platform's clock (the same one it gives the
 * core). Returns 1 while something is moving or waiting on the clock (the cursor gliding, a toast, a held page button):
 * the platform then redraws at once, else it can wait for input. */
int ui_draw(KApp *app, KCursorVisual *cursor, uint32_t now_ms);   /* 0 still; 1 moving (draw again at once); 2 a slow animation (again within ~100 ms) */
/* a platform may time the frame's parts: called at 0 (the start: the platform's own), 1 the ground drawn, 2 the header, 3 the page,
 * 4 its cards; 0 (none) by default */
extern void (*ui_mark)(int section);
const GCanvas *ui_draw_canvas(void);                  /* the last frame: 0xAARRGGBB, w x h of the profile (s > 1: pw x ph pixels) */
void ui_draw_shutdown(void);
void ui_draw_outline(int on);   /* (debugging) each part of the header in a red frame, its middle line blue */
int ui_draw_set_scale(int s);   /* the canvas at s pixels a unit (1..6: a desktop's high-density screen); the layout unchanged */
int ui_hit_header(int x, int y);                     /* the page name: opens the page menu */
int ui_hit_menu_index(const KApp *app, int x, int y);
void ui_tap(KApp *app, int x, int y, uint32_t ms);
void ui_roll_scroll(const KApp *app, int n);         /* ROLL's rows n semitones up by hand (a stick): kept until the note in hand moves */
int ui_format_desc(const KParamDesc *d, int v, char *b, size_t n, const char **unit);   /* a parameter's value as text, its unit (1: a number) */
void ui_wheel(KApp *app, int x, int y, int d, uint32_t ms);
int ui_reset_at(KApp *app, int x, int y, uint32_t ms);
int ui_hover(KApp *app, int x, int y, int down);
int ui_context(KApp *app, int x, int y, uint32_t ms);       /* a right click: the menu for what is under it; 1 opened */
int ui_popup_open(void);                                     /* the popup (that menu, an insert's list) is up */
int ui_popup_click(KApp *app, int x, int y, uint32_t ms);   /* a left click while it is up: a row, or closed */
void ui_popup_close(void);
int ui_edit(KApp *app, int op, uint32_t ms);
enum { KP_UP, KP_DOWN, KP_LEFT, KP_RIGHT, KP_A, KP_B, KP_X, KP_Y, KP_L, KP_R, KP_START, KP_SELECT, KP_L2, KP_R2 };   /* a pad's buttons (ui_pad) */
int ui_pad(KApp *app, int button, int down, uint32_t ms);   /* a pad's button down / up (the keys of a desktop come here too) */
int ui_pad_tick(KApp *app, uint32_t ms);   /* each frame: SELECT held, the menu at the cursor; 1 when it opened */   /* 0 copy, 1 paste, 2 clear: SONG's selection, DRUM's selected lane, else the value in hand */
int ui_popup_rows(KRect *out, int max);
int ui_tool_part(int k, KRect *r);   /* a desktop's open tool as last drawn: 0, 1 its sliders, 2 Cancel, 3 Apply; 0 when not there */   /* the popup's rows as last drawn (its count; 0 closed) */   /* a pointer at (x, y) (-1: gone), its button down: lit under it; 1 when that changed */   /* option-click: the value under the pointer at its default; 1 when one was */   /* a mouse wheel at x, y: d notches up (+) or down: the value under it turns */
/* a touch-only device's surface (draw_surface.c: keys, pads, macros, a game pad, play / rec / the octave, beside the page):
 * ui_surface_layout gives the page's box for a w x h screen (make the profile that size); ui_surface_init its canvas at s pixels
 * a unit; each frame, after ui_draw, ui_surface_frame is the whole screen to show; every finger goes to ui_finger (id the
 * platform's, phase 0 down 1 moved 2 up), which hands the first one on the page to ui_touch */
void ui_surface_layout(int w, int h, int *page_w, int *page_h);
int ui_surface_init(int s);
void ui_surface_set_desktop(int on);
int ui_surface_velocity(void);           /* the Velocity strip: a typed or clicked note's */
void ui_surface_velocity_delta(int d);
void ui_surface_key_held(int note, int on);   /* a note held on the computer's keys: lit on the surface's keyboard */   /* a desktop browser: the keys from C with their computer keys (z s x d c ..) */
const GCanvas *ui_surface_frame(KApp *app, uint32_t now_ms);
const GCanvas *ui_surface_canvas(void);
void ui_finger(KApp *app, int id, int phase, int x, int y, uint32_t ms);
int ui_surface_relayout(void);   /* 1 once after the drawer opened or shut: lay out again (ui_surface_layout, a new page profile) */
void ui_touch(KApp *app, int phase, int x, int y, uint32_t ms);   /* 0 down (a tap), 1 moved (a value under it slides), 2 up */       /* a tap on the screen: overlays, the header, a focus target (every platform) */
#endif
