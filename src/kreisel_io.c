/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The input layer (kreisel_io.h). */
#include "kreisel_io.h"
#include "kreisel_clock.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* the FM-1's MIDI, as fm1_main.c's midi_service did it: notes on the track in hand, the clock, CC 100 a page, CC 102 play */
const KIOMap k_io_map_fm1[] = {
    {KIO_NOTE, 0, -1, KIO_DO_NOTE, 0, 0, 0},
    {KIO_REALTIME, 0, -1, KIO_DO_CLOCK, 0, 0, 0},
    {KIO_CC, 0, 100, KIO_DO_PAGE, 0, 0, 0},
    {KIO_CC, 0, 102, KIO_DO_ACTION, 0, K_ACT_PLAY, 0},
};
const int k_io_map_fm1_n = (int)(sizeof k_io_map_fm1 / sizeof k_io_map_fm1[0]);

/* a computer's MIDI (the SDL builds: CoreMIDI, ALSA, RTP-MIDI): notes on the track in hand, the clock, every CC to the core's (7 level,
 * 10 pan, 16 .. 19 the macros) */
const KIOMap k_io_map_desk[] = {
    {KIO_NOTE, 0, -1, KIO_DO_NOTE, 0, 0, 0},
    {KIO_REALTIME, 0, -1, KIO_DO_CLOCK, 0, 0, 0},
    {KIO_CC, 0, -1, KIO_DO_CC, 0, 0, 0},
};
const int k_io_map_desk_n = (int)(sizeof k_io_map_desk / sizeof k_io_map_desk[0]);

/* the Cardputer Zero's keys: its orange Fn keys work without Fn, the app types nothing. F Z X C the cursor (up,
 * left, down, right), K L the value up and down (its PgUp PgDn), held: repeating; 1 .. 0 the pages (its F1 .. F10): 1 HOME, 2 SEQ,
 * 3 GLO, 4 SYNTH, 5 EDIT, 6 ENV, 7 LFO, 8 FX, 9 ARP, 0 every page; , . ALGORITHM; r REC, s SAVE, g settings, u undo, [ ] octave,
 * o p presets. With Fn held the device sends the arrows, PgUp, the F-keys: the same */
