/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM-1 panel's controls become the semantic actions they should (run with SDL_VIDEODRIVER=dummy). */
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include "kreisel.h"
#include "draw.h"
#include "panel.h"

static int at(const char *label, int *x, int *y) { return panel_find(label, x, y); }
static void turn(KApp *a, const char *label, int dy, int fine)
{
    int x, y;
    assert(at(label, &x, &y));
    assert(panel_wheel(a, x, y, dy, fine, 0));
}
static void press(KApp *a, const char *label, int release)
{
    int x, y;
    assert(at(label, &x, &y));
    assert(panel_press(a, x, y, 0));
    if (release) panel_release(a, 0);
}

int main(void)
{
    SDL_Window *w;
    SDL_Renderer *r;
    KApp a;
    int x, y;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    w = SDL_CreateWindow("t", 0, 0, 600, 300, 0);
    r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    assert(r && ui_draw_init(k_profile_find("fm1"), K_THEME_DARK) && panel_init(r, "fm1", 248));
    assert(!panel_init(r, "nonesuch", 0));
    k_init(&a);k_open_page(&a,K_PAGE_STEP);
    ui_draw(&a, &(KCursorVisual){0}, 0);   /* places the focus rectangles */

    turn(&a, "MASTER", 1, 0);   assert(a.master == 82);
    turn(&a, "MASTER", -1, 1);  assert(a.master == 81);
    turn(&a, "KNOB 2", 1, 0);   assert(a.swing > 0);   /* SEQ's second card: the swing */
    assert(a.focus[a.focused].kind == K_NODE_PARAM && a.focus[a.focused].index == 1);   /* focus followed the knob */
    {   int f = a.focused;   turn(&a, "SELECT", -1, 0);   assert(a.focused != f && a.track == 0); }   /* SELECT: the cursor */
    k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 0}); turn(&a, "SELECT", 1, 0); k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 0});
    assert(a.track == 1 && a.page == K_PAGE_STEP);                   /* HOME held + SELECT: the track */
    turn(&a, "PRESETS", 1, 0);   assert(a.factory[a.track] != 0);   /* STEP: PRESETS does nothing (no preset's name there) */
    k_open_page(&a, K_PAGE_SOUND); turn(&a, "PRESETS", 1, 0);   assert(a.balloon.kind == K_BALLOON_PRESET && a.factory[a.track] == 0);   /* SOUND: the presets (the factory's first) */
    k_open_page(&a, K_PAGE_STEP);
    k_open_page(&a, K_PAGE_SOUND); turn(&a, "ALGORITHM", 1, 0); assert(a.sound[1] == 1);   /* ALGORITHM: the engine */
    turn(&a, "ALGORITHM", -1, 0); assert(a.sound[1] == 0);
    k_open_page(&a, K_PAGE_STEP); ui_draw(&a, &(KCursorVisual){0}, 0);

    press(&a, "PLAY", 1);        assert(a.playing);
    press(&a, "REC", 1);         assert(a.recording);
    k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 0}); turn(&a, "PRESETS", 1, 0); k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 0});
    assert(a.octave == 1 && a.page == K_PAGE_STEP);   /* HOME held + PRESETS: the octave (OCT+ and OCT- are yes and no) */
    {   uint16_t before = a.steps[a.track][a.pattern[a.track]];
        press(&a, "KEY 4", 1);   assert(a.steps[a.track][a.pattern[a.track]] == (uint16_t)(before ^ (1u << 3))); }

    press(&a, "ENV", 1);         assert(a.page == K_PAGE_ENV1 && a.group_held == -1);   /* a tap: the family's first page */
    press(&a, "HOME", 1);        assert(a.page == K_PAGE_MIXER);                        /* HOME: the mixer */
    press(&a, "ENV", 0);         assert(a.group_held == K_GROUP_ENV);                      /* held ... */
    k_tick(&a, 1000);            assert(!a.menu_open && !a.tools_open);                    /* ... alone: nothing */
    turn(&a, "SELECT", -1, 0);   assert(a.page == K_PAGE_ENV1 && !a.menu_open && !a.tools_open);   /* + SELECT: its page (ENV has no tools) */
    panel_release(&a, 1010);     assert(a.page == K_PAGE_ENV1 && a.group_held == -1);       /* release: nothing more */
    press(&a, "ENV", 1);         assert(a.page == K_PAGE_ENV2);                            /* a press again: the next page */
    press(&a, "REC", 1);         assert(a.page == K_PAGE_ENV2);                            /* REC marks recording, it opens no page */
    press(&a, "SEL", 1);         assert(a.page == K_PAGE_SOUND);                           /* SEL: the synth */
    press(&a, "EDIT", 1);        assert(a.page == K_PAGE_EDIT);                            /* EDIT: VCF, VCA (the song is GLO's second) */
    assert(!panel_press(&a, 5, 5, 0));                                               /* between controls: not taken */
    press(&a, "HOME", 1);        assert(a.page == K_PAGE_MIXER); press(&a, "HOME", 1); assert(a.page == K_PAGE_HOME);   /* (HOME again: the macros) */
    k_open_page(&a, K_PAGE_STEP);                                                     /* the keys step on STEP */
    {   uint16_t before = a.steps[a.track][a.pattern[a.track]];
        press(&a, "BLACK 1", 1); assert(a.steps[a.track][a.pattern[a.track]] == before);   /* a black key steps nothing (on STEP it does nothing) */
        a.sound[a.track] = K_ENGINE_COUNT - 1; k_open_page(&a, K_PAGE_DRUM);
        press(&a, "BLACK 4", 1); assert(a.cur_lane[a.track] == 3);   /* on DRUM it picks the lane */
        a.sound[a.track] = 0; k_open_page(&a, K_PAGE_STEP);
        press(&a, "KEY 16", 1);  assert(a.steps[a.track][a.pattern[a.track]] == (uint16_t)(before ^ (1u << 15))); }
    assert(at("BLACK 11", &x, &y));

    panel_shutdown(); ui_draw_shutdown();
    SDL_DestroyRenderer(r); SDL_DestroyWindow(w); SDL_Quit();
    puts("kreisel panel tests: OK");
    return 0;
}
