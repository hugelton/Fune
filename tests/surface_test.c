/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The touch surface (src/ui/draw_surface.c) on a phone on its side and on a tablet: where the page goes, the faces, and what
 * the fingers send: two keys at once are two notes, let go they stop; the bend goes back to the middle; a pad strikes its lane;
 * the tracker view brings the game pad and its arrows move the cursor. Drawn frames are checked by size only. */
#include "draw.h"
#include "kreisel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static KApp app;
static KCursorVisual cur;

static int screen(int w, int h)   /* the surface for a w x h screen, the page profile its box; the page's width back */
{
    int pw, ph;
    ui_surface_layout(w, h, &pw, &ph);
    assert(pw > 0 && ph > 0 && pw <= w && ph <= h);
    {   const int ok = ui_draw_init(k_profile_custom(pw, ph, K_IN_TOUCH), K_THEME_DARK);   /* (not inside assert: a Release build drops it) */
        assert(ok); (void)ok; }
    k_set_shown(&app, 4);
    k_set_layout(&app, 0);
    {   const int ok = ui_surface_init(1); assert(ok); (void)ok; }
    ui_draw(&app, &cur, 10); ui_draw(&app, &cur, 20);
    {   const GCanvas *c = ui_surface_frame(&app, 20);
        assert(c->w == w && c->h == h); }
    return pw;
}
static int notes(int *on, int *off)   /* the live notes the core queued since */
{
    KNoteEv e;
    int n = 0;
    *on = *off = 0;
    while (k_take_note(&app, &e)) { n++; if (e.vel) (*on)++; else (*off)++; }
    return n;
}
int main(void)
{
    int on, off, x, y;
    k_init(&app);
    app.track = 0;
    app.sound[0] = 0;   /* (a synth: Keys) */

    /* a phone on its side, 844 x 390: no bar, the surface always up under the page, the Keys for a synth
     * track; the page laid out in the room above it, never again for the panel */
    {   const int w0 = screen(844, 390); assert(w0 == 844); (void)w0; }   /* (the buttons' column is inside the surface; not in an assert: Release) */
    ui_surface_frame(&app, 30);
    assert(!ui_surface_relayout());
    {   int pw, ph; ui_surface_layout(844, 390, &pw, &ph); assert(pw == 844 && ph >= 240 && ph < 390); }
    {   const GCanvas *c = ui_surface_frame(&app, 450), *pg = ui_draw_canvas();
        assert(pg->pw == c->pw);   /* (the page the whole width: no letterbox) */
    }
    notes(&on, &off);
    /* two fingers on two white keys near the drawer's bottom (velocity high), then let go */
    y = 390 - 44 - 20;   /* (above the bar: the panel's lower part) */
    for (x = 120; x < 840; x += 4) {   /* (the first two keys found along the row) */
        ui_finger(&app, 1, 0, x, y, 100);
        if (notes(&on, &off) && on == 1) break;
        ui_finger(&app, 1, 2, x, y, 101); notes(&on, &off);
    }
    assert(x < 840);
    ui_finger(&app, 2, 0, x + 70, y, 102);
    assert(notes(&on, &off) == 1 && on == 1);   /* a second note while the first is held */
    ui_finger(&app, 1, 2, x, y, 110);
    ui_finger(&app, 2, 2, x + 70, y, 111);
    assert(notes(&on, &off) == 2 && off == 2);   /* both let go */

    /* the bend strip: dragged up it bends, let go it is back in the middle */
    for (x = 4; x < 300; x += 2) {
        ui_finger(&app, 3, 0, x, 390 - 30, 120);   /* (the strips under the octave's row) */
        ui_finger(&app, 3, 1, x, 390 - 60, 121);
        if (app.bend[0] != 0) break;
        ui_finger(&app, 3, 2, x, 390 - 60, 122);
    }
    assert(x < 300 && app.bend[0] > 0);
    ui_finger(&app, 3, 2, x, 390 - 60, 123);
    assert(app.bend[0] == 0);

    /* a drum track brings the pads: a pad strikes a lane */
    {   int t;
        for (t = 0; t < K_TRACKS; t++) if (k_is_drum(&app, t)) break;
        if (t < K_TRACKS) {
            app.track = t;
            ui_draw(&app, &cur, 130); ui_surface_frame(&app, 130);
            notes(&on, &off);
            ui_finger(&app, 4, 0, 844 / 2, 390 - 44 - 30, 140);
            assert(notes(&on, &off) == 1 && on == 1);
            ui_finger(&app, 4, 2, 844 / 2, 390 - 44 - 30, 150);
            assert(notes(&on, &off) == 1 && off == 1);
            app.track = 0;
        }
    }

    /* the tracker view brings the game pad: its down arrow moves the step */
    k_open_page(&app, K_PAGE_ROLL);
    app.rows_view = 1;
    ui_draw(&app, &cur, 200); ui_surface_frame(&app, 200); ui_surface_frame(&app, 600);
    {   /* its down arrow: the cross at the lower middle of the panel's left */
        const int t0 = app.track, c0 = app.cur[t0];
        /* (panel 101 (26 % of 390): content y 299, height 81; a cell 27: the cross from y 299, its down arm at x 41..68,
         * y 353..380) */
        ui_finger(&app, 5, 0, 54, 366, 210);
        ui_finger(&app, 5, 2, 54, 366, 211);
        assert(app.track == t0 && app.cur[t0] == c0 + 1);
    }
    app.rows_view = 0;

    /* a tablet, 1180 x 820 at 1.5 x: the same panels, the page the whole width */
    {   const int w1 = screen(786, 546); assert(w1 == 786); (void)w1; }
    ui_surface_frame(&app, 300);

    printf("kreisel surface tests: OK\n");
    return 0;
}