#define KEY2(c, k, act, a, b, rep) {KIO_KEY, 0, c, act, rep, a, b}, {KIO_KEY, 0, k, act, rep, a, b}
const KIOMap k_io_map_zero[] = {
    KEY2('f', KIO_K_UP, KIO_DO_ACTION, K_ACT_UP, 0, 1),
    KEY2('x', KIO_K_DOWN, KIO_DO_ACTION, K_ACT_DOWN, 0, 1),
    KEY2('z', KIO_K_LEFT, KIO_DO_ACTION, K_ACT_LEFT, 0, 1),
    KEY2('c', KIO_K_RIGHT, KIO_DO_ACTION, K_ACT_RIGHT, 0, 1),
    KEY2('k', KIO_K_PGUP, KIO_DO_ACTION, K_ACT_VALUE_DELTA, 1, 1),
    KEY2('l', KIO_K_PGDN, KIO_DO_ACTION, K_ACT_VALUE_DELTA, -1, 1),
    KEY2('1', KIO_K_F1 + 0, KIO_DO_ACTION, K_ACT_BACK, 0, 0),   /* HOME: the mixer, again its macros */
    KEY2('2', KIO_K_F1 + 1, KIO_DO_GROUP, K_GROUP_PATTERN, 0, 0),
    KEY2('3', KIO_K_F1 + 2, KIO_DO_GROUP, K_GROUP_GLO, 0, 0),
    KEY2('4', KIO_K_F1 + 3, KIO_DO_GROUP, K_GROUP_SOUND, 0, 0),
    KEY2('5', KIO_K_F1 + 4, KIO_DO_GROUP, K_GROUP_EDIT, 0, 0),
    KEY2('6', KIO_K_F1 + 5, KIO_DO_GROUP, K_GROUP_ENV, 0, 0),
    KEY2('7', KIO_K_F1 + 6, KIO_DO_GROUP, K_GROUP_LFO, 0, 0),
    KEY2('8', KIO_K_F1 + 7, KIO_DO_GROUP, K_GROUP_FX, 0, 0),
    KEY2('9', KIO_K_F1 + 8, KIO_DO_GROUP, K_GROUP_ARP, 0, 0),
    KEY2('0', KIO_K_F1 + 9, KIO_DO_ACTION, K_ACT_PAGE_MENU, 0, 0),   /* every page */
    {KIO_KEY, 0, 'r', KIO_DO_ACTION, 0, K_ACT_REC, 0},           /* r REC, s SAVE, g the settings, u undo, [ ] the octave, o p the presets */
    {KIO_KEY, 0, 's', KIO_DO_ACTION, 0, K_ACT_SAVE, 0},          /* (the interface audit's A-2, 2026-11-12: the keyboard alone had no way */
    {KIO_KEY, 0, 'g', KIO_DO_ACTION, 0, K_ACT_SETTINGS, 0},      /* to them) */
    {KIO_KEY, 0, 'u', KIO_DO_ACTION, 0, K_ACT_UNDO, 0},
    {KIO_KEY, 0, '[', KIO_DO_ACTION, 0, K_ACT_OCTAVE_DELTA, -1},
    {KIO_KEY, 0, ']', KIO_DO_ACTION, 0, K_ACT_OCTAVE_DELTA, 1},
    {KIO_KEY, 0, 'o', KIO_DO_ACTION, 0, K_ACT_SOUND_DELTA, -1},
    {KIO_KEY, 0, 'p', KIO_DO_ACTION, 0, K_ACT_SOUND_DELTA, 1},
    {KIO_KEY, 0, ',', KIO_DO_ACTION, 0, K_ACT_TYPE_DELTA, -1},   /* , . ALGORITHM (2026-11-12): the pattern on SEQ, the engine on its pages, */
    {KIO_KEY, 0, '.', KIO_DO_ACTION, 0, K_ACT_TYPE_DELTA, 1},    /* the effect on FX (the keyboard alone had no way to them) */
    {KIO_KEY, 0, '\r', KIO_DO_HOLD, 0, K_ACT_GRAB, 0},   /* OK: pressed OK; held, a grip (the roll as cells: the arrows edit the note) */
};
const int k_io_map_zero_n = (int)(sizeof k_io_map_zero / sizeof k_io_map_zero[0]);

static const KIOMap *midi_map = k_io_map_fm1, *key_map;
static int midi_n = (int)(sizeof k_io_map_fm1 / sizeof k_io_map_fm1[0]), key_n;
static struct { const KIOMap *map; int n; } dev_map[KIO_DEVS];
void k_io_dev_map(int dev, const KIOMap *map, int n) { if (dev >= 1 && dev <= KIO_DEVS) { dev_map[dev - 1].map = map; dev_map[dev - 1].n = map ? n : 0; } }
void k_io_midi_map(const KIOMap *map, int n) { midi_map = map; midi_n = map ? n : 0; }
void k_io_key_map(const KIOMap *map, int n) { key_map = map; key_n = map ? n : 0; }

static void act(KApp *a, const KIOMap *m, int value, uint32_t ms)
{
    switch (m->act) {
    case KIO_DO_ACTION: k_dispatch(a, (KAction){(KActionType)m->a, m->b, 0, ms}); break;
    case KIO_DO_PAGE: if (value >= 0 && value < K_PAGE_COUNT) k_open_page(a, (KPage)value); break;
    case KIO_DO_KNOB: k_knob_in(a, m->a, value); break;
    case KIO_DO_REL: { const int d = value >= 1 && value <= 63 ? value : value >= 65 ? value - 128 : 0; k_knob_turn(a, m->a, d); (void)ms; } break;
    case KIO_DO_LEVEL: k_level_in(a, m->a, value); break;
    case KIO_DO_GROUP:   /* a press of a page button: its page, again the next of its family */
        k_dispatch(a, (KAction){K_ACT_PAGE_GROUP_DOWN, m->a, 0, ms});
        k_dispatch(a, (KAction){K_ACT_PAGE_GROUP_UP, m->a, 0, ms});
        break;
    default: break;
    }
}
static const KIOMap *find(const KIOMap *map, int n, int in, int ch, int num)
{
    int i;
    for (i = 0; i < n; i++)
        if (map[i].in == in && (!map[i].ch || map[i].ch == ch) && (map[i].num < 0 || map[i].num == num)) return &map[i];
    return 0;
}

