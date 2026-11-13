/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The sequencer's tools (kreisel_seqtool.c): what each does to a synth track's pattern and a drum track's lane, and UNDO */
#include "kreisel.h"
#include "kreisel_engine.h"
#include "kreisel_song.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int notes(const KApp *a, int t, int p) { int i, c = 0; for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && (a->roll[t][i].at >> 4) == p) c++; return c; }
static int bits16(unsigned b) { int c = 0; while (b) { c += b & 1; b >>= 1; } return c; }

int main(void) {
    KApp a;
    int i, k, r;
    /* the Euclidean rhythms: k onsets, the first on step 0, as even as can be */
    assert(k_euclid(4, 0) == 0x1111 && k_euclid(0, 0) == 0 && k_euclid(16, 0) == 0xFFFF && k_euclid(99, 0) == 0xFFFF);
    for (k = 0; k <= 16; k++) for (r = -3; r < 20; r++) assert(bits16(k_euclid(k, r)) == k);
    assert(k_euclid(4, 1) == 0x2222 && k_euclid(4, -1) == 0x8888);
    assert((k_euclid(3, 0) & 1) && bits16(k_euclid(5, 0)) == 5);

    k_init(&a);
    a.track = 0; a.pattern[0] = 1;
    a.rnote[0] = 64; a.rvel[0] = 90; a.rlen[0] = 1;
    k_roll_add(&a, 0, 0, 3, 50, 1, 100);   /* pattern A keeps its own (k_init's pulse and this) whatever pattern B gets */
    {   const int a0 = notes(&a, 0, 0);
        assert(k_seq_tool(&a, K_TOOL_FILL, 4, 0) && notes(&a, 0, 1) == 4 && a.steps[0][1] == 0x1111);
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 1) assert(a.roll[0][i].note == 64 && a.roll[0][i].vel == 90);
        assert(k_seq_tool(&a, K_TOOL_EUCLID, 5, 2) && a.steps[0][1] == k_euclid(5, 2) && notes(&a, 0, 1) == 5);
        assert(k_seq_tool(&a, K_TOOL_SHIFT, 1, 0) && a.steps[0][1] == k_euclid(5, 3));
        assert(k_seq_tool(&a, K_TOOL_SHIFT, -1, 0) && a.steps[0][1] == k_euclid(5, 2));
        assert(k_seq_tool(&a, K_TOOL_TRANSPOSE, 12, 0));
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 1) assert(a.roll[0][i].note == 76);
        assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0));   /* the transpose undone */
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 1) assert(a.roll[0][i].note == 64);
        assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0));   /* and redone */
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 1) assert(a.roll[0][i].note == 76);
        assert(notes(&a, 0, 0) == a0);
        /* REVERSE: a note's end goes where its start was */
        k_seq_tool(&a, K_TOOL_CLEAR, 0, 0); assert(notes(&a, 0, 1) == 0 && a.steps[0][1] == 0 && notes(&a, 0, 0) == a0);
        k_roll_add(&a, 0, 1, 0, 60, 3, 100);
        assert(k_seq_tool(&a, K_TOOL_REVERSE, 0, 0) && a.steps[0][1] == 1u << 13);
        /* SHIFT round: the note at 13 (3 long) moved 5 later is at 2, its length cut to fit nothing (it fits) */
        assert(k_seq_tool(&a, K_TOOL_SHIFT, 5, 0) && a.steps[0][1] == 1u << 2);
        /* COPY then PASTE into pattern C; C's own notes go */
        assert(k_seq_tool(&a, K_TOOL_COPY, 0, 0));
        a.pattern[0] = 2; k_roll_add(&a, 0, 2, 9, 70, 1, 100);
        assert(k_seq_tool(&a, K_TOOL_PASTE, 0, 0) && a.steps[0][2] == 1u << 2 && notes(&a, 0, 2) == 1 && notes(&a, 0, 1) == 1);
        /* RANDOM: 100 % puts a note on every step, in the scale (C major) and near the note in hand; 0 % none */
        a.params[0][K_PAGE_SCL][0] = 0; a.params[0][K_PAGE_SCL][1] = 1;
        assert(k_seq_tool(&a, K_TOOL_RANDOM, 100, 3) && a.steps[0][2] == 0xFFFF && notes(&a, 0, 2) == 16);
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 2) {
            const int n = a.roll[0][i].note;
            assert(n >= 64 - 6 && n <= 64 + 6);
            assert(n == 64 || ((k_scale_masks[1] >> (n % 12)) & 1));
        }
        assert(k_seq_tool(&a, K_TOOL_RANDOM, 0, 0) && a.steps[0][2] == 0);
        /* HUMANIZE keeps the velocities in range and the steps where they were */
        k_seq_tool(&a, K_TOOL_FILL, 1, 0);
        assert(k_seq_tool(&a, K_TOOL_HUMANIZE, 20, 0) && a.steps[0][2] == 0xFFFF);
        for (i = 0; i < K_ROLL_NOTES; i++) if (a.roll[0][i].len && (a.roll[0][i].at >> 4) == 2) assert(a.roll[0][i].vel >= 70 && a.roll[0][i].vel <= 110);
        /* a drum lane's steps are not a pattern's notes: nothing to paste */
        assert(notes(&a, 0, 0) == a0);
    }
    /* a drum track: the lane in hand */
    a.track = 1; a.sound[1] = k_engine_find(K_UID('D', 'R', 'U', 'M')); assert(k_is_drum(&a, 1));
    a.cur_lane[1] = 2;
    {   const uint16_t l0 = a.lane_on[1][0];
        assert(!k_seq_tool(&a, K_TOOL_PASTE, 0, 0));   /* (notes copied, not a lane) */
        assert(!k_seq_tool(&a, K_TOOL_TRANSPOSE, 1, 0));
        assert(k_seq_tool(&a, K_TOOL_EUCLID, 3, 0) && a.lane_on[1][2] == k_euclid(3, 0));
        assert(k_seq_tool(&a, K_TOOL_COPY, 0, 0));
        a.cur_lane[1] = 4;
        assert(k_seq_tool(&a, K_TOOL_PASTE, 0, 0) && a.lane_on[1][4] == k_euclid(3, 0));
        assert(k_seq_tool(&a, K_TOOL_FILL, 2, 0) && a.lane_on[1][4] == 0x5555);
        assert(k_seq_tool(&a, K_TOOL_REVERSE, 0, 0) && a.lane_on[1][4] == 0xAAAA);
        assert(k_seq_tool(&a, K_TOOL_SHIFT, 1, 0) && a.lane_on[1][4] == 0x5555);
        assert(k_seq_tool(&a, K_TOOL_RANDOM, 100, 0) && a.lane_on[1][4] == 0xFFFF);
        assert(k_seq_tool(&a, K_TOOL_CLEAR, 0, 0) && a.lane_on[1][4] == 0 && a.lane_on[1][0] == l0);
        assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0) && a.lane_on[1][4] == 0xFFFF);
        assert(k_seq_tool(&a, K_TOOL_CLEAR, 1, 0)); for (i = 0; i < K_LANES; i++) assert(a.lane_on[1][i] == 0);
        assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0) && a.lane_on[1][0] == l0 && a.lane_on[1][2] == k_euclid(3, 0));
    }
    /* UNDO goes back to the track the tool changed, wherever the cursor is now */
    a.track = 0; a.pattern[0] = 2;
    k_seq_tool(&a, K_TOOL_CLEAR, 0, 0); a.track = 2;
    assert(k_seq_tool(&a, K_TOOL_UNDO, 0, 0) && a.steps[0][2] == 0xFFFF);
    /* the tools' list: opened on ROLL only (a SEQ page), a knob tries the tool, BACK lets it go, SAVE keeps it */
    {   KApp b; KTool t0; uint16_t was;
        k_init(&b); k_open_page(&b, K_PAGE_HOME);
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); assert(!b.tools_open);   /* (HOME: no tools) */
        k_open_page(&b, K_PAGE_ROLL); was = b.steps[0][0];
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); assert(b.tools_open && b.tools_sel == 0 && b.tools_try < 0);
        t0 = k_tools_item(&b, 0); assert(t0 == K_TOOL_FILL && k_tool_vals(t0, 0) == 1);
        k_dispatch(&b, (KAction){K_ACT_PARAM_DELTA, 0, 2, 0});   /* FILL every 5: tried at once */
        assert(b.tool_x[K_TOOL_FILL] == 5 && b.tools_try == 0 && b.steps[0][0] == ((1u | 1u << 5 | 1u << 10 | 1u << 15) & 0xFFFF));
        k_dispatch(&b, (KAction){K_ACT_PARAM_DELTA, 0, -2, 0}); assert(b.steps[0][0] == 0x1111);   /* (every 4: tried again from the pattern as it was) */
        k_dispatch(&b, (KAction){K_ACT_BACK, 0, 0, 0}); assert(!b.tools_open && b.steps[0][0] == was);   /* let go */
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0});
        k_dispatch(&b, (KAction){K_ACT_FOCUS_NEXT, 0, 0, 0}); assert(k_tools_item(&b, b.tools_sel) == K_TOOL_EUCLID);
        k_dispatch(&b, (KAction){K_ACT_KEY, 0, 1, 0}); assert(b.tools_open);   /* (the keys still play under it) */
        k_dispatch(&b, (KAction){K_ACT_KEY, 0, 0, 0});
        k_dispatch(&b, (KAction){K_ACT_PARAM_DELTA, 1, 2, 0});   /* K2: ROT 1 */
        assert(b.tool_y[K_TOOL_EUCLID] == 1 && b.steps[0][0] == k_euclid(5, 1));
        k_dispatch(&b, (KAction){K_ACT_SAVE, 0, 0, 0}); assert(!b.tools_open && b.steps[0][0] == k_euclid(5, 1));   /* kept */
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); assert(b.tools_sel == 1);   /* (the row in hand kept) */
        b.tools_sel = 8; assert(k_tools_item(&b, 8) == K_TOOL_CLEAR);   /* (Duplicate in Copy and Paste's place) */
        k_dispatch(&b, (KAction){K_ACT_PARAM_DELTA, 0, 2, 0}); assert(b.steps[0][0] == k_euclid(5, 1));   /* (no values: a knob does nothing) */
        {   /* Dynamite (the clear): lit, the pattern kept until its fuse burns down; pressed again, put out */
            const uint32_t s0 = b.steps[0][0];
            k_dispatch(&b, (KAction){K_ACT_ACCEPT, 0, 0, 0}); assert(!b.tools_open && b.boom && b.steps[0][0] == s0);
            k_tick(&b, 1000); k_tick(&b, 1000 + K_BOOM_MS / 2); assert(b.boom && b.steps[0][0] == s0);
            k_dispatch(&b, (KAction){K_ACT_ACCEPT, 0, 0, 1500}); assert(!b.boom);   /* (put out) */
            k_tick(&b, 3000); assert(b.steps[0][0] == s0);
            k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); b.tools_sel = 8;
            k_dispatch(&b, (KAction){K_ACT_ACCEPT, 0, 0, 0}); k_tick(&b, 4000); k_tick(&b, 4000 + K_BOOM_MS);
            assert(!b.boom && b.steps[0][0] == 0);   /* (gone off: cleared) */
        }
        {   int i; for (i = 0; i < k_tools_count(&b); i++) assert(k_tools_item(&b, i) != K_TOOL_VIEW && k_tools_item(&b, i) != K_TOOL_CHANCE); }   /* (a view and
                                                 * a step's value are not the pattern's tools) */
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); b.tools_sel = 9; assert(k_tools_item(&b, 9) == K_TOOL_UNDO);
        k_dispatch(&b, (KAction){K_ACT_POINTER, -1, 9, 0}); assert(!b.tools_open && b.steps[0][0] == k_euclid(5, 1));   /* a tap on the row in hand: done */
        k_dispatch(&b, (KAction){K_ACT_TOOLS, 0, 0, 0}); b.tools_sel = 0;
        k_dispatch(&b, (KAction){K_ACT_PARAM_DELTA, 0, 2, 0});
        k_open_page(&b, K_PAGE_MIXER); assert(!b.tools_open && b.steps[0][0] == 0x8421);   /* another page: closed, the tried kept */
    }
    {   /* a cursor's sheet (K_LAY_SHEET_CURSOR, the Zero): the names, OK opens the tool's values (tried at once), left / right turn the one
         * in hand, BACK returns to the names (the pattern as it was), OK does it; a tool with no values: OK does it at once */
        KApp z; uint16_t was;
        k_init(&z); k_set_layout(&z, K_LAY_SHEET_CURSOR); k_open_page(&z, K_PAGE_ROLL); z.playing = 0; was = z.steps[0][0];
        k_dispatch(&z, (KAction){K_ACT_TOOLS, 0, 0, 0}); assert(z.tools_open && !z.tools_stage);
        k_dispatch(&z, (KAction){K_ACT_RIGHT, 0, 0, 0}); assert(!z.tools_stage && z.tools_try < 0);   /* (the names: nothing turns) */
        k_dispatch(&z, (KAction){K_ACT_DOWN, 0, 0, 0}); assert(k_tools_item(&z, z.tools_sel) == K_TOOL_EUCLID);
        k_dispatch(&z, (KAction){K_ACT_ACCEPT, 0, 0, 0}); assert(z.tools_stage == 1 && z.tools_try == z.tools_sel && z.steps[0][0] == k_euclid(z.tool_x[K_TOOL_EUCLID], z.tool_y[K_TOOL_EUCLID]));
        {   const int h = z.tool_x[K_TOOL_EUCLID];
            k_dispatch(&z, (KAction){K_ACT_RIGHT, 0, 0, 0}); assert(z.tool_x[K_TOOL_EUCLID] == h + 1);
            k_dispatch(&z, (KAction){K_ACT_DOWN, 0, 0, 0}); assert(z.tools_which == 1);
            k_dispatch(&z, (KAction){K_ACT_RIGHT, 0, 0, 0}); assert(z.tool_y[K_TOOL_EUCLID] == 1 && z.steps[0][0] == k_euclid(h + 1, 1)); }
        k_dispatch(&z, (KAction){K_ACT_BACK, 0, 0, 0}); assert(z.tools_open && !z.tools_stage && z.steps[0][0] == was);   /* back: as it was */
        k_dispatch(&z, (KAction){K_ACT_ACCEPT, 0, 0, 0}); k_dispatch(&z, (KAction){K_ACT_ACCEPT, 0, 0, 0});
        assert(!z.tools_open && z.steps[0][0] == k_euclid(z.tool_x[K_TOOL_EUCLID], z.tool_y[K_TOOL_EUCLID]));   /* done */
        k_dispatch(&z, (KAction){K_ACT_TOOLS, 0, 0, 0}); z.tools_sel = 6; assert(k_tools_item(&z, 6) == K_TOOL_REVERSE);
        was = z.steps[0][0]; k_dispatch(&z, (KAction){K_ACT_ACCEPT, 0, 0, 0}); assert(!z.tools_open && z.steps[0][0] != was);   /* no values: at once */
    }
    {   /* a note lengthened stops at the next note on its row (the tracker: at the column's next note, any pitch) */
        KApp q; int s1, s2;
        k_init(&q); memset(q.roll, 0, sizeof q.roll); memset(q.steps, 0, sizeof q.steps);
        s1 = k_roll_add(&q, 0, 0, 2, 60, 1, 100); k_roll_add(&q, 0, 0, 6, 60, 1, 100); k_roll_add(&q, 0, 0, 4, 64, 1, 100);
        k_roll_set(&q, 0, s1, 2, 60, 9); assert(q.roll[0][s1].len == 4);   /* (60 at step 6: up to it) */
        s2 = k_roll_add(&q, 0, 0, 0, 60, 16, 100); assert(q.roll[0][s2].len == 2);   /* (a new one too) */
        q.rows_view = 1; k_roll_set(&q, 0, s1, 2, 60, 9); assert(q.roll[0][s1].len == 2);   /* the tracker: 64 at step 4 */
        k_roll_set(&q, 0, s1, 4, 64, 1); assert((q.roll[0][s1].at & 15) == 2 && q.roll[0][s1].note == 60);   /* (onto 64 at step 4: refused) */
    }
    {   /* ROLL's NOTE turned through a chord: the chord's other notes passed over, not taken away; LEN stops at the next note */
        KApp c; int a1, n0 = 0, k;
        k_init(&c); k_open_page(&c, K_PAGE_ROLL); c.track = 0; memset(c.roll, 0, sizeof c.roll); memset(c.steps, 0, sizeof c.steps);
        a1 = k_roll_add(&c, 0, c.pattern[0], 3, 60, 1, 100); k_roll_add(&c, 0, c.pattern[0], 3, 61, 1, 100); k_roll_add(&c, 0, c.pattern[0], 5, 60, 1, 100);
        c.cur[0] = 3; k_roll_select(&c, 0, a1);
        k_dispatch(&c, (KAction){K_ACT_PARAM_DELTA, 1, 1, 0});   /* NOTE (K2) up one: 61 is the chord's: on to 62 */
        for (k = 0; k < K_ROLL_NOTES; k++) n0 += c.roll[0][k].len != 0;
        assert(n0 == 3 && c.roll[0][a1].note == 62);
        k_dispatch(&c, (KAction){K_ACT_PARAM_DELTA, 1, -1, 0});   /* down one detent: past 61, to 60 */
        for (k = 0; k < 8; k++) k_dispatch(&c, (KAction){K_ACT_PARAM_DELTA, 2, 1, 0});   /* LEN (K3) up */
        assert(c.roll[0][a1].note == 60 && c.roll[0][a1].len == 2);   /* (60 at step 5: it stops there) */
    }
    {   /* the tracker on a drum track: the first eight white keys the lanes, a press a hit on the row in hand, again none */
        KApp d; int e;
        k_init(&d); for (e = 0; e < k_engine_count(); e++) if (k_engine_uid(e) == K_UID('D', 'R', 'U', 'M')) k_track_engine(&d, 1, e);
        d.track = 1; k_open_page(&d, K_PAGE_DRUM); d.rows_view = 1; d.cur[1] = 5; memset(d.lane_on[1], 0, sizeof d.lane_on[1]);
        k_dispatch(&d, (KAction){K_ACT_KEY, 2, 1, 0}); k_dispatch(&d, (KAction){K_ACT_KEY, 2, 0, 1});
        assert(d.lane_on[1][2] == 1u << 5 && d.cur_lane[1] == 2);
        k_dispatch(&d, (KAction){K_ACT_KEY, 2, 1, 2}); assert(d.lane_on[1][2] == 0);
    }
    {   /* SEQ pressed again on a SEQ page: ROLL, STEP, the tracker view, ROLL (STEP must stay reachable: the FM-1 has no page
         * menu); the step's chance on its fourth card */
        KApp v; k_init(&v); k_open_page(&v, K_PAGE_ROLL);
        k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_PATTERN, 0, 100}); k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_PATTERN, 0, 150});
        assert(!v.rows_view && v.page == K_PAGE_STEP);
        k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_PATTERN, 0, 160}); k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_PATTERN, 0, 170});
        assert(v.rows_view && v.page == K_PAGE_STEP);
        k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_PATTERN, 0, 200}); k_dispatch(&v, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_PATTERN, 0, 250});
        assert(!v.rows_view && v.page == K_PAGE_ROLL);
        k_open_page(&v, K_PAGE_STEP); v.cur[v.track] = 6;
        assert(k_param_ref(&v, K_PAGE_STEP, 3).id == K_P_CHANCE && k_param_get(&v, K_PAGE_STEP, 3) == 0);
        k_param_set(&v, K_PAGE_STEP, 3, 2); assert(k_chance(&v, v.track, v.pattern[v.track], 6) == 2 && k_chance(&v, v.track, v.pattern[v.track], 5) == 0);
        k_dispatch(&v, (KAction){K_ACT_PARAM_DELTA, 3, 1, 300}); assert(k_chance(&v, v.track, v.pattern[v.track], 6) == 3);   /* K4 turns it */
    }
    {   /* the tracker view: up / down the step, left / right the track on the same row, OK a note or none */
        KApp r; int k, n = 0;
        k_init(&r); k_open_page(&r, K_PAGE_ROLL); r.rows_view = 1; r.track = 0; r.cur[0] = 0;
        memset(r.roll, 0, sizeof r.roll); memset(r.steps, 0, sizeof r.steps);
        k_dispatch(&r, (KAction){K_ACT_DOWN, 0, 0, 0}); k_dispatch(&r, (KAction){K_ACT_DOWN, 0, 0, 0}); assert(r.cur[0] == 2);
        k_dispatch(&r, (KAction){K_ACT_UP, 0, 0, 0}); assert(r.cur[0] == 1);
        r.rnote[0] = 62; r.rlen[0] = 2; r.rvel[0] = 90;
        k_dispatch(&r, (KAction){K_ACT_ACCEPT, 0, 0, 0});
        for (k = 0; k < K_ROLL_NOTES; k++) if (r.roll[0][k].len) { n++; assert(r.roll[0][k].note == 62 && (r.roll[0][k].at & 15) == 1 && r.roll[0][k].len == 2 && r.roll[0][k].vel == 90); }
        assert(n == 1);
        k_dispatch(&r, (KAction){K_ACT_ACCEPT, 0, 0, 0}); assert(!k_roll_count(&r, 0, r.pattern[0], 1));   /* again: the step emptied */
        k_dispatch(&r, (KAction){K_ACT_RIGHT, 0, 0, 0}); assert(r.track == 1 && r.cur[1] == 1);   /* the next track, the same row */
        if (k_is_drum(&r, 1)) {   /* (a drum track: its page, the view kept; OK the lane in hand's step) */
            const uint16_t was = r.lane_on[1][r.cur_lane[1]];
            assert(r.page == K_PAGE_DRUM && r.rows_view);
            k_dispatch(&r, (KAction){K_ACT_ACCEPT, 0, 0, 0}); assert(r.lane_on[1][r.cur_lane[1]] == (was ^ 2));
        }
        k_dispatch(&r, (KAction){K_ACT_LEFT, 0, 0, 0}); k_dispatch(&r, (KAction){K_ACT_LEFT, 0, 0, 0}); assert(r.track == 0);   /* (stops at the ends) */
        k_dispatch(&r, (KAction){K_ACT_FOCUS_NEXT, 0, 0, 0}); assert(r.track == 1 && r.cur[1] == 1);   /* SELECT: the next column, the same row */
        k_dispatch(&r, (KAction){K_ACT_FOCUS_PREV, 0, 0, 0}); assert(r.track == 0);
    }
    {   /* a drum track: a tool on pattern A, then pattern B in hand: UNDO puts A's lane back, B's left alone */
        KApp u; k_init(&u); u.sound[1] = K_ENGINE_COUNT - 1; u.track = 1; u.cur_lane[1] = 0; u.lane_on[1][0] = 0x0001;
        assert(k_seq_tool(&u, K_TOOL_FILL, 4, 0) && u.lane_on[1][0] == 0x1111);
        assert(k_pattern_set(&u, 1, 1)); u.lane_on[1][0] = 0x00F0;
        assert(k_seq_tool(&u, K_TOOL_UNDO, 0, 0) && k_lanes(&u, 1, 0)[0] == 0x0001 && u.lane_on[1][0] == 0x00F0);
    }
    {   /* SONG: HOME + SELECT (the track) takes the cursor to that track's row, the scene kept */
        KApp g; k_init(&g); k_open_page(&g, K_PAGE_SONG); g.track = 0; g.focused = 0 * K_SCENES + 3;
        k_dispatch(&g, (KAction){K_ACT_TRACK_DELTA, 1, 0, 0});
        assert(g.track == 1 && g.focus[g.focused].kind == K_NODE_SCENE && g.focus[g.focused].index == 1 * K_SCENES + 3);
    }
    {   /* SEQ held + SELECT (the FM-1's way in): from HOME onto the SEQ page with the tools open, its release then
         * does nothing; HOME closes. Held alone: nothing */
        KApp c; k_init(&c); k_open_page(&c, K_PAGE_HOME);
        k_dispatch(&c, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_PATTERN, 0, 100});
        k_tick(&c, 900); assert(!c.tools_open && !c.menu_open && c.page == K_PAGE_HOME);
        k_dispatch(&c, (KAction){K_ACT_SELECT_DELTA, 1, 0, 950}); assert(c.tools_open && c.page == K_PAGE_ROLL && !c.menu_open);
        k_dispatch(&c, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_PATTERN, 0, 500}); assert(c.tools_open && c.page == K_PAGE_ROLL);
        k_dispatch(&c, (KAction){K_ACT_HOME_DOWN, 0, 0, 600}); k_dispatch(&c, (KAction){K_ACT_HOME_UP, 0, 0, 650});
        assert(!c.tools_open && c.page == K_PAGE_ROLL);
        k_dispatch(&c, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_PATTERN, 0, 700});   /* a short SEQ: its next page, as before */
        k_dispatch(&c, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_PATTERN, 0, 750}); assert(!c.tools_open && c.page == K_PAGE_STEP);
    }
    {   /* SONG: insert, duplicate, delete, clear on the scene in hand; UNDO swaps back */
        KApp d; uint8_t was[K_SCENES][K_TRACKS]; int s2;
        k_init(&d); k_open_page(&d, K_PAGE_SONG);
        for (i = 0; i < d.focus_count; i++) if (d.focus[i].kind == K_NODE_SCENE && d.focus[i].index == 2) d.focused = i;   /* track 1, scene 3 */
        memcpy(was, d.song, sizeof was);
        assert(k_tools_page(&d) && k_tools_item(&d, 0) == K_TOOL_SC_INSERT);
        assert(k_seq_tool(&d, K_TOOL_SC_INSERT, 0, 0));
        for (k = 0; k < K_TRACKS; k++) assert(d.song[2][k] == 4 && d.song[3][k] == was[2][k] && d.song[1][k] == was[1][k] && d.song[7][k] == was[6][k]);
        assert(k_seq_tool(&d, K_TOOL_UNDO, 0, 0) && !memcmp(d.song, was, sizeof was));
        assert(k_seq_tool(&d, K_TOOL_SC_DUP, 5, 0));   /* scene 3 onto scene 6, the rest as they were */
        for (k = 0; k < K_TRACKS; k++) assert(d.song[5][k] == was[2][k] && d.song[2][k] == was[2][k] && d.song[3][k] == was[3][k] && d.song[6][k] == was[6][k]);
        memcpy(d.song, was, sizeof was);
        assert(k_seq_tool(&d, K_TOOL_SC_DELETE, 0, 0));
        for (k = 0; k < K_TRACKS; k++) assert(d.song[2][k] == was[3][k] && d.song[6][k] == was[7][k] && d.song[7][k] == 4);
        assert(k_seq_tool(&d, K_TOOL_SC_CLEAR, 1, 0));
        for (s2 = 0; s2 < K_SCENES; s2++) for (k = 0; k < K_TRACKS; k++) assert(d.song[s2][k] == 4);
    }
    {   /* SOUND: RANDOM moves the values, UNDO brings them back; COPY / PASTE across tracks of one engine; INIT the defaults */
        KApp e; int v0[K_ENGINE_PARAMS], diff = 0; const KEngine *en;
        k_init(&e); e.track = 1; k_open_page(&e, K_PAGE_SOUND);
        en = k_engine_at(e.sound[1]);
        for (i = 0; i < en->nparams; i++) v0[i] = k_param_get(&e, K_PAGE_SOUND, i);
        assert(k_seq_tool(&e, K_TOOL_SN_RANDOM, 100, 0));
        for (i = 0; i < en->nparams; i++) { const int v = k_param_get(&e, K_PAGE_SOUND, i); diff += v != v0[i]; assert(v >= en->edit[i].min && v <= en->edit[i].max); }
        assert(diff > 0 && e.preset_dirty[1]);
        assert(k_seq_tool(&e, K_TOOL_SN_COPY, 0, 0) && e.clip_kind == K_CLIP_SOUND);
        assert(k_seq_tool(&e, K_TOOL_UNDO, 0, 0));
        for (i = 0; i < en->nparams; i++) assert(k_param_get(&e, K_PAGE_SOUND, i) == v0[i]);
        e.track = 2;
        if (e.sound[2] == e.sound[1]) {
            assert(k_seq_tool(&e, K_TOOL_SN_PASTE, 0, 0)); diff = 0;
            for (i = 0; i < en->nparams; i++) diff += k_param_get(&e, K_PAGE_SOUND, i) != v0[i];
            assert(diff > 0);
            assert(k_seq_tool(&e, K_TOOL_SN_INIT, 0, 0));
            for (i = 0; i < en->nparams; i++) if (en->edit[i].fmt != KF_LINK_FENV) assert(k_param_get(&e, K_PAGE_SOUND, i) == k_param_clamp(&en->edit[i], en->edit[i].def));
        }
    }
    {   /* MIXER: every track's level, pan, sends, mute and solo; FX: swap, empty, copy to another track */
        KApp m; k_init(&m); k_open_page(&m, K_PAGE_MIXER);
        m.params[1][K_PAGE_MIXER][0] = 10; m.params[2][K_PAGE_MIXER][1] = 0; m.params[3][K_PAGE_MIXER][2] = 70; m.mute = 3; m.solo = 4;
        assert(k_seq_tool(&m, K_TOOL_MX_LEVELS, 0, 0) && m.params[1][K_PAGE_MIXER][0] == 88);
        assert(k_seq_tool(&m, K_TOOL_MX_PANS, 0, 0) && m.params[2][K_PAGE_MIXER][1] == 50);
        assert(k_seq_tool(&m, K_TOOL_MX_SENDS, 0, 0) && m.params[3][K_PAGE_MIXER][2] == 0);
        assert(k_seq_tool(&m, K_TOOL_MX_UNMUTE, 0, 0) && !m.mute && !m.solo);
        assert(k_seq_tool(&m, K_TOOL_UNDO, 0, 0) && m.mute == 3 && m.solo == 4);
        m.track = 0; m.params[0][K_PAGE_FX][0] = 3; m.params[0][K_PAGE_FX][1] = 20; m.params[0][K_PAGE_FX][4] = 5; m.params[0][K_PAGE_FX][8] = 11;
        assert(k_seq_tool(&m, K_TOOL_FX_SWAP, 0, 0) && m.params[0][K_PAGE_FX][0] == 5 && m.params[0][K_PAGE_FX][4] == 3 && m.params[0][K_PAGE_FX][5] == 20 && m.params[0][K_PAGE_FX][16] == 11);
        assert(k_seq_tool(&m, K_TOOL_FX_COPY, 0, 0));
        assert(k_seq_tool(&m, K_TOOL_FX_INIT, 0, 0) && m.params[0][K_PAGE_FX][0] == 0 && m.params[0][K_PAGE_FX][4] == 3);
        m.track = 1; assert(k_seq_tool(&m, K_TOOL_FX_PASTE, 0, 0) && m.params[1][K_PAGE_FX][0] == 5 && m.params[1][K_PAGE_FX][4] == 3);
    }
    {   /* the FM-1: a page key held + SELECT: its page's tools (GLO on MIXER, SYNTH anywhere, FX); GLO from HOME: MIXER's */
        KApp h; k_init(&h); k_open_page(&h, K_PAGE_MIXER);
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 100}); k_dispatch(&h, (KAction){K_ACT_SELECT_DELTA, 1, 0, 150});
        assert(h.tools_open && h.page == K_PAGE_MIXER && !h.menu_open && k_tools_item(&h, 0) == K_TOOL_MX_UNMUTE);
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_GLO, 0, 500}); assert(h.tools_open && h.page == K_PAGE_MIXER);
        k_dispatch(&h, (KAction){K_ACT_BACK, 0, 0, 510}); assert(!h.tools_open);
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_SOUND, 0, 600}); k_dispatch(&h, (KAction){K_ACT_SELECT_DELTA, 1, 0, 650});
        assert(h.tools_open && h.page == K_PAGE_SOUND && k_tools_item(&h, 0) == K_TOOL_SN_RANDOM);
        {   int v0[K_ENGINE_PARAMS], diff = 0; const KEngine *en = k_engine_at(h.sound[h.track]);
            for (i = 0; i < en->nparams; i++) v0[i] = k_param_get(&h, K_PAGE_SOUND, i);
            k_dispatch(&h, (KAction){K_ACT_PARAM_DELTA, 0, 2, 0});   /* AMT 35: tried at once */
            for (i = 0; i < en->nparams; i++) diff += k_param_get(&h, K_PAGE_SOUND, i) != v0[i];
            assert(diff > 0 && h.tools_try == 0 && h.tool_x[K_TOOL_SN_RANDOM] == 35);
            k_dispatch(&h, (KAction){K_ACT_BACK, 0, 0, 0}); assert(!h.tools_open);   /* let go: as it was */
            for (i = 0; i < en->nparams; i++) assert(k_param_get(&h, K_PAGE_SOUND, i) == v0[i]);
            k_dispatch(&h, (KAction){K_ACT_TOOLS, 0, 0, 0}); }
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_SOUND, 0, 1000}); k_dispatch(&h, (KAction){K_ACT_BACK, 0, 0, 1010});
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_FX, 0, 1100}); k_dispatch(&h, (KAction){K_ACT_SELECT_DELTA, 1, 0, 1150});
        assert(h.tools_open && h.page == K_PAGE_FX && k_tools_item(&h, 0) == K_TOOL_FX_SWAP);
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_FX, 0, 1500}); k_dispatch(&h, (KAction){K_ACT_BACK, 0, 0, 1510});
        k_open_page(&h, K_PAGE_HOME);
        k_dispatch(&h, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_GLO, 0, 1600}); k_tick(&h, 1960);
        assert(!h.tools_open && !h.menu_open && h.page == K_PAGE_HOME);   /* (held alone: nothing) */
        k_dispatch(&h, (KAction){K_ACT_SELECT_DELTA, -1, 0, 1970}); assert(h.tools_open && h.page == K_PAGE_MIXER);
    }
    {   /* SONG's blocks: a scene's column copied onto another, a track's row emptied; UNDO brings it back */
        KApp g; int k3;
        k_init(&g);
        for (k3 = 0; k3 < K_SCENES; k3++) { int t3; for (t3 = 0; t3 < K_TRACKS; t3++) g.song[k3][t3] = (uint8_t)((k3 / 2 + t3) % 4); }   /* (a song: the default is empty) */
        assert(k_song_block(&g, 0, 1, 0) && k_song_block(&g, 1, 1, 5));
        for (k3 = 0; k3 < K_TRACKS; k3++) assert(g.song[5][k3] == g.song[0][k3]);
        assert(!k_song_block(&g, 1, 2, 0));   /* (a column does not paste over a row) */
        assert(k_song_block(&g, 2, 2, 1));
        for (k3 = 0; k3 < K_SCENES; k3++) assert(g.song[k3][1] == 4);
        assert(k_seq_tool(&g, K_TOOL_UNDO, 0, 0) && g.song[0][1] != 4);
    }
    {   /* CHANCE: the step in hand's chance (a drum key's last toggled step, the cursor's), or every step's; UNDO; a song keeps them,
         * a song without them (PROB) plays every step; SHIFT takes them round with the notes, CLEAR forgets them */
        static KApp g, h;
        static uint8_t buf[8192];
        size_t len;
        k_init(&g);
        g.track = 0; g.page = K_PAGE_STEP; g.cur[0] = 5; g.pattern[0] = 1;
        assert(k_chance(&g, 0, 1, 5) == 0 && k_chance_pct(0) == 100 && k_chance_pct(3) == 25);
        assert(k_seq_tool(&g, K_TOOL_CHANCE, 50, 0) && k_chance(&g, 0, 1, 5) == 2 && g.chance[0][1] == 2u << 10 && !g.chance[0][0]);
        {   char b[8]; g.tool_x[K_TOOL_CHANCE] = 75; g.tool_y[K_TOOL_CHANCE] = 0;
            k_tool_val_text(&g, K_TOOL_CHANCE, 0, b, sizeof b); assert(!strcmp(b, "75%"));
            k_tool_val_text(&g, K_TOOL_CHANCE, 1, b, sizeof b); assert(!strcmp(b, "6"));   /* (the step in hand, numbered from 1) */
            assert(k_tool_turn(&g, K_TOOL_CHANCE, 0, 1) && g.tool_x[K_TOOL_CHANCE] == 100 && !k_tool_turn(&g, K_TOOL_CHANCE, 0, 1));
            assert(k_tool_set(&g, K_TOOL_CHANCE, 0, 30) && g.tool_x[K_TOOL_CHANCE] == 25); }
        assert(k_seq_tool(&g, K_TOOL_UNDO, 0, 0) && g.chance[0][1] == 0);
        assert(k_seq_tool(&g, K_TOOL_UNDO, 0, 0) && k_chance(&g, 0, 1, 5) == 2);   /* (redone) */
        assert(k_seq_tool(&g, K_TOOL_CHANCE, 25, 1) && g.chance[0][1] == 0xFFFFFFFFu);
        k_roll_fill(&g, 0, 1, 0x0001u, 60); k_chance_set(&g, 0, 1, 0, 1); k_chance_set(&g, 0, 1, 1, 0);
        g.chance[0][1] = 1u;   /* (step 0: 75 %) */
        assert(k_seq_tool(&g, K_TOOL_SHIFT, 2, 0) && g.chance[0][1] == 1u << 4 && k_chance(&g, 0, 1, 2) == 1);
        len = k_song_save(&g, buf, sizeof buf); assert(len);
        k_init(&h); assert(k_song_load(&h, buf, len) == K_SONG_OK && h.chance[0][1] == 1u << 4 && !h.chance[0][0] && !h.chance[1][1]);
        assert(k_seq_tool(&g, K_TOOL_CLEAR, 0, 0) && g.chance[0][1] == 0);
        len = k_song_save(&g, buf, sizeof buf);   /* (no chance left: no PROB, as a song before) */
        for (size_t q = 0; q + 4 <= len; q++) assert(memcmp(buf + q, "PROB", 4));
        k_init(&h); h.chance[0][1] = 7; assert(k_song_load(&h, buf, len) == K_SONG_OK && h.chance[0][1] == 0);
        {   /* a drum track: a key's step is the one in hand */
            int d = k_engine_find(K_UID('D', 'R', 'U', 'M'));
            k_init(&g); k_track_engine(&g, 0, d); g.track = 0; g.page = K_PAGE_DRUM; g.focus_count = 0;
            k_lane_step_toggle(&g, 0, 2, 9);
            assert(g.cur[0] == 9 && k_seq_tool(&g, K_TOOL_CHANCE, 25, 0) && k_chance(&g, 0, g.pattern[0], 9) == 3);
            assert(k_seq_tool(&g, K_TOOL_CLEAR, 1, 0) && !g.chance[0][g.pattern[0]]);   /* (every lane: the chances too) */
        }
    }
    printf("seqtool_test: ok\n");
    return 0;
}
