/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "panel.h"
#include "draw.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { W = 340, H = 240 };
typedef enum { C_KNOB, C_BTN, C_KEY } CKind;
enum { KN_MASTER, KN_SELECT, KN_PRESETS, KN_ALGO, KN_K1, KN_COUNT = KN_K1 + 4 };
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTM, B_OCTP, B_COUNT };
#define NWHITE 16        /* the FM-1 has 27 keys: the 16 white ones are the steps of the page ... */
#define NBLACK 11        /* ... and the 11 black ones pick the lane of a drum page (10 lanes) */
#define NKEYS (NWHITE + NBLACK)
static const int BLACK_AFTER[NBLACK] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each black one follows */

typedef struct { CKind kind; int id; KRect r; const char *label; } Ctl;
static Ctl ctl[KN_COUNT + B_COUNT + NKEYS];
static int nctl;
static float angle[KN_COUNT];
static int hover = -1, held = -1, origin = -1;

/* a page button opens a page family (a short press its first page, a hold its list) or does one thing */
static const struct { const char *label; int group; } BTN[B_COUNT] = {
    {"FX", K_GROUP_FX}, {"SEL", K_GROUP_SOUND}, {"ENV", K_GROUP_ENV}, {"LFO", K_GROUP_LFO}, {"EDIT", K_GROUP_EDIT}, {"GLO", K_GROUP_GLO},
    {"HOME", -1}, {"SAVE", -1}, {"ARP", K_GROUP_ARP}, {"SEQ", K_GROUP_PATTERN}, {"PLAY", -1}, {"REC", -1}, {"OCT-", -1}, {"OCT+", -1},
};
static const char *KNOB_LABEL[KN_COUNT] = {"MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"};

static GCanvas cv;
static SDL_Texture *tex;

static void add(CKind k, int id, int x, int y, int w, int h, const char *label)
{
    ctl[nctl++] = (Ctl){k, id, {x, y, w, h}, label};
}
static char keylabel[NKEYS][10];
static void layout(void)
{
    int i;
    nctl = 0;
    for (i = 0; i < NWHITE; i++) snprintf(keylabel[i], sizeof keylabel[i], "KEY %d", i + 1);
    for (i = 0; i < NBLACK; i++) snprintf(keylabel[NWHITE + i], sizeof keylabel[0], "BLACK %d", i + 1);
    for (i = 0; i < KN_COUNT; i++)
        add(C_KNOB, i, 12 + (i % 4) * 80 + 20, 12 + (i / 4) * 56, 28, 28, KNOB_LABEL[i]);
    for (i = 0; i < 12; i++)
        add(C_BTN, i, 10 + (i % 6) * 54, 122 + (i / 6) * 26, 50, 22, BTN[i].label);
    add(C_BTN, B_OCTM, 10, 180, 50, 22, BTN[B_OCTM].label);
    add(C_BTN, B_OCTP, 64, 180, 50, 22, BTN[B_OCTP].label);
    for (i = 0; i < NWHITE; i++)
        add(C_KEY, i, 10 + i * 20, 208, 19, 28, keylabel[i]);
    for (i = 0; i < NBLACK; i++)   /* last: a black key is above the white ones, so it is found first */
        add(C_KEY, NWHITE + i, 10 + (BLACK_AFTER[i] + 1) * 20 - 6, 208, 12, 17, keylabel[NWHITE + i]);
}

int panel_init(SDL_Renderer *r, const char *name, int x0)
{
    if (strcmp(name, "fm1")) return 0;
    if (!g_init(&cv, W, H)) return 0;
    tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    layout();
    origin = x0;
    return tex != NULL;
}
void panel_shutdown(void)
{
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
    g_free(&cv);
    origin = -1;
}
int panel_origin(void) { return origin; }
int panel_w(void) { return W; }
int panel_h(void) { return H; }