/* a message whole (status and its data): through the table */
static int cur_dev;   /* (the device the message in hand came from: 0 none) */
static const KIOMap *find_in(int in, int ch, int num)   /* the device's table first, then the platform's */
{
    const KIOMap *m = cur_dev ? find(dev_map[cur_dev - 1].map, dev_map[cur_dev - 1].n, in, ch, num) : 0;
    return m ? m : find(midi_map, midi_n, in, ch, num);
}
static void midi_message(KApp *a, int port, uint8_t st, uint8_t d1, uint8_t d2, uint32_t ms)
{
    const int kind = st & 0xF0, ch = (st & 0x0F) + 1;
    const KIOMap *m;
#if K_WITH_CONNECT
    if (a->set_v[K_SET_MIDI_CH] && a->set_v[K_SET_MIDI_CH] != ch) return;   /* (the Connect tab's channel: the others not heard) */
    if (a->learn == 2 && k_learn_midi(a, kind, ch, d1, d2, ms)) return;   /* MIDI learn: this control is bound, not played */
#endif
    if (kind == 0x90 || kind == 0x80) {   /* (a note on of velocity 0 is an off) */
        if ((m = find_in(KIO_NOTE, ch, d1)) != 0 && m->act == KIO_DO_NOTE) k_note_in(a, d1, kind == 0x80 ? 0 : d2);
        else if (m && kind == 0x90 && d2) act(a, m, 127, ms);   /* (a pad's note as a button: on its press) */
    } else if (kind == 0xE0) {   /* pitch bend: the track in hand (+-2 semitones) */
        k_bend_in(a, d1 | d2 << 7);
    } else if (kind == 0xB0) {
        if ((m = find_in(KIO_CC, ch, d1)) != 0) {
            if (m->act == KIO_DO_CC) k_cc_in(a, m->a ? m->a : d1, d2);   /* (a profile's pan, macro: the core's CC it stands for) */
            else if (!m->repeat || d2 >= 64) act(a, m, d2, ms);   /* (a button's release: nothing) */
        }
    }
    (void)port;
}

/* each port's place in its stream: the running status, the data bytes so far, inside SysEx */
#define KIO_PORTS 4
static struct { uint8_t st, d[2], nd, sysex; } in[KIO_PORTS];
static int data_len(uint8_t st) { const int k = st & 0xF0; return k == 0xC0 || k == 0xD0 ? 1 : 2; }

void k_io_midi_dev(KApp *a, int port, int dev, const uint8_t *b, int n, uint32_t ms)
{
    cur_dev = dev >= 1 && dev <= KIO_DEVS && dev_map[dev - 1].map ? dev : 0;
    k_io_midi(a, port, b, n, ms);
    cur_dev = 0;
}
void k_io_midi(KApp *a, int port, const uint8_t *b, int n, uint32_t ms)
{
    const int p = port > 0 && port <= KIO_PORTS ? port - 1 : 0;
    int i;
    for (i = 0; i < n; i++) {
        const uint8_t c = b[i];
        if (c >= 0xF8) {   /* realtime: anywhere, even inside another message */
            const KIOMap *m = find_in(KIO_REALTIME, 0, c);
            if (m && m->act == KIO_DO_CLOCK) (void)k_clock_in(a, port, c, ms);
            else if (m) act(a, m, c, ms);
            continue;
        }
        if (c == 0xF0) { in[p].sysex = 1; in[p].st = 0; continue; }
        if (c >= 0x80) {
            in[p].sysex = 0;
            if (c >= 0xF0) { in[p].st = 0; continue; }   /* (system common: not read; it ends the running status) */
            in[p].st = c; in[p].nd = 0;
            continue;
        }
        if (in[p].sysex || !in[p].st) continue;   /* (data with no status, or SysEx's: skipped) */
        in[p].d[in[p].nd++] = c;
        if (in[p].nd >= data_len(in[p].st)) {
            midi_message(a, port, in[p].st, in[p].d[0], in[p].nd > 1 ? in[p].d[1] : 0, ms);
            in[p].nd = 0;   /* (the status runs on) */
        }
    }
}

