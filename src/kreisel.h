/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_H
#define KREISEL_H

#include <stdint.h>
#include "kreisel_engine.h"

#ifndef K_TRACKS
#define K_TRACKS 4           /* tracks: a platform's own (platforms/platforms.toml, -DK_TRACKS=N), 1..8 (mute and solo are bits of a byte) */
#endif
#if K_TRACKS < 1 || K_TRACKS > 8
#error "K_TRACKS is 1..8"
#endif
#define K_STEPS 16
/* HOME's items: the track in hand's four macros, then LEVEL of every track, PAN of every track, the colour of every track */
#define K_NOTE_Q 64          /* the live notes' queue (a power of two): a note-off is never dropped from it (63 held: the 27 keys and MIDI) */
#define K_ROLL_NOTES 64      /* the notes a synth track holds, its four patterns together (a song slot on the FM-1 is one 4 KB sector) */
typedef int16_t KVal;   /* a saved value (a page's parameter, a drum lane's): every range fits in 16 bits */
typedef struct { uint8_t at, len, note, vel; } KNote;   /* at: pattern << 4 | step; len 1..16 steps, 0 an empty slot; note, vel MIDI */
#define K_HOME_ITEMS (4 + 3 * K_TRACKS)
#define K_HOME_COLOR0 (4 + 2 * K_TRACKS)   /* the first colour item */
#define K_HOME_TRACK(i) (((i) - 4) % K_TRACKS)   /* the track of an item past the macros */
#define K_SCENES 256      /* a song's scenes, a bar each (256; it ends after the last one with anything in it) */
#define K_SONG_VIS 8      /* the scenes SONG shows at once (a window that follows the cursor and the song playing) */
#define K_MAX_PARAMS 24      /* the most parameters a page can have; all are saved, a device shows some of them */
/* the Switch's SOUND (K_LAY_SYNTH_ALL) holds the engine and then ENV 1's page (with ENV 2 paired: 16) and the LFO's (9): at most
 * K_SYNTH_ALL_MOD more (k_synth_all_sub) */
#define K_SYNTH_ALL_MOD 25
#define K_ITEM_SLOTS_A (K_HOME_ITEMS > K_MAX_PARAMS ? K_HOME_ITEMS : K_MAX_PARAMS)
#define K_ITEM_SLOTS (K_ITEM_SLOTS_A > K_ENGINE_PARAMS + K_SYNTH_ALL_MOD ? K_ITEM_SLOTS_A : K_ENGINE_PARAMS + K_SYNTH_ALL_MOD)   /* a page's items at most */
_Static_assert(K_ITEM_SLOTS <= 64, "KApp.touched is a bit an item");
#define K_DEFAULT_SHOWN 4
#define K_FX_TYPES 16        /* NONE plus 15 built-in effects; legacy ids 1..4 preserved */
#define K_FX_SLOTS 2
#define K_FX_EXTRA_PER_SLOT 8    /* extra parameters after the original TYPE / P0 / P1 / MIX */
#define K_FX_ITEMS (8 + K_FX_SLOTS * K_FX_EXTRA_PER_SLOT) /* 24 stored values per track */
static inline int k_fx_store_param(int slot, int param)
{
    return param < 2 ? slot * 4 + 1 + param : 8 + slot * K_FX_EXTRA_PER_SLOT + param - 2;
}
#define K_MAX_FOCUS 176       /* a drum page: 10 lanes of 16 steps, and the cards */
#define K_LANES 8
#define K_NAME_LEN 10         /* a song's or a preset's name: A-Z 0-9 space - . */
#define K_LANE_ENV 5          /* lane_p: where a lane's ENV values start (ATK PDEP PTIM CLIK) */
#define K_LANE_PAN 9          /* lane_p: its PAN, 0 left .. 50 centre .. 100 right */
#define K_LANE_VALS 10        /* lane_p: the values a lane keeps */
#define K_DRUM_PIECES 35     /* the pieces a drum lane can pick (drum_kreisel.c DP[], the DRUM engine's PIECE names): 31 recipes, 3 samples, TP ZP */
#define K_PALETTE 8          /* the colours a track can take (HOME: TRACK COLOR): the owner's table */

typedef enum {
    K_PAGE_HOME,   /* the four tracks, 2 x 2 */
    K_PAGE_STEP, K_PAGE_DRUM, K_PAGE_ROLL, K_PAGE_SONG, K_PAGE_SOUND,   /* SEQ: STEP the steps of a track, DRUM the lanes of a drum track, ROLL the notes of a track on a piano roll */
    K_PAGE_ENV1, K_PAGE_ENV2,
    K_PAGE_ENV_DEST,   /* not a page (2026-11-09): where ENV 1 and ENV 2 go (PIT FLT SHP AMP), kept here for the songs; ENV 1's and ENV 2's rows show them */
    K_PAGE_FX,
    K_PAGE_MIXER, K_PAGE_BUS1, K_PAGE_BUS2,   /* GLO: the mixer and the two effect buses */
    K_PAGE_LFO, K_PAGE_ARP, K_PAGE_SCL,   /* after Felucca 2.0: the LFO, the arp, the scale */
    K_PAGE_CLOCK,   /* not a page (2026-11-09): the tempo, swing and clock source (the song's), on SEQ's and SONG's cards and the header */
    K_PAGE_TRACKS,   /* the tracks one above the other, as a DAW shows them (from the page menu); HOME shows them 2 x 2 */
    K_PAGE_EDIT,     /* the engine's second half (edit[page2 ..]: ANALOG's filter), on the EDIT button; SOUND has the first */
    K_PAGE_COUNT
} KPage;
#define K_IS_HOME(p) ((p) == K_PAGE_HOME || (p) == K_PAGE_TRACKS)   /* the two views of the tracks: the same cards (levels, pans, colours) */

typedef enum {
    K_GROUP_PATTERN, K_GROUP_GLO, K_GROUP_SOUND, K_GROUP_ENV, K_GROUP_FX, K_GROUP_EDIT, K_GROUP_LFO, K_GROUP_ARP,
    K_GROUP_ALL, K_GROUP_COUNT
} KPageGroup;

typedef enum {
    K_NODE_TRACK, K_NODE_STEP, K_NODE_SCENE, K_NODE_PARAM, K_NODE_SOUND
} KNodeKind;

/* The balloon: what a control just changed, shown over the tempo in the header for a moment (MASTER with its gauge, the octave, the
 * preset, the page). It dissolves in, holds while it is refreshed, and dissolves out. All the timing is here, on one clock the caller
 * gives; the UI only asks how visible it is and what it is. The same kind again while it still shows only holds on (turning a knob
 * does not make it blink or move). */