static int find(int x, int y)
{
    int i;
    for (i = nctl - 1; i >= 0; i--) {
        KRect r = ctl[i].r;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}
void panel_hover(int x, int y) { hover = find(x, y); }
int panel_find(const char *label, int *x, int *y)
{
    int i;
    for (i = 0; i < nctl; i++)
        if (ctl[i].label && !strcmp(ctl[i].label, label)) {
            *x = ctl[i].r.x + ctl[i].r.w / 2;
            *y = ctl[i].r.y + ctl[i].r.h / 2;
            return 1;
        }
    return 0;
}

static void send(KApp *a, KActionType t, int x, int y, uint32_t ms) { k_dispatch(a, (KAction){t, x, y, ms}); }

static int group_of_page(KPage p)
{
    return K_IS_HOME(p) ? -1 : p == K_PAGE_STEP || p == K_PAGE_DRUM || p == K_PAGE_ROLL ? K_GROUP_PATTERN : p == K_PAGE_EDIT ? K_GROUP_EDIT : p == K_PAGE_MIXER || p == K_PAGE_SONG || p == K_PAGE_BUS1 || p == K_PAGE_BUS2 ? K_GROUP_GLO : p == K_PAGE_LFO ? K_GROUP_LFO : p == K_PAGE_ARP || p == K_PAGE_SCL ? K_GROUP_ARP : p == K_PAGE_SOUND ? K_GROUP_SOUND :
           p == K_PAGE_FX ? K_GROUP_FX : K_GROUP_ENV;
}

int panel_press(KApp *a, int x, int y, uint32_t ms)
{
    int i = find(x, y);
    if (i < 0) return 0;
    held = i;
    if (ctl[i].kind == C_KEY) {
        send(a, K_ACT_KEY, ctl[i].id, 1, ms);   /* the page decides: steps, lanes, or a note on ROLL */
    } else if (ctl[i].kind == C_BTN) {
        int id = ctl[i].id;
        if (BTN[id].group >= 0) send(a, K_ACT_PAGE_GROUP_DOWN, BTN[id].group, 0, ms);
        else if (id == B_HOME) send(a, K_ACT_HOME_DOWN, 0, 0, ms);   /* short: HOME; held: the settings */
        else if (id == B_SAVE) send(a, K_ACT_SAVE, 0, 0, ms);
        else if (id == B_PLAY) send(a, K_ACT_PLAY, 0, 0, ms);
        else if (id == B_REC) send(a, K_ACT_REC, 0, 0, ms);
        else if (id == B_OCTM) send(a, K_ACT_OCTAVE_DELTA, -1, 0, ms);
        else if (id == B_OCTP) send(a, K_ACT_OCTAVE_DELTA, 1, 0, ms);
    }
    return 1;
}
void panel_release(KApp *a, uint32_t ms)
{
    if (held >= 0 && ctl[held].kind == C_BTN && BTN[ctl[held].id].group >= 0)
        send(a, K_ACT_PAGE_GROUP_UP, BTN[ctl[held].id].group, 0, ms);
    if (held >= 0 && ctl[held].kind == C_KEY) send(a, K_ACT_KEY, ctl[held].id, 0, ms);
    if (held >= 0 && ctl[held].kind == C_BTN && ctl[held].id == B_HOME) send(a, K_ACT_HOME_UP, 0, 0, ms);
    held = -1;
}
int panel_wheel(KApp *a, int x, int y, int dy, int fine, uint32_t ms)
{
    int i = find(x, y), d = dy > 0 ? 1 : -1, step = fine ? 1 : 2;
    if (i < 0 || ctl[i].kind != C_KNOB || !dy) return 0;
    angle[ctl[i].id] += (float)d * 0.35f;
    switch (ctl[i].id) {
    case KN_MASTER: send(a, K_ACT_MASTER_DELTA, d * step, 0, ms); break;
    case KN_SELECT: send(a, K_ACT_SELECT_DELTA, d, 0, ms); break;   /* SELECT: the cursor; HOME held: the track */
    case KN_PRESETS: send(a, K_ACT_SOUND_DELTA, d, 0, ms); break;   /* PRESETS: the user presets */
    case KN_ALGO: send(a, K_ACT_TYPE_DELTA, d, 0, ms); break;       /* ALGORITHM: the engine, or the effect in hand on FX */
    default: send(a, K_ACT_PARAM_DELTA, ctl[i].id - KN_K1, d * step, ms); break;
    }
    return 1;
}

void panel_draw(SDL_Renderer *r, const KApp *a)
{
    KTheme t;
    int i;
    k_theme(&t, ui_draw_theme());
    g_clear(&cv, t.bg);
    g_rrect(&cv, 0, 0, W, H, 8, t.edge);
    g_rrect(&cv, 1, 1, W - 2, H - 2, 7, t.surf);
    for (i = 0; i < nctl; i++) {
        Ctl *c = &ctl[i];
        KRect q = c->r;
        int over = i == hover, down = i == held;
        if (c->kind == C_KNOB) {
            float an = angle[c->id];
            int cx = q.x + q.w / 2, cy = q.y + q.h / 2;
            g_rrect(&cv, q.x, q.y, q.w, q.h, q.w / 2, down || over ? k_mix(t.raise, t.text, 14) : t.raise);
            g_line(&cv, cx + (int)(sinf(an) * 5.f) - 1, cy - (int)(cosf(an) * 5.f) - 1,
                   cx + (int)(sinf(an) * 11.f) - 1, cy - (int)(cosf(an) * 11.f) - 1, 2, t.accent);
            g_text_box(&cv, q.x - 26, q.y + q.h + 2, q.w + 52, 14, GF_S, c->label, over ? t.text : t.mid, 0);
        } else if (c->kind == C_BTN) {
            int id = c->id, live = BTN[id].group >= 0 ? group_of_page(a->page) == BTN[id].group :
                                   id == B_PLAY ? a->playing : id == B_REC ? a->recording : 0;
            int usable = BTN[id].group >= 0 || id == B_HOME || id == B_SAVE || id == B_PLAY || id == B_REC || id == B_OCTM || id == B_OCTP;
            uint32_t face = down ? t.key : over && usable ? k_mix(t.raise, t.text, 10) : t.raise;
            if (usable) g_rrect(&cv, q.x, q.y, q.w, q.h - 2, 5, face);
            else g_rrect(&cv, q.x, q.y, q.w, q.h - 2, 5, k_mix(t.surf, t.bg, 40));
            g_text_box(&cv, q.x, q.y, q.w, q.h - 2, GF_S, c->label, !usable ? t.dim : down ? t.ink : live ? t.accent : t.text, 0);
            if (live) g_fill(&cv, q.x + q.w / 2 - 6, q.y + q.h - 5, 12, 2, t.accent);
        } else {
            int black = c->id >= NWHITE;
            uint32_t face = black ? k_mix(t.key, t.ink, 80) : t.key;
            g_rrect(&cv, q.x, q.y, q.w, q.h - 2, 3, down ? t.accent : over ? k_mix(face, t.bg, 8) : face);
            if (k_key_lit(a, c->id))
                g_rrect(&cv, q.x + q.w / 2 - 2, q.y + 4, 5, 5, 2, t.accent);
        }
    }
    SDL_UpdateTexture(tex, NULL, cv.px, W * 4);
    SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){origin, 0, W, H});
}
int panel_save(const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(cv.px, W, H, 32, W * 4, SDL_PIXELFORMAT_ARGB8888);
    int ok;
    if (!s) return -1;
    ok = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return ok;
}
