/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The values' table (src/kreisel_param.c): every page's item, on every engine and layout, finds an entry; its storage is what
 * k_param gives; a reset lands within its range; a set outright stays in it; a turn never leaves it. */
#include "kreisel.h"
#include "kreisel_plugin.h"
#include <assert.h>
#include <stdio.h>

static void check(KApp *a)
{
    int pg, i;
    for (pg = 0; pg < K_PAGE_COUNT; pg++) {
        for (i = 0; i < K_ITEM_SLOTS; i++) {   /* (every slot a page could have, past its own too) */
            const KParamRef r = k_param_ref(a, (KPage)pg, i);
            KParamProps p;
            assert(r.id < K_P_COUNT && r.track < K_TRACKS);
            assert(k_param_at(a, r) == k_param(a, (KPage)pg, i));
            if (!k_param_props(a, r, &p)) continue;
            assert(p.lo <= p.hi);
            assert(p.center == K_NO_CENTER || (p.center >= p.lo && p.center <= p.hi));
            if (!(p.flags & K_PF_NORESET)) assert(p.reset >= p.lo && p.reset <= p.hi);
            {   /* set outright far out either way, and a turn: within the range (then put back) */
                KVal *v = k_param_at(a, r); int old = *v;
                if (k_ref_set(a, r, 30000)) assert(*v <= p.hi);
                if (k_ref_set(a, r, -30000)) assert(*v >= p.lo);
                *v = old;
                k_ref_turn(a, r, 500);
                assert(*v == old || (*v >= p.lo && *v <= p.hi));
                *v = old;
            }
        }
    }
}