typedef enum { K_BALLOON_NONE, K_BALLOON_MASTER, K_BALLOON_OCTAVE, K_BALLOON_PRESET, K_BALLOON_PAGE, K_BALLOON_SAVED, K_BALLOON_SAVE_FAIL, K_BALLOON_MISSING, K_BALLOON_HINT, K_BALLOON_NO_PRESET,
               K_BALLOON_LOADED, K_BALLOON_ERASED, K_BALLOON_NAMED, K_BALLOON_STOP, K_BALLOON_DIRTY, K_BALLOON_TRACKS, K_BALLOON_PATTERN, K_BALLOON_UNDO, K_BALLOON_LEARN } KBalloonKind;   /* DIRTY: PRESETS refused once (the preset in hand changed): it shakes, a second turn discards; TRACKS: a song of more tracks than here */   /* (SAVE: a slot loaded, erased, renamed; STOP TO SAVE) */   /* HINT: what a control does on the page (SYNTH: ALGORITHM moves the row) */   /* MISSING: a song loaded names a plugin not here */
/* the settings' rows (k_setting_name; HOME held opens them) */
#ifndef K_WITH_CONNECT   /* the Connect tab (MIDI in, its channel, the clock): the SDL builds, not the FM-1's */
#ifdef KREISEL_BITMAP_FONT
#define K_WITH_CONNECT 0
#else
#define K_WITH_CONNECT 1
#endif
#endif
enum { K_SET_LEDS, K_SET_ANIM, K_SET_SCREEN_OFF, K_SET_HELP, K_SET_RESTORE, K_SET_CLEAR, K_SET_ABOUT,
       K_SET_METRO, K_SET_CLICK, K_SET_COUNT,   /* the metronome (a setting, as usual): Off / On (only while REC), its sound, a bar counted in */
#if K_WITH_CONNECT
       K_SET_MIDI_IN, K_SET_MIDI_CH, K_SET_MIDI_CLOCK,   /* MIDI in: All, None, a name (io.conf); its channel: All, 1 .. 16; the clock: Internal, Follow (KApp.clock_src) */
       K_SET_MIDI_LEARN,   /* MIDI learn (kreisel_io.h k_learn_*): OK on it, then a control on the page, then a MIDI control: a profile's row */
#endif
       K_SET_USB_LEVEL,   /* the USB audio's level (a device with one: K_LAY_USB_AUDIO): Fixed, whatever the volume knob, or Volume */
       K_SETTINGS };
#define K_BALLOON_HOLD_MS 1500u
#define K_BALLOON_OUT_MS 250u
typedef struct { int kind; uint32_t in_ms, held_ms, step_ms; int8_t dir; } KBalloon;   /* step_ms, dir: PRESETS' last detent and its way (the name slides in from that side) */
void k_balloon_show(KBalloon *b, KBalloonKind kind, uint32_t now_ms);
int k_balloon_alpha(const KBalloon *b, uint32_t now_ms);    /* 0 (gone) .. 255 (fully there) */
int k_balloon_active(const KBalloon *b, uint32_t now_ms);   /* still to be drawn, or fading: the platform keeps drawing */

typedef struct { int16_t x, y, w, h; } KRect;   /* (16 bits: a screen is under 32768 units; the FM-1 keeps 176 of them in KApp) */
typedef struct { KNodeKind kind; int index; KRect bounds; } KFocusNode;

typedef enum {
    K_ACT_FOCUS_NEXT, K_ACT_FOCUS_PREV,
    K_ACT_UP, K_ACT_DOWN, K_ACT_LEFT, K_ACT_RIGHT,
    K_ACT_ACCEPT, K_ACT_BACK, K_ACT_PLAY,
    K_ACT_TRACK_DELTA, K_ACT_VALUE_DELTA,
    K_ACT_PARAM_DELTA,      /* a: the k-th VISIBLE parameter (a knob's position), b: delta. Not the parameter's own number. */
    K_ACT_PAGE_GROUP_DOWN, K_ACT_PAGE_GROUP_UP,
    K_ACT_PAGE_MENU, K_ACT_POINTER,
    K_ACT_SOUND_DELTA,      /* a: steps through the track's sound. */
    K_ACT_MASTER_DELTA,     /* a: output level. */
    K_ACT_OCTAVE_DELTA,     /* a: octave. */
    K_ACT_STEP_TOGGLE,      /* a: toggle step a of the current pattern (a key bed, where there is one). */
    K_ACT_SAVE,             /* SAVE: confirms in a menu; elsewhere opens the SAVE overlay (kreisel_save.h), and turns its side */
    K_ACT_REC,              /* Toggle recording (a red mark in the header; nothing is recorded yet). */
    K_ACT_LANE_SELECT,      /* a: the lane in hand on a DRUM page (a black key). */
    K_ACT_KEY,              /* a: a key of the bed (0..15 the white keys, 16..26 the black ones, left to right), b: 1 down, 0 up. What it does
                             * depends on the page: STEP and DRUM, the steps and the lanes; ROLL, a note (step entry). */
    K_ACT_HOME_DOWN,        /* the HOME button down: a short press is BACK (on release), held 500 ms it opens the settings */
    K_ACT_HOME_UP,
    K_ACT_SETTINGS,         /* open / close the settings (a platform with no HOME button to hold) */
    K_ACT_SELECT_DELTA,     /* a: the SELECT encoder (the FM-1's): the cursor (rows, steps, lanes, menus); with HOME held, the track */
    K_ACT_TYPE_DELTA,       /* a: the ALGORITHM encoder (the FM-1's): the engine (SOUND, EDIT, ENV, LFO) or the effect in hand (FX) */
    K_ACT_TOOLS,            /* open / close the page's tools (a long press: the platform tells it); k_tools_page says which have them */
    K_ACT_GRAB,             /* a: 1 down, 0 up: OK held as a grip (the roll as cells: held, the arrows edit the note under the cursor;
                             * let go unmoved, OK). Elsewhere its press is ACCEPT */
    K_ACT_UNDO              /* the last tool undone (again: redone), as HOME + REC: a desktop's Cmd+Z */
} KActionType;

typedef struct {
    KActionType type;
    int a, b;              /* Group/parameter and delta, or pointer x/y. */
    uint32_t time_ms;       /* Event clock, injected by the platform. */
} KAction;

