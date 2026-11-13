/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_IO_H
#define KREISEL_IO_H
/* The input layer: every platform hands its MIDI bytes and its keys to one entry
 * here; what they do comes from a table (KIOMap), not from code of the platform's own. A platform only carries: the FM-1 its USB and
 * TRS MIDI (its buttons and knobs stay KActions), the SDL builds their keys (later CoreMIDI, ALSA, RTP-MIDI, OSC). A row of a table is
 * a line of the settings file to come ([midi], [keys]). */
#include <stdint.h>
#include "kreisel.h"
#include "kreisel_audio.h"

enum { KIO_NOTE = 1, KIO_CC, KIO_REALTIME, KIO_KEY };            /* what comes in: a MIDI note (on / off), a control change, a realtime
                                                                   * byte (clock, start, stop), a key */
enum { KIO_DO_NOTE = 1, KIO_DO_CLOCK, KIO_DO_ACTION, KIO_DO_PAGE, KIO_DO_GROUP, KIO_DO_CC, KIO_DO_HOLD,
       KIO_DO_KNOB, KIO_DO_REL, KIO_DO_LEVEL };   /* what it does: a note on the track in hand
                                                                   * (k_note_in), the clock follower (k_clock_in), an action (a: its
                                                                   * KActionType, b: its argument), the page of the CC's value, a press of
                                                                   * a page button (a: its KPageGroup), the core's CC (k_cc_in: 7 level,
                                                                   * 10 pan, 16 .. 19 the macros of the track in hand), an action told the key's
                                                                   * press and release (a: its KActionType, its a 1 down, 0 up: K_ACT_GRAB);
                                                                   * a controller's (2026-11-12): KNOB the a-th knob of the page in hand to the
                                                                   * CC's value (0 .. 127 across its range), REL the a-th knob turned by an
                                                                   * endless encoder (1 .. 63 up, 65 .. 127 down), LEVEL track a's level (-1:
                                                                   * the track in hand's). A CC row's `repeat` 1: a button, done on its press
                                                                   * (a value of 64 or more) only */
enum {   /* keys past ASCII (a letter, a digit: its own lower-case code) */
    KIO_K_UP = 0x100, KIO_K_DOWN, KIO_K_LEFT, KIO_K_RIGHT, KIO_K_PGUP, KIO_K_PGDN, KIO_K_HOME, KIO_K_INS,
    KIO_K_F1 = 0x110   /* .. F12: KIO_K_F1 + 11 */
};
typedef struct {
    uint8_t in;       /* KIO_NOTE .. KIO_KEY */
    uint8_t ch;       /* MIDI: 1 .. 16, 0 any */
    int16_t num;      /* the note, the CC, the key's code; -1 any */
    uint8_t act;      /* KIO_DO_* */
    uint8_t repeat;   /* a key: held, it repeats */
    int16_t a, b;     /* the action's */
} KIOMap;

void k_io_midi_map(const KIOMap *map, int n);   /* the table MIDI is read by (the platform's own default) */
void k_io_key_map(const KIOMap *map, int n);    /* the table keys are read by; none: k_io_key takes no key */
/* MIDI bytes from a port (1 USB, 2 TRS, ...: the clock's sources too): running status, a realtime byte anywhere, SysEx skipped */
void k_io_midi(KApp *app, int port, const uint8_t *bytes, int n, uint32_t ms);
/* the same from a device that has a table of its own (a controller's profile, k_io_dev_map): dev 1 .. KIO_DEVS; its rows first, then
 * the platform's table for what it does not name */
#define KIO_DEVS 8
void k_io_midi_dev(KApp *app, int port, int dev, const uint8_t *bytes, int n, uint32_t ms);
void k_io_dev_map(int dev, const KIOMap *map, int n);
/* a key (KIO_K_* or ASCII), down or up, repeating: 1 when the table took it */
int k_io_key(KApp *app, int code, int down, int repeat, uint32_t ms);
/* MIDI in by name (io.conf's [midi] in = a part of a port's name; the Connect tab's MIDI in Named): "" none */
void k_io_set_in_name(const char *name);
/* MIDI out (the layer's other side): the platform's sender of one message (status and its data);
 * none: nothing sent. The core sends the notes its key bed plays, on the track's channel (track 1: channel 1) */
typedef void (*KIOSend)(const uint8_t *msg, int n);
void k_io_send_to(KIOSend send);
void k_io_note_out(int track, int note, int vel);   /* (vel 0: off) */
/* the sequencer's notes (k_audio_midi_pop) to the sender, from the main loop each round; with no sender the audio keeps none */
void k_io_send_audio(KAudio *audio);
const char *k_io_in_name(void);

#if K_WITH_CONNECT
/* MIDI learn (the SDL builds: the settings' Connect tab, Learn; docs/CONTROLLERS.md). KApp.learn: 1 the next control on the page is
 * pointed at (a knob or card turned, touched or clicked: knob N, on the mixer level N and pan, on HOME macro N; a track's tile: level
 * N; PLAY REC UNDO TOOLS SAVE, the menu, the settings key, track / select / pattern / octave steps, a page key), not turned; 2 the next
 * CC (any control) or note on (a button only) from a MIDI port is bound to it (another control pointed at first: that one instead);
 * 3 bound: the platform writes k_learn_row into the port's profile and says k_learn_done. BACK (Esc) or HOME stops it. A balloon
 * (K_BALLOON_LEARN, KApp.learn_say) says each stage: "Learn: a control", "MIDI -> Knob 1", "CC 74 -> Knob 1" */
void k_learn_start(KApp *app);
void k_learn_cancel(KApp *app);
int k_learn_action(KApp *app, KAction e);   /* (k_dispatch's, while learning) 1 when it took the action */
int k_learn_midi(KApp *app, int kind, int ch, int num, int value, uint32_t ms);   /* (the input layer's) kind 0xB0 0x90: 1 when bound */
const char *k_learn_row(const KApp *app);   /* stage 3: the row ("cc 74 = knob 1", "note 36 ch 10 = rec"); else 0 */
void k_learn_done(KApp *app, int written);  /* the row written (or not: "Not saved") */
#endif

extern const KIOMap k_io_map_fm1[];   /* the FM-1's MIDI: notes, the clock, CC 100 a page, CC 102 play (its tests) */
extern const int k_io_map_fm1_n;
extern const KIOMap k_io_map_desk[];  /* a computer's MIDI (the SDL builds): notes, the clock, the core's CCs */
extern const int k_io_map_desk_n;
extern const KIOMap k_io_map_zero[];  /* the Cardputer Zero's keys: its orange (Fn) keys without Fn */
extern const int k_io_map_zero_n;
#endif
