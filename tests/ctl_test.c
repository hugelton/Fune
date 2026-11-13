/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Controller profiles (kreisel_ctl.h): the text read, a port chosen by its name, a controller's rows ahead of the platform's. */
#include "kreisel_ctl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static KApp a;
static void midi(int dev, uint8_t s, uint8_t d1, uint8_t d2) { const uint8_t b[3] = {s, d1, d2}; k_io_midi_dev(&a, 1, dev, b, 3, 100); }

int main(void)
{
    static KCtlProfile p, q;
    char err[64];
    int i;
    /* every built-in reads */
    for (i = 0; i < k_ctl_builtin_n; i++) { assert(k_ctl_parse(k_ctl_builtin[i], &p, err, sizeof err) == 0); assert(p.name[0] && p.n_midi > 0); }
    /* a user's: comments, channels, every kind of row; matched by a part of the name, any case */
    assert(k_ctl_parse("# mine\nname = My Box\nmatch = mybox\n\ncc 74 = knob 1   # cutoff\ncc 20 ch 2 = rel 2\ncc 7 = level\n"
                       "cc 8 = level 3\ncc 10 = pan\ncc 1 = macro 2\ncc 41 = play\nnote 36 = rec\nnote any = note\ncc 50 = page fx\n"
                       "key f = up\nkey f1 = page seq\n", &q, err, sizeof err) == 0);
    assert(!strcmp(q.name, "My Box") && q.n_midi == 10 && q.n_keys == 2);
    assert(k_ctl_matches(&q, "USB MyBOX MIDI 1") && !k_ctl_matches(&q, "IAC Driver"));
    assert(q.midi[1].ch == 2 && q.midi[1].act == KIO_DO_REL && q.midi[1].a == 1);
    /* what it does not read: the line said */
    assert(k_ctl_parse("name = x\ncc 300 = knob 1\n", &p, err, sizeof err) == -2 && strstr(err, "line 2"));
    assert(k_ctl_parse("name = x\ncc 3 = knob 9\n", &p, err, sizeof err) == -2);
    assert(k_ctl_parse("name = x\nkey a = knob 1\n", &p, err, sizeof err) == -2);   /* (a key has no value) */
    assert(k_ctl_parse("cc 3 = play\n", &p, err, sizeof err) < 0);                   /* (no name) */

    /* in use: device 1 has the profile, device 0 (none) the platform's table only */
    k_init(&a);
    k_io_midi_map(k_io_map_desk, k_io_map_desk_n);
    k_io_dev_map(1, q.midi, q.n_midi);
    k_open_page(&a, K_PAGE_SOUND);
    {   int was = k_param_get(&a, K_PAGE_SOUND, a.win);
        midi(1, 0xB0, 74, 0); assert(k_param_get(&a, K_PAGE_SOUND, a.win) <= was);     /* knob 1 to its bottom */
        midi(1, 0xB0, 74, 127); assert(k_param_get(&a, K_PAGE_SOUND, a.win) >= was); }  /* and its top */
    {   int was = k_param_get(&a, K_PAGE_SOUND, a.win + 1);
        midi(1, 0xB1, 20, 1); assert(k_param_get(&a, K_PAGE_SOUND, a.win + 1) != was || was >= 127);   /* rel 2 on channel 2 */
        was = k_param_get(&a, K_PAGE_SOUND, a.win + 1);
        midi(1, 0xB0, 20, 1); assert(k_param_get(&a, K_PAGE_SOUND, a.win + 1) == was); }   /* (channel 1: not this row) */
    a.track = 0;
    midi(1, 0xB0, 8, 0); assert(a.params[2][K_PAGE_MIXER][0] == 0 && a.track == 0);   /* level 3: track 3's fader */
    midi(1, 0xB0, 41, 127); assert(a.playing);                                        /* play: on its press */
    midi(1, 0xB0, 41, 0); assert(a.playing);                                          /* (its release: nothing) */
    midi(1, 0x90, 36, 100); assert(a.recording);                                          /* a pad's note as REC */
    midi(1, 0x90, 60, 90); assert(a.midi_on[60]);                                     /* note any: played */
    midi(1, 0xB0, 50, 127); assert(a.page == K_PAGE_FX);                              /* page fx */
    {   int lv = a.params[a.track][K_PAGE_MIXER][0];
        midi(0, 0xB0, 74, 0); assert(a.params[a.track][K_PAGE_MIXER][0] == lv); }       /* device 0: CC 74 is nothing of the desk's */
    k_io_dev_map(1, 0, 0);

    /* MIDI learn: the settings' Learn row, a control on the page pointed at (not turned), the next CC bound to it; the row into a
     * profile's text, which read in does it */
    {   static char text[4096], text2[4096];
        static KCtlProfile r;
        int was;
        k_init(&a);
        k_io_midi_map(k_io_map_desk, k_io_map_desk_n);
        k_open_page(&a, K_PAGE_SOUND);
        a.settings_open = 1; a.settings_sel = K_SET_MIDI_LEARN;
        assert(!strcmp(k_setting_name(K_SET_MIDI_LEARN), "Learn") && !strcmp(k_setting_tab_name(k_setting_tab(K_SET_MIDI_LEARN)), "Connect"));
        k_dispatch(&a, (KAction){K_ACT_ACCEPT, 0, 0, 10});
        assert(a.learn == 1 && !a.settings_open && a.balloon.kind == K_BALLOON_LEARN && !strcmp(a.learn_say, "Learn: a control"));
        was = k_param_get(&a, K_PAGE_SOUND, a.win);
        k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 0, 5, 20});                        /* knob 1 turned: pointed at */
        assert(a.learn == 2 && k_param_get(&a, K_PAGE_SOUND, a.win) == was && !strcmp(a.learn_say, "MIDI -> Knob 1"));
        midi(0, 0x90, 60, 90); assert(a.learn == 2 && a.midi_on[60]);                 /* (a note: not for a knob; played) */
        midi(0, 0x80, 60, 0);
        midi(0, 0xB0, 74, 10);                                                         /* the CC moved: bound */
        assert(a.learn == 3 && !strcmp(k_learn_row(&a), "cc 74 = knob 1") && !strcmp(a.learn_say, "CC 74 -> Knob 1"));
        assert(k_param_get(&a, K_PAGE_SOUND, a.win) == was);                          /* (not turned by it) */
        assert(k_ctl_learn_text(0, "USB Box", "USB Box", k_learn_row(&a), text, sizeof text) == 0);
        k_learn_done(&a, 1); assert(a.learn == 0 && !k_learn_row(&a));
        assert(k_ctl_parse(text, &r, err, sizeof err) == 0 && !strcmp(r.name, "USB Box") && k_ctl_matches(&r, "USB Box MIDI 1") && r.n_midi == 1);
        k_io_dev_map(1, r.midi, r.n_midi);
        midi(1, 0xB0, 74, 0); assert(k_param_get(&a, K_PAGE_SOUND, a.win) <= was);
        midi(1, 0xB0, 74, 127); assert(k_param_get(&a, K_PAGE_SOUND, a.win) >= was);
        /* a button: PLAY pointed at (not played), a pad's note on channel 10 */
        k_learn_start(&a);
        k_dispatch(&a, (KAction){K_ACT_PLAY, 0, 0, 30}); assert(!a.playing && a.learn == 2 && !strcmp(a.learn_say, "MIDI -> Play"));
        midi(0, 0x99, 36, 100); assert(!strcmp(k_learn_row(&a), "note 36 ch 10 = play") && !strcmp(a.learn_say, "Note 36 -> Play"));
        assert(k_ctl_learn_text(text, "x", "x", k_learn_row(&a), text2, sizeof text2) == 0);
        k_learn_done(&a, 1);
        /* the same CC again, to knob 2: its old row goes */
        k_learn_start(&a);
        k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, 1, -1, 40});
        midi(0, 0xB0, 74, 64); assert(!strcmp(k_learn_row(&a), "cc 74 = knob 2"));
        assert(k_ctl_learn_text(text2, "x", "x", k_learn_row(&a), text, sizeof text) == 0);
        k_learn_done(&a, 0); assert(!a.learn && !strcmp(a.learn_say, "Not saved"));
        assert(k_ctl_parse(text, &r, err, sizeof err) == 0 && !strcmp(r.name, "USB Box") && r.n_midi == 2);
        assert(r.midi[0].in == KIO_NOTE && r.midi[0].ch == 10 && r.midi[1].num == 74 && r.midi[1].a == 1 && !strstr(text, "knob 1"));
        k_io_dev_map(1, r.midi, r.n_midi);
        midi(1, 0x99, 36, 100); assert(a.playing);
        /* the mixer's fader: level N; HOME's knobs: macros; a page key; Esc stops it */
        k_open_page(&a, K_PAGE_MIXER); a.track = 2;
        k_learn_start(&a); k_dispatch(&a, (KAction){K_ACT_PARAM_DELTA, -a.win, 1, 50}); assert(!strcmp(a.learn_what, "level 3"));
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_DOWN, K_GROUP_FX, 0, 60}); assert(!strcmp(a.learn_what, "page fx") && a.page == K_PAGE_MIXER);
        k_dispatch(&a, (KAction){K_ACT_PAGE_GROUP_UP, K_GROUP_FX, 0, 61}); assert(a.page == K_PAGE_MIXER && a.learn == 2);
        k_dispatch(&a, (KAction){K_ACT_BACK, 0, 0, 70}); assert(!a.learn && !strcmp(a.learn_say, "Learn off") && a.page == K_PAGE_MIXER);
        midi(0, 0xB0, 75, 3); assert(!a.learn);                                         /* (off: a CC is no binding) */
        assert(k_ctl_learn_text(0, "a", "a", "cc 300 = knob 1", text, sizeof text) < 0);   /* (not a row) */
        assert(k_ctl_learn_text(0, "a", "a", "cc 3 = knob 1", text, 20) < 0);            /* (no room) */
        k_io_dev_map(1, 0, 0);
    }
    printf("ctl_test: ok (%d built-in)\n", k_ctl_builtin_n);
    return 0;
}