typedef struct { uint8_t track, note, vel; } KNoteEv;   /* a live note for the sound: vel 0 = off */
/* The external clock follower's state (src/kreisel_clock.c). */
typedef struct {
    uint32_t pulses;        /* clocks since the last Start (a Continue picks up on the next step boundary) */
    uint32_t pend;          /* accepted clocks the platform has not yet handed to the audio (k_clock_take) */
    uint32_t gen;           /* counts Start and Continue, so the audio can tell a restart while playing */
    uint32_t last_ms;       /* when the last clock came (while playing) */
    uint32_t n, ts[24];     /* the tempo window: clocks seen and the times of the last 24 */
    uint8_t seen, cont;     /* seen: a clock since Start; cont: the last transport start was a Continue */
} KClock;

typedef struct {
    KPage page;
    int playing;
    int recording;
    KVal swing, clock_src;   /* the clock page: the swing (0..100) and where the clock comes from (INT USB TRS) */
    KVal chance_ui;         /* (the step in hand's chance as a value a card turns: k_param_at fills it, the turn packs it back) */
    uint8_t count_in;       /* PLAY counts a bar in first (the settings' Count-in, REC armed, the internal clock): the steps start at play_ms */
    int step;               /* the 16th being played, 0..15, -1 when stopped (counted from play_ms at the tempo). */
    int last_idx;           /* how many 16ths have been counted since play (-1: none yet) */
    uint32_t hit_ms[K_TRACKS];   /* when a track last sounded a step, and how hard: what the level meter shows until there is audio */
    uint8_t hit_v[K_TRACKS];
    uint8_t trk_lv[K_TRACKS], trk_live;   /* the tracks' meters from the audio (0..100 over 48 dB, falling), once it reports (k_set_track_peaks) */
    uint32_t trk_ms;
    int beat;               /* 0..3 while playing (the beat in the bar, from k_tick), -1 when stopped. */
    uint32_t play_ms;
    uint32_t clock_ms;      /* the last time the platform gave (an action or a tick). */
    KVal bpm;
    int track;
    int pattern[K_TRACKS];
    uint16_t steps[K_TRACKS][4];
    KNote roll[K_TRACKS][K_ROLL_NOTES];  /* a synth track's notes, its four patterns' together (KNote.at); steps[t][p] says which steps
                                          * of pattern p have one: the k_roll_* functions keep it */
    int rsel[K_TRACKS];                  /* ROLL: the note in hand (a slot of roll[t]), -1 none (the step in hand is empty) */
    KVal rnote[K_TRACKS], rlen[K_TRACKS], rvel[K_TRACKS];   /* its NOTE, LEN, VEL as ROLL's knobs turn them (the next note's when none) */
    KVal cur[K_TRACKS];                   /* the step the STEP and ROLL pages edit */
    uint32_t keys_down;                  /* bit id: the key (as K_ACT_KEY numbers it) is down */
    uint8_t key_note[27];                /* the note each down key started (its release stops that one, whatever the octave is by then) */
    uint8_t key_track[27];               /* the track each down key played (its release goes there, whatever track is in hand by then) */
    uint8_t midi_on[128];                /* a MIDI note sounding: 1 + the track it went to (0: none), and midi_lane what it pushed there */
    uint8_t midi_lane[128];
    struct { uint8_t t, at, note, on; } rec_held[8];   /* live REC: a note written while playing and still held (its length: at its release) */
    KNoteEv note_q[K_NOTE_Q];            /* live notes for the sound: keys and MIDI in, taken by the platform (k_take_note) */
    uint8_t note_r, note_w;
    KVal tcolor[K_TRACKS];                /* each track's colour: an index into the palette */
    uint8_t mute, solo;                  /* bit t: track t is muted / soloed (HOME: M and S; any solo silences the unsoloed tracks) */
    int entry_open;                      /* ROLL step entry: a key has written the step in hand, and the cursor moves on when all are up */
    uint8_t grab, grab_moved;            /* the roll as cells: OK held (K_ACT_GRAB), and the arrows have edited since */
    int16_t bend[K_TRACKS];              /* MIDI pitch bend in, a track's: -8192 .. 8191 (1/4096 semitone, +-2 semitones) */
    uint8_t rows_view;                   /* SEQ as a tracker: rows the steps, columns the tracks, the scene's patterns */
    uint16_t lane_on[K_TRACKS][K_LANES]; /* a drum track: the steps of each lane, the pattern in hand's (k_pattern_set swaps them) */
    uint16_t lane_pat[K_TRACKS][4][K_LANES];   /* the patterns' lanes (2026-11-12: a drum track's patterns differ); the one in hand's is in
                                          * lane_on, its row here stale: k_lanes reads either */
    int cur_lane[K_TRACKS];
    uint32_t chance[K_TRACKS][4];        /* each pattern's steps' chance to sound (2026-11-12), 2 bits a step (step s: bits 2s, 2s + 1): 0 always,
                                          * 1 75 %, 2 50 %, 3 25 % (a synth step's notes, a drum step's hits, together); 0 every step of a
                                          * song from before */
    KVal lane_p[K_TRACKS][K_LANES][K_LANE_VALS];   /* a drum lane: its piece, tune, tone, decay, level, then attack, pitch depth, pitch
                                          * time, click (K_LANE_ENV), then its pan (K_LANE_PAN) */
    /* an outside drum's values for each lane (a synth whose descriptor says drum: k_plugin_lane_params, its first ten at most; the
     * rest stay the track's, as DRUM's kit). One drum's at a time, the one whose id plug_lane_uid says (0: none, each lane then
     * plays the track's values, params[t][K_PAGE_SOUND][4 + k]); taken over, from the track's values, as the values of another
     * drum are first reached (kreisel_param.c lane_claim). A missing plugin's are kept as they were read (its id), and saved back */
    KVal plug_lane[K_TRACKS][K_LANES][K_LANE_VALS];
    uint32_t plug_lane_uid[K_TRACKS];
    uint8_t song[K_SCENES][K_TRACKS];
    uint8_t song_off;       /* SONG's window: the first scene shown (K_SONG_VIS of them) */
    uint8_t undo_song[K_SCENES][K_TRACKS];   /* the song as it was before a SONG tool (UNDO; undo_roll is too small for it) */
    uint8_t song_play;      /* PLAY on SONG: the song plays, a scene a bar from song_scene, the patterns as its cells say (the audio's) */
    int song_scene;         /* the scene it is on (the audio's, k_audio_song_follow) */
    int sound[K_TRACKS];
    int16_t eng_mem[K_TRACKS][K_ENGINE_COUNT + 8][K_ENGINE_PARAMS];   /* each engine's values as the track last had them: a switch of engine
                                                         * keeps them, and switching back brings that sound back (not saved in a
                                                         * song: only the engine in use is). (+ 8: the plugin slots, K_PLUGIN_SLOTS) */
    uint32_t eng_mem_set[K_TRACKS];                     /* bit e: engine e has values kept for the track */
    struct {                /* a track whose saved engine is not here (a plugin missing), as a DAW keeps one: its id, version and values
                             * untouched, written back on a save; the track plays silent and says NO PLUGIN. uid 0: none missing. */
        uint32_t uid;
        uint8_t version, n;
        uint8_t raw[K_ENGINE_PARAMS][3];  /* the values as saved: parameter id (2 bytes), value (biased) */
        uint8_t newer;                    /* 1: the engine is here, older than the song's: kept as a missing one (kreisel_engine_api.h) */
    } missing[K_TRACKS];
    KVal params[K_TRACKS][K_PAGE_COUNT][K_MAX_PARAMS];   /* (16 bits: every range fits; owner's RAM, 2026-11-12) */
    KFocusNode focus[K_MAX_FOCUS];
    int focus_count;
    int focused;
    int battery;            /* -1: none (a desktop); 0..4: the level the platform reports. */
    int charging;
    uint8_t arp_hist[K_TRACKS][8];   /* the arp's last eight notes on each track (pitches), the newest at arp_n - 1 (mod 8): the ARP page */
    uint8_t arp_n[K_TRACKS];
    uint32_t arp_ms[K_TRACKS];     /* when its last one played (the key lit, the bar glowing) */
    uint8_t out_lv[2];      /* the output's level, left and right, 0 (-48 dB or less) .. 100 (0 dB, the ceiling): the header's meter */
    uint32_t out_ms;        /* when it was last set (its fall) */
    int shown;              /* parameter cards a device shows at once: its knobs, or what its width holds. */
    int win;                /* the first parameter of that window; it slides with the focus. */
    unsigned lay;           /* K_LAY_*: how the device's screen joins pages (k_set_layout) */
    uint64_t touched;       /* bit i: parameter i was touched or turned, at touch_ms[i]; knobs turn together, so several cards open. */
    uint32_t touch_ms[K_ITEM_SLOTS];
    int menu_open;
    int menu_is_hold;
    int menu_changed;
    KPageGroup menu_group;
    int menu_selected;
    int menu_cols;          /* the page menu drawn as tiles, this many across (the UI sets it; 0 or 1: a list): up / down move a row of them */
    uint32_t act_n;         /* actions dispatched so far (the UI tells a pad's from its own touches: the cursor hides under a finger) */
    int master;             /* Output level 0..100. */
    int octave;             /* -3..3. */
    KBalloon balloon;       /* what changed last, over the tempo in the header, briefly */
    KBalloon hint;          /* the hint on its own (K_BALLOON_HINT): a drum track arrived at, its black keys pick the lane (beside the balloon) */
    int group_held;         /* -1 when none. */
    uint8_t group_combo;    /* the held page key was used with a key of the bed (GLO + black key: mute/solo); its release does nothing */
    uint8_t home_held, home_fired;   /* HOME down (K_ACT_HOME_DOWN), and its hold has been used (1; 2: it opened the settings) */
    int8_t preset_slot[K_TRACKS];   /* the user preset a track's sound came from (PRESETS steps on from it), -1 none */
    int8_t factory[K_TRACKS];       /* or the factory preset of its engine (presets_factory.c), -1 none */
    char preset_name[K_TRACKS][K_NAME_LEN + 1];   /* its name (the header shows it), "" none */
    uint32_t home_since;
    uint8_t settings_open;  /* the settings (HOME held), after Felucca's MENU > SYSTEM: a list of rows, a value each */
    int settings_sel;
    uint8_t set_v[K_SETTINGS];   /* the settings' values (an index into each row's list) */
    uint8_t set_pending;         /* a setting changed: kept (k_settings_save) once the settings close and the transport stops */
    uint32_t set_try_ms;                 /* when the settings were last written (a failed write: tried again 2 s later) */
    uint8_t set_ask;             /* CLEAR ALL turned once: once more clears, anything else lets it go */
    char song_name[K_NAME_LEN + 1];   /* the song's name (saved with it); "" until it is named */
    uint8_t song_from;      /* 1 + the song slot it was loaded from or saved to; 0 none */
    uint8_t save_open;      /* the SAVE overlay (kreisel_save.h): 0 closed, 1 SONG, 2 SOUND */
    uint8_t preset_dirty[K_TRACKS];   /* the preset in hand (factory or user) changed since it was picked: its name shows a '*' */
    uint8_t save_ask;       /* SAVE on a synth page with a user preset in hand: "overwrite?" up; SAVE again writes it, any other control cancels */
    int8_t save_act;        /* the action a knob armed: K_SAVE_* */
    uint8_t save_sel[2];    /* the slot in hand on each side */
    uint8_t name_open;      /* the NAME screen: 0 closed, 1 for a SAVE, 2 a rename */
    uint8_t name_pos;
    char name_buf[K_NAME_LEN + 1];

    /* the sequencer's tools (kreisel_seqtool.c): the track before the last one (UNDO), what COPY took, their random numbers */
    KNote undo_roll[K_ROLL_NOTES];
    uint16_t undo_lanes[K_LANES];
    uint8_t undo_lpat;      /* the pattern undo_lanes are of (and undo_chance) */
    uint32_t undo_chance;   /* that pattern's chances (kind 1) */
    int8_t undo_t;          /* the track undo_* hold, -1 none */
    uint8_t undo_kind;      /* what undo_roll holds: 1 a track's notes and lanes (undo_lanes too); else, in its bytes, 2 the song, 3 a sound,
                             * 4 the mixer, 5 a track's inserts (kreisel_seqtool.c) */
    uint8_t clip_kind;      /* K_CLIP_*: the one clipboard, what was copied last (C3): a pattern's notes (clip, their steps only), a drum
                             * lane's steps (clip_lane), a sound, a track's inserts, a value, SONG's column or row (those in clip's bytes) */
    uint8_t clip_n;
    uint16_t clip_lane;
    KNote clip[K_ROLL_NOTES];
    uint32_t tool_rng;
    /* what is selected for a copy, a paste, a clear (k_edit; C3: here, not in a device's UI) */
    uint8_t sel_kind, sel_idx;        /* K_SEL_*: none (the value in hand), a scene's column, a track's row on SONG; the lane in hand on DRUM */
    uint8_t tools_open;     /* the tools' list over a SEQ page (K_ACT_TOOLS): its row in hand, the one tried on the track (live, -1 none) */
    int8_t tools_sel, tools_try;
    uint8_t tools_stage, tools_which;   /* K_LAY_SHEET_CURSOR: 0 the list, 1 the tool's values (tools_which the one in hand) */
    int16_t tool_x[32], tool_y[32];   /* each tool's values (K_TOOL_COUNT), kept while the device is on */
    uint8_t boom, boom_tool; int8_t boom_track, boom_x, boom_y; uint32_t boom_ms;   /* Dynamite (the clear tools): lit, it
                                     * goes off after K_BOOM_MS (the rings drawn); pressed again before, nothing is cleared */

    uint32_t held_since_ms;
    KClock clk;             /* the external clock follower (clock_src USB or TRS) */
#if K_WITH_CONNECT
    uint8_t learn;          /* MIDI learn (kreisel_io.h): 0 off, 1 waiting for a control on the page, 2 for a MIDI control, 3 bound (the platform
                             * writes learn_row into the port's profile, then k_learn_done) */
    char learn_what[16];    /* the control: a profile row's words ("knob 1") */
    char learn_row[40];     /* the row bound ("cc 74 = knob 1") */
    char learn_say[32];     /* the balloon's text (K_BALLOON_LEARN) */
#endif
} KApp;

