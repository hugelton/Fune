/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_CTL_H
#define KREISEL_CTL_H
/* Controller profiles ():
 * what a MIDI controller's knobs, faders and buttons do, as a text file the user can write (docs/CONTROLLERS.md). A profile is the
 * rows of an input table (KIOMap, kreisel_io.h) for the ports whose name holds its `match`; what it does not name goes to the
 * platform's own table (notes, the clock, the core's CCs). The built-in ones are the same text, compiled in (k_ctl_builtin).
 *
 *   # a comment
 *   name = nanoKONTROL2
 *   match = nanoKONTROL2            (a part of the port's name, any case; none: never chosen by itself)
 *   cc 16 = knob 1                  (the page in hand's first knob, the CC's 0 .. 127 across its range)
 *   cc 17 ch 2 = rel 2              (an endless encoder: 1 .. 63 up, 65 .. 127 down)
 *   cc 0 = level 1                  (track 1's level; "level" alone: the track in hand's)
 *   cc 41 = play                    (a button: on its press only)
 *   note 36 = rec                   (a pad's note as a button)
 *   note any = note                 (notes played on the track in hand)
 *   key f = up                      (a computer keyboard's key, for a keys profile: io.conf [keys] map = its name)
 *
 * The things a row can do: note, knob N, rel N, level [N], pan, macro N (1 .. 4), play, rec, undo, tools, ok, back, home, menu,
 * save, settings, track+ track-, select+ select-, pattern+ pattern-, value+ value-, octave+ octave-, up down left right,
 * page seq|glo|synth|edit|env|lfo|fx|arp. */
#include "kreisel_io.h"

#define K_CTL_ROWS 96
typedef struct {
    char name[32], match[32];
    KIOMap midi[K_CTL_ROWS], keys[K_CTL_ROWS];
    int n_midi, n_keys;
} KCtlProfile;

/* text into a profile: 0 when read; else -(the line) of the first line it could not read, err says why */
int k_ctl_parse(const char *text, KCtlProfile *out, char *err, int err_n);
/* 1 when the port's name holds the profile's match (any case) */
int k_ctl_matches(const KCtlProfile *p, const char *port_name);

/* MIDI learn's row (k_learn_row) into a profile's text: old's lines with any row of the same input (CC or note, channel) dropped,
 * then the row; no old text: a new profile (name, match). 0 when it fits in out */
int k_ctl_learn_text(const char *old, const char *name, const char *match, const char *row, char *out, int n);

extern const char *const k_ctl_builtin[];   /* the built-in profiles' text */
extern const int k_ctl_builtin_n;
#endif