int main(void)
{
    static KApp a;
    static const unsigned LAY[4] = {0, K_LAY_ENV_PAIR, K_LAY_FX_ALGO, K_LAY_SYNTH_ALL | K_LAY_ENV_PAIR};
    int e, l, t;
    for (l = 0; l < 4; l++)
        for (e = 0; e < k_engine_count(); e++)
            for (t = 0; t < K_TRACKS; t++) {
                k_init(&a);
                k_set_layout(&a, LAY[l]);
                a.track = t;
                k_track_engine(&a, t, e);
                a.params[t][K_PAGE_FX][0] = 1 + e % k_effect_count();   /* an effect in each slot: their rows */
                a.params[t][K_PAGE_FX][4] = 1 + (e + 3) % k_effect_count();
                check(&a);
            }
    {   /* the table's own: HOME's PAN of track 2 is the mixer's, its colour the track's */
        k_init(&a);
        KParamRef r = k_param_ref(&a, K_PAGE_TRACKS, 4 + K_TRACKS + 1);
        assert(r.id == K_P_PAN && r.track == 1 && k_param_at(&a, r) == &a.params[1][K_PAGE_MIXER][1]);
        r = k_param_ref(&a, K_PAGE_TRACKS, K_HOME_COLOR0 + 1);
        assert(r.id == K_P_COLOR && k_param_at(&a, r) == &a.tcolor[1]);
        assert(k_param_ref(&a, K_PAGE_STEP, 0).id == K_P_BPM && k_param_ref(&a, K_PAGE_CLOCK, 0).id == K_P_BPM);
    }
    {   /* M and S: one command each, whatever pressed them; a track out of range is let be */
        k_init(&a);
        k_mute_toggle(&a, 1); assert(a.mute == 2 && !k_track_audible(&a, 1) && k_track_audible(&a, 0));
        k_solo_toggle(&a, 0); assert(a.solo == 1 && k_track_audible(&a, 0) && !k_track_audible(&a, 2));
        k_mute_toggle(&a, 1); k_solo_toggle(&a, 0); assert(!a.mute && !a.solo);
        k_mute_toggle(&a, K_TRACKS); k_solo_toggle(&a, -1); assert(!a.mute && !a.solo);
        assert(k_param_set(&a, K_PAGE_MIXER, 1, 140) && a.params[0][K_PAGE_MIXER][1] == 100);   /* a drag past the end: kept in range */
    }
    {   /* SONG's cells: one command; pressed goes round, turned stops at the ends */
        k_init(&a);
        a.song[0][1] = 4;
        assert(k_song_cell_step(&a, 0, 1, 1, 1) && a.song[0][1] == 0);    /* round: after nothing (4), A */
        assert(k_song_cell_step(&a, 0, 1, -1, 0) && a.song[0][1] == 4);   /* turned, the knobs' order: nothing A B C D, A down: nothing */
        assert(!k_song_cell_step(&a, 0, 1, -1, 0) && a.song[0][1] == 4);  /* stops at nothing */
        assert(k_song_cell_step(&a, 0, 1, 1, 0) && a.song[0][1] == 0);    /* nothing up: A */
        assert(k_song_cell_step(&a, 0, 1, 9, 0) && a.song[0][1] == 3);    /* and stops at D */
        assert(!k_song_cell_set(&a, K_SCENES, 0, 1) && !k_song_cell_set(&a, 0, K_TRACKS, 1));
        a.track = 2; a.params[0][K_PAGE_BUS1][0] = 77; a.params[2][K_PAGE_BUS1][0] = 5;
        assert(k_param_get(&a, K_PAGE_BUS1, 0) == 77);   /* a bus's values are the song's (kept with track 1), whichever track is in hand */
    }
    {   /* a drum lane's steps: set (a drag across takes one state), toggle; nothing outside the lanes and steps */
        k_init(&a);
        a.lane_on[1][2] = 0;
        assert(k_lane_step_set(&a, 1, 2, 5, 1) && a.lane_on[1][2] == 1u << 5);
        assert(!k_lane_step_set(&a, 1, 2, 5, 1));                          /* already on: no change */
        assert(k_lane_step_toggle(&a, 1, 2, 5) && a.lane_on[1][2] == 0);
        assert(!k_lane_step_set(&a, 1, K_LANES, 0, 1) && !k_lane_step_set(&a, 1, 0, K_STEPS, 1) && !k_lane_step_toggle(&a, K_TRACKS, 0, 0));
    }
    {   /* the selection and the clipboard, in the core (C3): SONG's column and row, DRUM's lane, a value; what is selected acts only on
         * its page; nothing selected: the value in hand */
        k_init(&a); k_open_page(&a, K_PAGE_SONG);
        a.song[2][0] = 1; a.song[2][1] = 3;
        k_select(&a, K_SEL_SCENE, 2); assert(k_selected(&a, K_SEL_SCENE) == 2 && k_selected(&a, K_SEL_ROW) < 0);
        assert(k_edit(&a, 0));                                   /* copy scene 3's column */
        k_select(&a, K_SEL_SCENE, 5); assert(k_edit(&a, 1) && a.song[5][0] == 1 && a.song[5][1] == 3);   /* pasted over scene 6 */
        k_select(&a, K_SEL_ROW, 1); assert(!k_edit(&a, 1));       /* a column does not paste over a row */
        assert(k_edit(&a, 2) && a.song[0][1] == 4 && a.song[7][1] == 4);   /* cleared: nothing on track 2 */
        k_open_page(&a, K_PAGE_MIXER); assert(k_selected(&a, K_SEL_ROW) < 0);   /* (another page: not selected there) */
        a.sound[1] = K_ENGINE_COUNT - 1; a.track = 1; k_open_page(&a, K_PAGE_DRUM);   /* a drum track */
        a.lane_on[1][3] = 0x00F0; k_select(&a, K_SEL_LANE, 3); assert(a.cur_lane[1] == 3 && k_selected(&a, K_SEL_LANE) == 3);
        assert(k_edit(&a, 0)); a.cur_lane[1] = 5; k_select(&a, K_SEL_LANE, 5); assert(k_edit(&a, 1) && a.lane_on[1][5] == 0x00F0);
        k_init(&a); k_open_page(&a, K_PAGE_MIXER);
        assert(!k_value_paste(&a, K_PAGE_MIXER, 1));              /* nothing copied */
        a.params[0][K_PAGE_MIXER][1] = 70; assert(k_value_copy(&a, K_PAGE_MIXER, 1));
        assert(k_value_paste(&a, K_PAGE_MIXER, 2) && a.params[0][K_PAGE_MIXER][2] == 70);   /* PAN's value onto BUS1's send */
        assert(k_value_paste(&a, K_PAGE_TRACKS, K_HOME_COLOR0) && a.tcolor[0] == K_PALETTE - 1);   /* kept within its range */
    }
    {   /* Duplicate, where it goes as its value (the panel's way; copy and paste are a desktop's): the scene, the pattern, the lane, the
         * sound (its engine with it), the inserts; each undone */
        int f, was;
        k_init(&a); k_open_page(&a, K_PAGE_SONG);
        for (f = 0; f < a.focus_count; f++) if (a.focus[f].kind == K_NODE_SCENE && a.focus[f].index == 0 * K_SCENES + 1) a.focused = f;   /* scene 2 */
        a.song[1][0] = 2; a.song[1][1] = 0; was = a.song[6][0];
        assert(!k_seq_tool(&a, K_TOOL_SC_DUP, 1, 0));                           /* onto itself: nothing */
        assert(k_seq_tool(&a, K_TOOL_SC_DUP, 6, 0) && a.song[6][0] == 2 && a.song[6][1] == 0);   /* onto scene 7 */
        assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0) && a.song[6][0] == was);
        k_init(&a); a.track = 0; a.pattern[0] = 0; k_open_page(&a, K_PAGE_ROLL);   /* a synth track: pattern A onto C */
        assert(k_seq_tool(&a, K_TOOL_DUP, 2, 0) && a.steps[0][2] == a.steps[0][0] && a.steps[0][0]);
        a.sound[1] = K_ENGINE_COUNT - 1; a.track = 1; a.cur_lane[1] = 0; a.lane_on[1][0] = 0x0F0F;   /* a drum track: lane 1 onto lane 4 */
        assert(k_seq_tool(&a, K_TOOL_DUP, 3, 0) && a.lane_on[1][3] == 0x0F0F);
        k_init(&a); a.track = 0; a.sound[2] = 2; a.params[0][K_PAGE_SOUND][4] = 3;   /* track 1's sound onto track 3 (another engine there) */
        { const int e0 = a.sound[0], e2 = a.sound[2];
          assert(e0 != e2 && k_seq_tool(&a, K_TOOL_SN_DUP, 2, 0) && a.sound[2] == e0 && a.params[2][K_PAGE_SOUND][4] == 3);
          assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0) && a.sound[2] == e2); }       /* undone: its own engine back */
        a.params[0][K_PAGE_FX][0] = 3; assert(k_seq_tool(&a, K_TOOL_FX_DUP, 1, 0) && a.params[1][K_PAGE_FX][0] == 3);
        k_value_copy(&a, K_PAGE_MIXER, 0); assert(a.clip_kind == K_CLIP_VALUE);   /* (the clipboard: a desktop's alone) */
    }
    {   /* the tools' list on the FM-1's keys: OCT+ does it, OCT- lets it go, the octave untouched; what waits for an answer */
        k_init(&a); k_open_page(&a, K_PAGE_MIXER); a.params[1][K_PAGE_MIXER][1] = 10;
        assert(k_asks(&a) == K_ASK_NONE);
        k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, 1}); assert(a.tools_open && k_asks(&a) == K_ASK_OCT);
        a.tools_sel = 2;   /* Pans center */
        assert(k_tools_item(&a, 2) == K_TOOL_MX_PANS);
        k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, 2}); assert(!a.tools_open && a.octave == 0 && a.params[1][K_PAGE_MIXER][1] == 10);
        k_dispatch(&a, (KAction){K_ACT_TOOLS, 0, 0, 3}); a.tools_sel = 2;
        k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, 4}); assert(!a.tools_open && a.octave == 0 && a.params[1][K_PAGE_MIXER][1] == 50);
        a.save_ask = 1; assert(k_asks(&a) == K_ASK_SAVE); a.save_ask = 0;
    }
    {   /* three steps at most: the pattern by ALGORITHM on SEQ; undo by HOME + REC anywhere */
        k_init(&a); k_open_page(&a, K_PAGE_ROLL); a.track = 0; a.pattern[0] = 0;
        k_dispatch(&a, (KAction){K_ACT_TYPE_DELTA, 1, 0, 1}); assert(a.pattern[0] == 1 && a.balloon.kind == K_BALLOON_PATTERN);
        k_dispatch(&a, (KAction){K_ACT_TYPE_DELTA, 5, 0, 2}); k_dispatch(&a, (KAction){K_ACT_TYPE_DELTA, 1, 0, 3}); k_dispatch(&a, (KAction){K_ACT_TYPE_DELTA, 1, 0, 4});
        assert(a.pattern[0] == 3);   /* (a detent a pattern; stops at D) */
        k_open_page(&a, K_PAGE_MIXER); a.params[1][K_PAGE_MIXER][1] = 10;
        assert(k_seq_tool(&a, K_TOOL_MX_PANS, 0, 0) && a.params[1][K_PAGE_MIXER][1] == 50);
        k_open_page(&a, K_PAGE_HOME);
        k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 10}); k_dispatch(&a, (KAction){K_ACT_REC, 0, 0, 20});
        assert(a.params[1][K_PAGE_MIXER][1] == 10 && !a.recording && a.balloon.kind == K_BALLOON_UNDO);   /* undone, REC not armed */
        k_dispatch(&a, (KAction){K_ACT_REC, 0, 0, 30}); assert(a.params[1][K_PAGE_MIXER][1] == 50);   /* again: redone */
        k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 40}); assert(a.page == K_PAGE_HOME && !a.settings_open);   /* (HOME's release: nothing) */
        k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 1000}); k_tick(&a, 1600); assert(a.settings_open);   /* held a while first: */
        k_dispatch(&a, (KAction){K_ACT_REC, 0, 0, 1700}); assert(!a.settings_open && a.params[1][K_PAGE_MIXER][1] == 10);   /* still undo, the settings gone */
        k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 1800}); assert(!a.settings_open && a.page == K_PAGE_HOME);
        k_dispatch(&a, (KAction){K_ACT_UNDO, 0, 0, 1900}); assert(a.params[1][K_PAGE_MIXER][1] == 50);   /* Cmd+Z: redone */
        a.balloon.kind = K_BALLOON_NONE; a.settings_open = 1;
        k_dispatch(&a, (KAction){K_ACT_UNDO, 0, 0, 2000}); assert(a.params[1][K_PAGE_MIXER][1] == 50 && a.balloon.kind != K_BALLOON_UNDO);   /* (the settings up: nothing) */
        a.settings_open = 0;
        k_dispatch(&a, (KAction){K_ACT_UNDO, 0, 0, 2100}); assert(a.params[1][K_PAGE_MIXER][1] == 10 && a.balloon.kind == K_BALLOON_UNDO);
        k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 2000}); k_tick(&a, 2600); k_dispatch(&a, (KAction){K_ACT_SELECT_DELTA, 1, 0, 2700});
        assert(!a.settings_open && a.track == 1);   /* HOME + SELECT, slow: the track, not the settings */
        k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 2800});
        k_dispatch(&a, (KAction){K_ACT_HOME_DOWN, 0, 0, 3000}); k_tick(&a, 3600); k_dispatch(&a, (KAction){K_ACT_HOME_UP, 0, 0, 3700});
        assert(a.settings_open);   /* (held alone: the settings stay) */
        k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, 3800}); assert(!a.settings_open);   /* OCT-: back */
    }
    {   /* a page key: a press turns its pages, held + SELECT its tools, the release does nothing more, OCT+ does it, OCT- lets go
         * */
        k_init(&a); k_open_page(&a, K_PAGE_HOME); a.params[1][K_PAGE_MIXER][1] = 10;
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 10}); k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_GLO, 0, 20}); assert(a.page == K_PAGE_SONG);   /* GLO: SONG first */
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 30}); k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_GLO, 0, 40}); assert(a.page == K_PAGE_BUS1);
        k_open_page(&a, K_PAGE_HOME); a.tools_sel = 0;
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 100});
        k_dispatch(&a, (KAction){K_ACT_SELECT_DELTA, 1, 0, 110}); assert(a.page == K_PAGE_MIXER && a.tools_open && a.tools_sel == 0);   /* the first detent: the list */
        k_dispatch(&a, (KAction){K_ACT_SELECT_DELTA, 2, 0, 120}); assert(a.tools_sel == 2 && k_tools_item(&a, 2) == K_TOOL_MX_PANS && a.track == 0);
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_GLO, 0, 130}); assert(a.page == K_PAGE_MIXER && a.tools_open && a.params[1][K_PAGE_MIXER][1] == 10);   /* let go: not done */
        k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, 140}); assert(!a.tools_open && a.params[1][K_PAGE_MIXER][1] == 50 && a.octave == 0);
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 200}); k_dispatch(&a, (KAction){K_ACT_SELECT_DELTA, 1, 0, 210});
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_GLO, 0, 220}); k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, 230});
        assert(!a.tools_open && a.page == K_PAGE_MIXER);   /* OCT-: let go, the page stays */
        k_open_page(&a, K_PAGE_FX); k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, -1, 0, 240}); assert(a.page == K_PAGE_FX && a.octave == -1);   /* OCT- on a page: the octave */
        k_open_page(&a, K_PAGE_STEP); a.focused = 0; { uint16_t b = a.steps[0][0];
        k_dispatch(&a, (KAction){K_ACT_OCTAVE_DELTA, 1, 0, 250}); assert(a.steps[0][0] == b && a.octave == 0); }   /* OCT+ on a page: the octave, no step (the keys set them) */
    }
    {   /* SONG on the FM-1's knobs: SELECT the scene, K1..K4 the patterns of tracks 1..4 in it (none A B C D) */
        k_init(&a); k_open_page(&a, K_PAGE_SONG); assert(k_params_visible(&a) == 0 && a.song[0][0] == 4);   /* empty; no cards */
        k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, 1}); k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, 2});
        assert(a.focus[a.focused].index % K_SCENES == 2);   /* scene 3 */
        k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 2, 3}); assert(a.song[2][1] == 0);   /* K2 up: track 2, A */
        k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 2, 4}); k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 2, 5}); assert(a.song[2][1] == 2);   /* C */
        k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 2, 6}); k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, 2, 7}); assert(a.song[2][1] == 3);   /* D, stops */
        for (int k = 0; k < 6; k++) k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, -2, 8}); assert(a.song[2][1] == 4);   /* down to none, stops */
        for (int k = 0; k < K_SCENES + 4; k++) k_dispatch(&a, (KAction){K_ACT_FOCUS_NEXT, 0, 0, 9}); assert(a.focus[a.focused].index % K_SCENES == K_SCENES - 1 && a.song_off == K_SCENES - K_SONG_VIS);   /* stops at the last, the window moved with it */
        assert(a.bpm == 120);   /* (the knobs no longer the tempo here) */
    }
    {   /* a keyboard alone (the Zero, K_LAY_MIX_KEYS): the mixer's left / right the strip, up / down the control, K L (the value keys) its value */
        k_init(&a); k_set_layout(&a, K_LAY_MIX_KEYS); k_open_page(&a, K_PAGE_MIXER);
        { int lv = a.params[0][K_PAGE_MIXER][0], pn = a.params[1][K_PAGE_MIXER][1];
          k_dispatch(&a, (KAction){K_ACT_VALUE_DELTA, -1, 0, 1}); assert(a.params[0][K_PAGE_MIXER][0] == lv - 1);   /* L: the level down */
          k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 2}); assert(a.track == 1);   /* right: the next strip */
          k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 3}); k_dispatch(&a, (KAction){K_ACT_VALUE_DELTA, 1, 0, 4});
          assert(a.params[1][K_PAGE_MIXER][1] == pn + 1);   /* up from the fader: the pan (over it on the strip); K: up */
          k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 5}); k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 6}); k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 7});
          assert(a.focus[a.focused].kind == K_NODE_PARAM && a.focus[a.focused].index == 2);   /* up to B1, at the top it stays */
          k_dispatch(&a, (KAction){K_ACT_DOWN, 0, 0, 8}); assert(a.focus[a.focused].index == 3);   /* down: B2, */
          k_dispatch(&a, (KAction){K_ACT_DOWN, 0, 0, 9}); k_dispatch(&a, (KAction){K_ACT_DOWN, 0, 0, 10}); assert(a.focus[a.focused].index == 0);   /* then pan, the fader */
          for (int k = 0; k < 12; k++) k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 5}); assert(a.track == K_TRACKS - 1); }   /* (stops at the last) */
    }
    {   /* the roll as cells (K_LAY_ROLL_CELLS: the Zero): the arrows a step and a pitch, OK a note there or none, the value keys its length */
        k_init(&a); k_set_layout(&a, K_LAY_ROLL_CELLS); a.track = 0; k_open_page(&a, K_PAGE_ROLL); a.cur[0] = 0; a.rnote[0] = 60;
        { const int p = a.pattern[0], before = k_roll_count(&a, 0, p, 3);
          k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 1}); k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 2}); k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 3});
          k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 4}); k_dispatch(&a, (KAction){K_ACT_UP, 0, 0, 5});
          assert(a.cur[0] == 3 && a.rnote[0] == 62);   /* (no pick: the pitch stays the cursor's) */
          k_dispatch(&a, (KAction){K_ACT_ACCEPT, 0, 0, 6}); assert(k_roll_at(&a, 0, p, 3, 62) >= 0 && k_roll_count(&a, 0, p, 3) == before + 1);
          k_dispatch(&a, (KAction){K_ACT_VALUE_DELTA, 1, 0, 7}); assert(a.roll[0][k_roll_at(&a, 0, p, 3, 62)].len == 2);   /* K: longer */
          assert(k_roll_at(&a, 0, p, 4, 62) >= 0);   /* (it covers the next step now) */
          k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 8}); k_dispatch(&a, (KAction){K_ACT_ACCEPT, 0, 0, 9});
          assert(k_roll_at(&a, 0, p, 3, 62) < 0);   /* OK on its tail: taken away */
          for (int k = 0; k < 20; k++) k_dispatch(&a, (KAction){K_ACT_RIGHT, 0, 0, 10}); assert(a.cur[0] == K_STEPS - 1); }   /* (stops at the last step) */
    }
    {   /* the cells' grip (OK held): a note's head moves its start, its tail its end, its middle the whole, up / down its pitch;
         * on empty cells notes are drawn; OK let go unmoved is OK */
        int sl; KNote n; const int p0 = 0;
        k_init(&a); k_set_layout(&a, K_LAY_ROLL_CELLS); a.track = 0; a.pattern[0] = 0; k_open_page(&a, K_PAGE_ROLL);
        for (int s2 = 0; s2 < K_STEPS; s2++) k_roll_clear(&a, 0, p0, s2);
        k_roll_add(&a, 0, p0, 4, 60, 4, 100);   /* steps 4 .. 7 */
#define G(t2, x) k_dispatch(&a, (KAction){t2, x, 0, 1})
        a.cur[0] = 7; a.rnote[0] = 60;   /* the tail */
        G(K_ACT_GRAB, 1); G(K_ACT_RIGHT, 0); G(K_ACT_RIGHT, 0); G(K_ACT_GRAB, 0);
        sl = k_roll_at(&a, 0, p0, 4, 60); n = a.roll[0][sl]; assert((n.at & 15) == 4 && n.len == 6 && a.cur[0] == 9);   /* the end: 9 */
        a.cur[0] = 4;   /* the head */
        G(K_ACT_GRAB, 1); G(K_ACT_LEFT, 0); G(K_ACT_GRAB, 0);
        sl = k_roll_at(&a, 0, p0, 3, 60); n = a.roll[0][sl]; assert((n.at & 15) == 3 && n.len == 7 && a.cur[0] == 3);   /* the start: 3, the end kept */
        G(K_ACT_GRAB, 1); G(K_ACT_RIGHT, 0); G(K_ACT_RIGHT, 0); G(K_ACT_GRAB, 0);
        sl = k_roll_at(&a, 0, p0, 5, 60); n = a.roll[0][sl]; assert((n.at & 15) == 5 && n.len == 5);   /* (in again: shorter) */
        a.cur[0] = 7;   /* the middle */
        G(K_ACT_GRAB, 1); G(K_ACT_RIGHT, 0); G(K_ACT_UP, 0); G(K_ACT_UP, 0); G(K_ACT_GRAB, 0);
        sl = k_roll_at(&a, 0, p0, 8, 62); assert(sl >= 0); n = a.roll[0][sl]; assert((n.at & 15) == 6 && n.len == 5 && n.note == 62 && a.rnote[0] == 62);   /* moved, then up two */
        assert(k_roll_count(&a, 0, p0, 6) == 1);   /* (still one note) */
        a.cur[0] = 0; a.rnote[0] = 48;   /* empty: drawn along */
        G(K_ACT_GRAB, 1); G(K_ACT_RIGHT, 0); G(K_ACT_RIGHT, 0); G(K_ACT_GRAB, 0);
        assert(k_roll_at(&a, 0, p0, 0, 48) >= 0 && k_roll_at(&a, 0, p0, 1, 48) >= 0 && k_roll_at(&a, 0, p0, 2, 48) >= 0);
        a.cur[0] = 14; a.rnote[0] = 70;   /* OK down, up, unmoved: a note there */
        G(K_ACT_GRAB, 1); G(K_ACT_GRAB, 0); assert(k_roll_at(&a, 0, p0, 14, 70) >= 0);
        G(K_ACT_GRAB, 1); G(K_ACT_GRAB, 0); assert(k_roll_at(&a, 0, p0, 14, 70) < 0);   /* again: gone */
        k_open_page(&a, K_PAGE_SOUND); a.settings_open = 1; a.settings_sel = K_SET_HELP; { int v = a.set_v[K_SET_HELP];
        G(K_ACT_GRAB, 1); assert(a.set_v[K_SET_HELP] != v); G(K_ACT_GRAB, 0); }   /* (elsewhere its press is OK) */
#undef G
    }
    printf("param_test: ok\n");
    return 0;
}