void k_init(KApp *app);
/* The sequencer's tools (kreisel_seqtool.c) on the track in hand: a synth track's pattern in hand, a drum track's lane in hand. x, y as
 * each says. They return 1 when they did something. UNDO puts back the track before the last tool (twice: as it was after). */
typedef enum {
    K_TOOL_CLEAR,      /* x 1 on a drum track: every lane */
    K_TOOL_FILL,       /* a note every x steps (1, 2, 4): the note in hand's pitch, velocity and length (at most x) */
    K_TOOL_RANDOM,     /* x 0..100 % of the steps; y 0..12: the pitch wanders so many notes of the scale (SCL) from the note in hand */
    K_TOOL_EUCLID,     /* x onsets spread over 16 steps, turned y steps later */
    K_TOOL_SHIFT,      /* x steps later (negative earlier), round */
    K_TOOL_REVERSE,
    K_TOOL_TRANSPOSE,  /* x semitones (a synth track) */
    K_TOOL_COPY,       /* the pattern's notes, or the lane's steps (to the clipboard: a desktop's copy, k_edit; not in the lists) */
    K_TOOL_PASTE,      /* over the pattern, or the lane */
    K_TOOL_DUP,        /* the pattern onto pattern x (0..3), or the lane's steps onto lane x (0..7) */
    K_TOOL_HUMANIZE,   /* the velocities, up to x either way (a synth track) */
    K_TOOL_UNDO,       /* the last tool undone, whatever page it was on (again: redone) */
    /* SONG: on the scene in hand (its column) */
    K_TOOL_SC_INSERT,  /* an empty scene there, the later ones a scene on (the last falls off) */
    K_TOOL_SC_DUP,     /* the scene onto scene x (0..7) */
    K_TOOL_SC_DELETE,  /* it goes, the later ones a scene back (an empty one at the end) */
    K_TOOL_SC_CLEAR,   /* it emptied; x 1: every scene */
    /* SOUND and EDIT: the sound of the track in hand (a drum track's: the lane in hand's) */
    K_TOOL_SN_INIT,    /* the engine's defaults (a drum lane keeps its piece) */
    K_TOOL_SN_RANDOM,  /* every value x % of the way to a random one (a drum lane keeps its piece) */
    K_TOOL_SN_COPY,
    K_TOOL_SN_PASTE,   /* onto a track (a lane) of the same engine */
    K_TOOL_SN_DUP,     /* the sound onto track x (its engine with it), a drum lane's onto lane x */
    /* MIXER and TRACKS: every track */
    K_TOOL_MX_LEVELS,  /* the levels at 0 dB */
    K_TOOL_MX_PANS,    /* centred */
    K_TOOL_MX_SENDS,   /* B1 and B2 off */
    K_TOOL_MX_UNMUTE,  /* no mute, no solo */
    /* FX: the inserts of the track in hand */
    K_TOOL_FX_SWAP,    /* the two slots change places */
    K_TOOL_FX_INIT,    /* x 0 slot 1, 1 slot 2, 2 both: empty */
    K_TOOL_FX_COPY,
    K_TOOL_FX_PASTE,
    K_TOOL_FX_DUP,     /* the inserts onto track x */
    K_TOOL_VIEW,       /* SEQ: the tracker view on or off (KApp.rows_view; not a change: no undo) */
    K_TOOL_CHANCE,     /* SEQ: x % (25, 50, 75, 100) the chance of the step in hand (y 0) or of every step of the pattern (y 1) */
    K_TOOL_COUNT
} KTool;
int k_seq_tool(KApp *app, KTool tool, int x, int y);
void k_seq_tool_revert(KApp *app);           /* the track the last tool changed, back as it was (undo_* kept: a tool tried, then let go) */
/* the tools' list (tools_open): its rows on the page in hand, a row's tool, and the tools' names and values */
/* a yes or no waits (the panel lights the keys that answer it): K_ASK_OCT OCT+ yes, OCT- no (the tools' list, SAVE armed, NAME);
 * K_ASK_SAVE SAVE yes (overwrite the preset?); 0 none */