static KIOSend sender;
void k_io_send_to(KIOSend send) { sender = send; }
void k_io_note_out(int track, int note, int vel)
{
    uint8_t m[3];
    if (!sender || track < 0 || track > 15 || note < 0 || note > 127) return;
    m[0] = (uint8_t)((vel > 0 ? 0x90 : 0x80) | track); m[1] = (uint8_t)note; m[2] = (uint8_t)(vel > 0 ? (vel > 127 ? 127 : vel) : 0);
    sender(m, 3);
}

void k_io_send_audio(KAudio *au)
{
    uint8_t m[3];
    int n;
    au->mo_on = sender != 0;
    while ((n = k_audio_midi_pop(au, m)) > 0) if (sender) sender(m, n);
}

static char in_name[32];
void k_io_set_in_name(const char *name)
{
    size_t i = 0;
    for (; name && name[i] && i < sizeof in_name - 1; i++) in_name[i] = name[i];
    in_name[i] = 0;
}
const char *k_io_in_name(void) { return in_name; }

int k_io_key(KApp *a, int code, int down, int repeat, uint32_t ms)
{
    const KIOMap *m;
    if (!key_map || (m = find(key_map, key_n, KIO_KEY, 0, code)) == 0) return 0;
    if (m->act == KIO_DO_HOLD) { if (!repeat) k_dispatch(a, (KAction){(KActionType)m->a, down ? 1 : 0, 0, ms}); return 1; }   /* (told its press and release) */
    if (!down || (repeat && !m->repeat)) return 1;   /* (the key is the table's: its release, a repeat it does not want, nothing) */
    act(a, m, 0, ms);
    return 1;
}