enum { K_ASK_NONE, K_ASK_OCT, K_ASK_SAVE };
int k_asks(const KApp *app);
int k_tools_page(const KApp *app);           /* the page in hand has tools (STEP DRUM ROLL SONG SOUND EDIT MIXER TRACKS FX) */
int k_tools_count(const KApp *app);
KTool k_tools_item(const KApp *app, int row);
const char *k_tool_name(KTool tool);
int k_tool_vals(KTool tool, int drum);       /* how many values it takes: 0, 1 (x) or 2 (x, y) */
const char *k_tool_val_name(KTool tool, int which, int drum);
void k_tool_val_text(const KApp *app, KTool tool, int which, char *out, int n);
void k_tools_init(KApp *app);
int k_tool_turn(KApp *app, KTool tool, int which, int d);
int k_tool_range(const KApp *app, KTool tool, int which, int *lo, int *hi);   /* a value's ends; 0 none */
int k_tool_set(KApp *app, KTool tool, int which, int v);   /* set outright within them; 1 moved */
void k_tools_value(KApp *app, int which, int v);
/* SONG: what a scene plays on a track, 0 .. 3 the patterns A .. D, 4 nothing. set: kept in 0..4; step: d one way or the other,
 * wrap: round (after nothing, A). 1 changed */
int k_song_cell_set(KApp *app, int scene, int track, int v);
/* the pattern a track plays and edits, 0 .. 3 (A .. D): the SEQ pages' ALGORITHM. 1 changed */
const uint16_t *k_lanes(const KApp *app, int t, int p);   /* a drum track's lanes in pattern p (the one in hand: lane_on) */
uint16_t *k_lanes_mut(KApp *app, int t, int p);
/* an outside drum's k-th value as lane `lane` of track t plays it (its own when the track keeps the drum's lanes, else the track's) */
int k_lane_val(const KApp *app, int t, int lane, int k);
int k_pattern_set(KApp *app, int track, int p);
int k_song_cell_step(KApp *app, int scene, int track, int d, int wrap);
int k_song_block(KApp *app, int op, int kind, int idx);
/* the selection and the clipboard (C3): one for every device. k_select: SONG's column (K_SEL_SCENE, idx the scene) or row (K_SEL_ROW,
 * idx the track), DRUM's lane in hand (K_SEL_LANE, idx the lane: it is taken in hand); K_SEL_NONE the value in hand. k_edit: op 0
 * copy, 1 paste, 2 clear, on what is selected on the page in hand (else on the value in hand: clear its default); 1 done.
 * k_value_copy / k_value_paste: a value of a page's item (a menu's on another than the one in hand) */
enum { K_SEL_NONE, K_SEL_SCENE, K_SEL_ROW, K_SEL_LANE };
/* what the one clipboard holds (KApp.clip_kind): a paste takes only its own kind */
enum { K_CLIP_NONE, K_CLIP_NOTES, K_CLIP_LANE, K_CLIP_SOUND, K_CLIP_INSERTS, K_CLIP_VALUE, K_CLIP_SCENE, K_CLIP_ROW };
void k_select(KApp *app, int kind, int idx);
int k_selected(const KApp *app, int kind);   /* the index selected of that kind on the page in hand, -1 none */
int k_edit(KApp *app, int op);
int k_value_copy(KApp *app, KPage page, int i);
int k_value_paste(KApp *app, KPage page, int i);   /* SONG: op 0 copy, 1 paste, 2 clear a scene's column (kind 1) or a track's row (2) */   /* the list's row in hand: its value set (a slider) and tried at once */
unsigned k_euclid(int k, int rot);           /* k onsets spread over 16 steps (bit i: step i), turned rot steps later */
extern const uint16_t k_scale_masks[16];     /* SCL's scales: bit i, the note i semitones over the root */
/* External clock (kreisel_clock.c): the platform feeds MIDI realtime bytes; see kreisel_clock.h */

void k_dispatch(KApp *app, KAction action);
#define K_BOOM_MS 900u   /* Dynamite's fuse: the rings drawn, then the clear */
void k_tick(KApp *app, uint32_t time_ms);
void k_open_page(KApp *app, KPage page);
void k_set_battery(KApp *app, int level, int charging);   /* from the platform; level -1 hides it */
void k_arp_note(KApp *app, int track, int note, uint32_t ms);   /* from the platform: the arp just played `note` on `track` */
void k_set_track_peaks(KApp *app, const uint16_t *peaks, uint32_t ms);   /* from the audio: each track's peak since the last call (0..32767) */
void k_set_out_peak(KApp *app, int left, int right, uint32_t ms);   /* from the platform: the output's peaks since the last call, 0..32767
                                                 * (32767 the ceiling); the meter rises at once and falls ~30 dB a second */
void k_set_shown(KApp *app, int n);        /* from the device's profile, after k_init */
/* How a screen joins pages, from the device's profile (after k_init). The pages stay what they are (their values, a song); a wide
 * screen shows two at once. K_LAY_ENV_PAIR: ENV 1 and ENV 2 side by side, as one page (ENV 1's): its items 0 .. 7 ENV 1's, 8 ..
 * 15 ENV 2's; its rows ENV 1's two, then ENV 2's two; ENV 2 opens it. */
#define K_LAY_ENV_PAIR 1u
#define K_LAY_CONNECT 16u  /* the settings' Connect tab shown (every profile but the FM-1's, whose build has no MIDI choice: K_WITH_CONNECT) */
#define K_LAY_ROLL_CELLS 32u   /* the piano roll as cells under a cursor (a step and a pitch), no cards: a device that only moves a cursor (the
                                  * Zero). The FM-1 keeps its column and its knobs */
#define K_LAY_SHEET_CURSOR 256u   /* the action sheets (the tools) as a cursor's: a list of names, OK opens a
                                  * second box of its values (up / down a value, left / right turn it, OK does, BACK returns): the Zero, a pad */
#define K_LAY_SHEET_POINTER 512u  /* as a pointer's: the names on the rail, the row clicked open (a desktop); neither: a knobs device's (the FM-1) */
#define K_LAY_USB_AUDIO 1024u   /* the device sends its sound over USB audio (the FM-1): the settings' USB level row */
#define K_LAY_LEDS 64u   /* the device has LEDs (the FM-1): the settings' LEDs row (not on a screen without them) */
#define K_LAY_PRESET_POP 128u   /* the preset's name as a balloon on every page as PRESETS turns, none in the header (the FM-1: the header had
                                 * room for three letters) */
#define K_LAY_MIX_KEYS 8u  /* a keyboard alone (the Cardputer Zero): the mixer without cards, the arrows its strips and values, 1..4 its control
                            * */
#define K_LAY_SYNTH_ALL 4u /* a large screen with no knobs (the Switch): SOUND shows the whole engine, its EDIT half too, every
                           * control a target (no window of four, no cards) */
#define K_LAY_FX_ALGO 2u   /* the device turns the effect type with an encoder of its own (the FM-1's ALGORITHM): FX's rows are all
                            * parameters, P1 P2 P3 MIX, then P4 .. four a row; else TYPE P1 P2 MIX, then P3 .. */
void k_set_layout(KApp *app, unsigned lay);
int k_fx_item(const KApp *app, int item, int *slot, int *param); /* page index -> stored index; -1 TYPE, -2 MIX, -3 blank */
/* an insert's effect on the track in hand (0 NONE .. k_effect_count()): every way of picking one (ALGORITHM, < >, a list) goes
 * through these; a change marks the preset changed. step: d one way or the other, wrap: round the ends (NONE after the last). 1 changed */