#if K_WITH_CONNECT
/* ---- MIDI learn (kreisel_io.h): the control pointed at, then the MIDI control moved: a profile's row ---- */
static void learn_say(KApp *a, const char *s, uint32_t ms)
{
    snprintf(a->learn_say, sizeof a->learn_say, "%s", s);
    k_balloon_show(&a->balloon, K_BALLOON_LEARN, ms);
}
static void learn_aim(KApp *a, const char *what, uint32_t ms)   /* a control pointed at: wait for MIDI */
{
    char s[sizeof a->learn_say];
    snprintf(a->learn_what, sizeof a->learn_what, "%s", what);
    snprintf(s, sizeof s, "MIDI -> %c%s", what[0] >= 'a' && what[0] <= 'z' ? what[0] - 32 : what[0], what + 1);
    a->learn = 2;
    learn_say(a, s, ms);
}
void k_learn_start(KApp *a)
{
    a->settings_open = 0; a->menu_open = 0; a->learn_what[0] = 0; a->learn_row[0] = 0;
    a->learn = 1;
    learn_say(a, "Learn: a control", a->clock_ms);
}
void k_learn_cancel(KApp *a)
{
    if (!a->learn) return;
    a->learn = 0;
    learn_say(a, "Learn off", a->clock_ms);
}
static int param_what(const KApp *a, int i, char *w, int n)   /* the page's parameter i as a row's words; 0 none */
{
    const int slot = i - a->win;
    if (a->page == K_PAGE_MIXER && i == 0) return snprintf(w, (size_t)n, "level %d", a->track + 1), 1;
    if (a->page == K_PAGE_MIXER && i == 1) return snprintf(w, (size_t)n, "pan"), 1;
    if (a->page == K_PAGE_HOME && i >= 0 && i < 4) return snprintf(w, (size_t)n, "macro %d", i + 1), 1;
    if (slot < 0 || slot >= 8) return 0;
    snprintf(w, (size_t)n, "knob %d", slot + 1);
    return 1;
}
static int node_what(const KApp *a, int f, char *w, int n)   /* a focus node: a card's knob, a track's tile (its level) */
{
    if (f < 0 || f >= a->focus_count) return 0;
    if (a->focus[f].kind == K_NODE_PARAM) return param_what(a, a->focus[f].index, w, n);
    if (a->focus[f].kind == K_NODE_TRACK && K_IS_HOME(a->page) && a->focus[f].index >= 0 && a->focus[f].index < K_TRACKS)
        return snprintf(w, (size_t)n, "level %d", a->focus[f].index + 1), 1;
    return 0;
}
int k_learn_action(KApp *a, KAction e)
{
    static const char *const PG[K_GROUP_ALL] = {"page seq", "page glo", "page synth", "page env", "page fx", "page edit", "page lfo", "page arp"};
    char w[16];
    const char *b = 0;
    w[0] = 0;
    switch (e.type) {
    case K_ACT_BACK: case K_ACT_HOME_UP: k_learn_cancel(a); return 1;   /* Esc, HOME: no learning */
    case K_ACT_HOME_DOWN: return 1;
    case K_ACT_PARAM_DELTA: { int i = a->win + e.a; if (!param_what(a, i, w, sizeof w)) return 0; } break;
    case K_ACT_VALUE_DELTA: if (!node_what(a, a->focused, w, sizeof w)) return 0; break;
    case K_ACT_POINTER: { const int f = e.a >= 0 ? k_hit_focus(a, e.a, e.b) : -1; if (!node_what(a, f, w, sizeof w)) return 0; a->focused = f; } break;
    case K_ACT_PLAY: b = "play"; break;
    case K_ACT_REC: b = "rec"; break;
    case K_ACT_UNDO: b = "undo"; break;
    case K_ACT_TOOLS: b = "tools"; break;
    case K_ACT_SAVE: b = "save"; break;
    case K_ACT_SETTINGS: b = "settings"; break;
    case K_ACT_PAGE_MENU: b = "menu"; break;
    case K_ACT_TRACK_DELTA: if (!e.a) return 1; b = e.a > 0 ? "track+" : "track-"; break;
    case K_ACT_SELECT_DELTA: if (!e.a) return 1; b = e.a > 0 ? "select+" : "select-"; break;
    case K_ACT_TYPE_DELTA: if (!e.a) return 1; b = e.a > 0 ? "pattern+" : "pattern-"; break;
    case K_ACT_OCTAVE_DELTA: if (!e.a) return 1; b = e.a > 0 ? "octave+" : "octave-"; break;
    case K_ACT_PAGE_GROUP_DOWN: if (e.a < 0 || e.a >= K_GROUP_ALL) return 1; b = PG[e.a]; break;
    case K_ACT_PAGE_GROUP_UP: return 1;
    default: return 0;   /* (the cursor, OK, the keys: as ever, to reach a control) */
    }
    learn_aim(a, b ? b : w, e.time_ms);
    return 1;
}
int k_learn_midi(KApp *a, int kind, int ch, int num, int val, uint32_t ms)
{
    const char *w = a->learn_what;
    const int value = !strncmp(w, "knob", 4) || !strncmp(w, "level", 5) || !strncmp(w, "pan", 3) || !strncmp(w, "macro", 5);
    char c[12], s[sizeof a->learn_say];
    if (a->learn != 2) return 0;
    if (kind != 0xB0 && !(kind == 0x90 && val && !value)) return 0;   /* a CC; a note (on) only for a button */
    if (ch != 1) snprintf(c, sizeof c, " ch %d", ch); else c[0] = 0;
    snprintf(a->learn_row, sizeof a->learn_row, "%s %d%s = %s", kind == 0xB0 ? "cc" : "note", num, c, w);
    snprintf(s, sizeof s, "%s %d -> %c%s", kind == 0xB0 ? "CC" : "Note", num, w[0] >= 'a' && w[0] <= 'z' ? w[0] - 32 : w[0], w + 1);
    a->learn = 3;
    learn_say(a, s, ms);
    return 1;
}
const char *k_learn_row(const KApp *a) { return a->learn == 3 ? a->learn_row : 0; }
void k_learn_done(KApp *a, int ok)
{
    if (a->learn != 3) return;
    a->learn = 0;
    if (!ok) learn_say(a, "Not saved", a->clock_ms);
}
#endif