int k_fx_type_set(KApp *app, int slot, int type);
int k_fx_type_step(KApp *app, int slot, int d, int wrap);
int k_page_tabs(const KApp *app, int *at);   /* the turns a page key gives the page in hand (its rows, its group's pages): the tabs beside the preset; 0: none */
int k_page_count(const KApp *app, KPage page);   /* the items a page has on this screen (k_param_count, or a pair's both) */
KPage k_item_page(const KApp *app, KPage page, int *index);   /* the page an item is of, its index there (a pair's ENV 2 items) */
int k_param_count(KPage page);             /* parameters the page has */
const KParamDesc *k_page_desc(KPage page);   /* the four descriptors of a ROLL, LFO, ARP, SCL or CLOCK page, else 0 */
KPage k_seq_page(const KApp *app);        /* the SEQ view of the selected track: DRUM for a drum engine, else STEP */
int k_is_drum(const KApp *app, int track);
int k_track_missing(const KApp *app, int track);
int k_track_newer(const KApp *app, int track);     /* missing because the song names a newer version of an engine that is here */
/* a page the track's engine does without (ORGAN, DRUM: no ENV 1, ENV 2, ENV DEST, LFO): shown as such, its knobs turn nothing */
int k_page_unused(const KApp *app, KPage page);   /* its engine is a plugin that is not here: silent, NO PLUGIN, its knobs still */
/* a drum lane's step (track, lane 0..K_LANES-1, step 0..15): every way of writing one (a key, a tap, a drag across, recording)
 * goes through these. 1 changed */
int k_lane_step_set(KApp *app, int track, int lane, int step, int on);
int k_lane_step_toggle(KApp *app, int track, int lane, int step);   /* (a toggle: that step the one in hand, cur) */
/* a step's chance to sound (KApp.chance): 0 always, 1 75 %, 2 50 %, 3 25 % (k_chance_pct); set: 1 changed */
int k_chance(const KApp *app, int track, int pattern, int step);
int k_chance_set(KApp *app, int track, int pattern, int step, int c);
#define k_chance_pct(c) (100 - 25 * ((c) & 3))
int k_step_on(const KApp *app, int track, int step);   /* the track sounds on that step (a note, or any lane) */
int k_track_audible(const KApp *app, int track);   /* not muted, and (if any track is soloed) soloed */
void k_mute_toggle(KApp *app, int track);          /* M (the mixer's, GLO + a black key): every way of muting goes through it */
void k_solo_toggle(KApp *app, int track);          /* S, the same */
void k_init_released(KApp *app);   /* k_init, the notes still sounding let go (their note-offs queued again after the reset) */
int k_take_note(KApp *app, KNoteEv *out);          /* the next live note (keys, MIDI in) for the sound; 0 when there is none */
void k_bend_in(KApp *app, int v14);
void k_knob_in(KApp *app, int slot, int v127);     /* a controller's knob: the slot-th knob of the page in hand to v (0 .. 127 its range) */
void k_knob_turn(KApp *app, int slot, int delta);  /* a controller's endless encoder: the slot-th knob turned */
void k_level_in(KApp *app, int t, int v127);       /* a controller's fader: track t's level (-1: the track in hand's) */                /* a MIDI pitch bend in (0 .. 16383, 8192 none): the track in hand's */
void k_note_in(KApp *app, int note, int vel);
/* a touch surface's drum pad (draw_surface.c): lane 0..7 of the track in hand struck (vel 1..127) or let go (0), as a key's lane
 * is, without the General MIDI numbers k_note_in maps; armed, it is written at the step */
void k_lane_hit(KApp *app, int lane, int vel);
/* the modulation wheel (a surface's strip, MIDI CC 1): 0..127 onto the track in hand's first macro (HOME's), through its value's
 * own range (the preset decides what the macro moves) */
void k_mod_in(KApp *app, int v127);      /* a MIDI note in (vel 0 = off): sounds on the selected track; armed, it is written like a key */
void k_cc_in(KApp *app, int cc, int value);       /* a MIDI control change: CC 7 level, 10 pan, 16..19 the macros of the track in hand */
/* the piano roll of pattern p of track t: its notes (KApp.roll), steps[t][p] kept with them. add: a note (the same step and pitch
 * there already: that one, its length and velocity set); the slot, -1 when full. at: the note sounding at step s on pitch n (it
 * starts there or reaches over it), -1 none. set: moved and sized (clamped to the bar). clear: the notes starting at step s */
int k_roll_add(KApp *app, int t, int p, int step, int note, int len, int vel);
int k_roll_at(const KApp *app, int t, int p, int step, int note);
void k_roll_set(KApp *app, int t, int slot, int step, int note, int len);
void k_roll_remove(KApp *app, int t, int slot);
void k_roll_clear(KApp *app, int t, int p, int step);
int k_roll_count(const KApp *app, int t, int p, int step);   /* the notes starting at step s of pattern p */
void k_roll_fill(KApp *app, int t, int p, unsigned bits, int note);   /* pattern p as bits says: a note (length 1, velocity 100) a step */
void k_roll_select(KApp *app, int t, int slot);   /* the note in hand (its step the one in hand, its NOTE LEN VEL ROLL's knobs); -1 none */
void k_preview(KApp *app, int note, int on);      /* a note heard on the track in hand, not written (the roll's finger on a note) */
int k_key_pitch(const KApp *app, int id);   /* the note key id plays on a ROLL page (F3 and up from the lowest key, by the octave) */
int k_factory_count(uint32_t engine_uid);              /* the factory presets of an engine (presets_factory.c) */
const char *k_factory_name(uint32_t engine_uid, int n);
void k_track_engine(KApp *app, int t, int engine);     /* track t onto that engine, as ALGORITHM does (PRESETS across engines) */
int k_factory_apply(KApp *app, int t, int n);          /* track t's engine's n-th factory preset onto it; 0 done */
int k_factory_check(char *bad, int len);               /* (tests) the words of the factory presets that set nothing; 0 all right */
int k_key_lit(const KApp *app, int id);     /* key i of the bed is lit: step i of the page in hand (STEP, or the lane in hand on DRUM) */
int k_key_playing(const KApp *app, int id);   /* key i is the step playing on a drum track (its steps are the white keys, on every page) */
int k_page_rowed(KPage page);              /* SELECT steps the page a row of cards at a time (the mixer, the synth, the inserts) */
KVal *k_param(KApp *app, KPage page, int i);          /* where parameter i of a page lives: the mixer's run across the tracks */
int k_param_get(const KApp *app, KPage page, int i);
int k_param_reset(KApp *app, KPage page, int i);
int k_param_set(KApp *app, KPage page, int i, int v);   /* set outright, kept within its range (a paste); 1 done, 0 not a value to set */   /* the value back at its default (option-click); 1 done, 0 nothing there to reset */
int k_synth_all_sub(const KApp *app, KPage page, int i, KPage *page_out, int *item_out);   /* the Switch's SOUND: an ENV / LFO item */
/* The values' table (src/kreisel_param.c): every value a track, the song or the device keeps has an id, and one entry says where
 * it lives, its range, its default (what a reset gives), how it is shown (KF_*) and where a turn stops on the way across (a
 * bipolar's 0, a pan's centre, a fader's 0 dB). An engine's values and an effect's take their range and format from its
 * descriptor (KEngine.edit), so the entry says only where. A page's item (page, i) is a lookup into it: k_param_ref. */
typedef enum {
    K_P_RAW,                                   /* a slot past what a page has (params[t][page][i]): 0..100, nothing else */
    K_P_ENG,                                   /* + k: the k-th parameter of the track's engine (a drum's: its lane in hand's) */
    K_P_ENV1 = K_P_ENG + K_ENGINE_PARAMS,      /* + 0..3: ENV 1's ATK DEC SUS REL (PHASE: its DCW) */
    K_P_ENV2 = K_P_ENV1 + 4,                   /* + 0..3: ENV 2's DLY HOLD CRV AMT */
    K_P_PH_DCO = K_P_ENV2 + 4,                 /* + 0..3: PHASE's DCO, A D S R (kept in ENV 2's items 4 .. 7) */
    K_P_PH_DCA = K_P_PH_DCO + 4,               /* + 0..3: PHASE's DCA, A D S R (kept where ENV 2's own are) */
    K_P_DEST = K_P_PH_DCA + 4,                 /* + 0..3: where ENV 1 goes (PIT FLT SHP) and ENV 2 (AMP): 50 none */
    K_P_KIT_ENV = K_P_DEST + 4,                /* + 0..3: a drum lane's ATK PDEP PTIM CLIK, as ENV 1 shows them */
    K_P_LFO = K_P_KIT_ENV + 4,                 /* + 0..8: the LFO (k_page_desc) */
    K_P_ARP = K_P_LFO + 9,                     /* + 0..3 */
    K_P_SCL = K_P_ARP + 4,                     /* + 0..3 */
    K_P_BPM = K_P_SCL + 4, K_P_SWING, K_P_CLOCK_SRC,   /* the song's (KApp.bpm, swing, clock_src) */
    K_P_CHANCE,                                /* STEP's and DRUM's fourth card: the step in hand's chance (KApp.chance, 2026-11-12) */
    K_P_ROLL_STEP, K_P_ROLL_NOTE, K_P_ROLL_LEN, K_P_ROLL_VEL,   /* ROLL: the step in hand, the next note's */
    K_P_LEVEL, K_P_PAN, K_P_SEND1, K_P_SEND2,  /* a track's strip on the mixer */
    K_P_BUS1, K_P_BUS2 = K_P_BUS1 + 4,         /* + 0..3: a bus (the song's, kept with track 1) */
    K_P_FX = K_P_BUS2 + 4,                     /* + 0..23: an insert's value, by where it is stored (k_fx_item) */
    K_P_COLOR = K_P_FX + K_FX_ITEMS,           /* a track's colour */
    K_P_COUNT
} KParamId;
/* an item looked up: the value's id, the track it is of, where (a slot of K_P_RAW, an engine's k), and how the item shows it */
typedef struct { uint8_t id, track, page, idx, view; } KParamRef;
#define K_V_DEAD 1u     /* nothing there (an insert's empty row): not set, reset, turned or shown */
#define K_V_NOTURN 2u   /* not turned (SOUND past its half, where EDIT has the rest) */
#define K_V_NOSHOW 4u   /* its value not shown (the same) */
#define K_V_PLAIN 8u    /* shown as 0..100 with no centre (ENV 2's blank rows past LVL) */
#define K_V_C50 16u     /* a turn stops at 50 (PHASE's DCW and DCA where ENV's destinations are on other engines) */
#define K_NO_CENTER (-999)
/* a value's properties as they are now: d its range, format and names (an engine's, an effect's own), lo .. hi what it is kept
 * in, reset what a reset gives, center where a turn stops (K_NO_CENTER none) */
#define K_PF_PRESET 1u       /* part of a sound (a preset): a turn marks the preset changed */
#define K_PF_DIRTY_SET 2u    /* set outright, it marks the preset changed */
#define K_PF_DIRTY_RESET 4u  /* reset, the same */
#define K_PF_NOSET 8u        /* not set outright (an insert's effect, ROLL's step) */
#define K_PF_NORESET 16u     /* not reset (an insert's effect and mix, a colour, ROLL's step) */
#define K_PF_COARSE 32u      /* turned as it comes, never a step at a time (an effect's values) */
typedef struct { KParamDesc d; int16_t lo, hi, reset, center; uint8_t flags; } KParamProps;
KParamRef k_param_ref(const KApp *app, KPage page, int i);
KVal *k_param_at(KApp *app, KParamRef r);
int k_param_props(const KApp *app, KParamRef r, KParamProps *out);   /* 0: nothing to set, turn or show (no plugin, past the engine's) */
int k_ref_set(KApp *app, KParamRef r, int v);      /* k_param_set's */
int k_ref_reset(KApp *app, KParamRef r);           /* k_param_reset's */
void k_ref_turn(KApp *app, KParamRef r, int delta);   /* a turn (add_param_raw): within its range, a step a detent where few values */
int k_ref_center(const KApp *app, KParamRef r);
int k_params_visible(const KApp *app);     /* min(shown, the page's) */
void k_set_bounds(KApp *app, KNodeKind kind, int index, KRect bounds);
int k_hit_focus(const KApp *app, int x, int y);
KRect k_focus_rect(const KApp *app);
const char *k_page_name(KPage page);
const char *k_group_name(KPageGroup group);
int k_menu_count(const KApp *app);
int k_page_folded(const KApp *app, KPage page);   /* folded into another on this screen (a big one: EDIT, ENV, LFO into SOUND) */
/* the settings (HOME held, after Felucca's MENU): rows in tabs, a value each (an action: CLEAR ALL, asked twice) */
#define K_SETTING_TABS (3 + K_WITH_CONNECT)
const char *k_setting_name(int i);
const char *k_setting_value(const KApp *app, int i);
int k_setting_tab(int i);                  /* the tab a row is in */
int k_setting_tabs(const KApp *app);       /* the tabs shown (Connect: K_LAY_CONNECT) */
int k_setting_visible(const KApp *app, int i);   /* row i shown on this device (LEDs: K_LAY_LEDS; Connect's: K_LAY_CONNECT) */
int k_setting_first(const KApp *app);      /* the first row shown */
#ifndef K_PAGE_BALLOON
#define K_PAGE_BALLOON 0   /* a new page naming itself in a balloon at the top: hidden (not wanted; the code kept, 1 shows it) */
#endif
const char *k_setting_tab_name(int tab);
KPage k_menu_page(const KApp *app, int option);

#endif
