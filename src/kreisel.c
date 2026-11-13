/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel_mul.h"
#include "kreisel.h"
#include "kreisel_save.h"
#include "kreisel_engine.h"
#include "kreisel_level.h"
#include "kreisel_clock.h"
#include "kreisel_plugin.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const KPage groups[K_GROUP_COUNT][K_PAGE_COUNT] = {
    { K_PAGE_ROLL, K_PAGE_STEP },   /* SEQ: the piano roll (DRUM for a drum track: group_page), then the step view (not for a drum track) */
    { K_PAGE_SONG, K_PAGE_BUS1, K_PAGE_BUS2 },   /* GLO: the song's own, SONG BUS 1 BUS 2 (the mixer is HOME's now) */
    { K_PAGE_SOUND, K_PAGE_SOUND, K_PAGE_SOUND },
    { K_PAGE_ENV1, K_PAGE_ENV2 },   /* ENV: each envelope with where it goes (ENV DEST's values, on the two pages) */
    { K_PAGE_FX },
    { K_PAGE_EDIT },   /* EDIT: the engine's second half */
    { K_PAGE_LFO },
    { K_PAGE_ARP, K_PAGE_SCL },
    { K_PAGE_HOME, K_PAGE_ROLL, K_PAGE_STEP, K_PAGE_SONG, K_PAGE_SOUND, K_PAGE_ENV1, K_PAGE_ENV2, K_PAGE_FX, K_PAGE_MIXER, K_PAGE_BUS1, K_PAGE_BUS2, K_PAGE_LFO, K_PAGE_ARP, K_PAGE_SCL, K_PAGE_TRACKS, K_PAGE_EDIT }
};
static const int group_sizes[K_GROUP_COUNT] = {2, 3, 1, 2, 1, 1, 1, 2, 16};
static const char *page_names[K_PAGE_COUNT] = {
    "HOME", "STEP", "DRUM", "ROLL", "SONG", "SYNTH", "ENV 1", "ENV 2", "ENV DEST", "FX", "MIXER", "BUS 1", "BUS 2", "LFO", "ARP", "SCL", "CLOCK", "TRACKS", "EDIT"
};
static const char *group_names[K_GROUP_COUNT] = {"SEQ", "GLO", "SYNTH", "ENV", "FX", "EDIT", "LFO", "ARP", "PAGES"};

void k_balloon_show(KBalloon *b,KBalloonKind kind,uint32_t now) {
    int showing=b->kind==(int)kind && (int32_t)(now-b->held_ms)>=0 && (now-b->held_ms)<K_BALLOON_HOLD_MS+K_BALLOON_OUT_MS;
    if(!showing)b->in_ms=now;
    b->kind=(int)kind;b->held_ms=now;b->dir=0;   /* (from the top: only PRESETS' turn sets a side, after this) */
}
int k_balloon_alpha(const KBalloon *b,uint32_t now) {
    int32_t held=(int32_t)(now-b->held_ms),shown=(int32_t)(now-b->in_ms);   /* (a frame a little before the action's own clock: not negative) */
    if(!b->kind)return 0;
    if(now-b->held_ms>0x40000000u && b->held_ms-now>1000u)return 0;   /* (long gone: past 2^31 ms the difference would read as "a moment
                                                * ago" again; a frame a moment before the action's clock is not that) */
    if(held<0)held=0;
    if(shown<0)shown=0;
    if(held>=(int32_t)(K_BALLOON_HOLD_MS+K_BALLOON_OUT_MS))return 0;
    if(held>=(int32_t)K_BALLOON_HOLD_MS)return (int)(255-(held-(int32_t)K_BALLOON_HOLD_MS)*255/(int32_t)K_BALLOON_OUT_MS);
    (void)shown;   /* (it shows at once: no dissolve in, so nothing to blink while a knob turns) */
    return 255;
}
int k_balloon_active(const KBalloon *b,uint32_t now) { return b->kind && (now-b->held_ms<K_BALLOON_HOLD_MS+K_BALLOON_OUT_MS+50u || b->held_ms-now<1000u); }
                                                /* (unsigned: one shown 24 days ago is not showing; a frame a moment before the action's clock is) */
static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static int wrap(int n, int size) { n %= size; return n < 0 ? n + size : n; }
const char *k_page_name(KPage page) { return (unsigned)page < K_PAGE_COUNT ? page_names[page] : "?"; }
const char *k_group_name(KPageGroup group) { return (unsigned)group < K_GROUP_COUNT ? group_names[group] : "?"; }
static int k_is_drum_sel(const KApp *a);
/* a page folded into another on this screen (K_LAY_SYNTH_ALL: SOUND shows the engine whole, its EDIT half, ENV and LFO): not a page
 * of its own there, so not in the list of all pages, and opening it opens SOUND */
int k_page_folded(const KApp *a, KPage p) {
    return (a->lay & K_LAY_SYNTH_ALL) && (p == K_PAGE_EDIT || p == K_PAGE_ENV1 || p == K_PAGE_ENV2 || p == K_PAGE_LFO);
}
static int all_count(const KApp *a);
int k_menu_count(const KApp *a) {
    if (a->menu_group == K_GROUP_PATTERN) return k_is_drum_sel(a) ? 1 : 2;   /* a drum track has no piano roll */
    if (a->menu_group == K_GROUP_ALL) return all_count(a);
    return group_sizes[a->menu_group];
}
int k_is_drum(const KApp *a, int t) { return k_engine_at(a->sound[t])->drum; }
static int k_is_drum_sel(const KApp *a) { return k_is_drum(a, a->track); }
KPage k_seq_page(const KApp *a) { return k_is_drum(a, a->track) ? K_PAGE_DRUM : K_PAGE_ROLL; }   /* a synth track: the piano roll; a drum track: its lanes */
/* the first page of SEQ (the second of the list of all pages) is the view of the track in hand: the piano roll, or DRUM for a drum engine; the
 * step view (STEP) is the second page of SEQ for a synth track */
static KPage group_page_raw(const KApp *a, int g, int i) { return (g == K_GROUP_PATTERN && i == 0) || (g == K_GROUP_ALL && i == 1) ? k_seq_page(a) : (g == K_GROUP_ALL && i == 2) ? K_PAGE_STEP : groups[g][i]; }
static int all_count(const KApp *a) { int i, n = 0; for (i = 0; i < group_sizes[K_GROUP_ALL]; i++) n += !k_page_folded(a, group_page_raw(a, K_GROUP_ALL, i)); return n; }
static KPage group_page(const KApp *a, int g, int i) {
    int k, n = 0;
    if (g != K_GROUP_ALL) return group_page_raw(a, g, i);
    for (k = 0; k < group_sizes[g]; k++) {   /* (the list of all pages: the folded ones left out) */
        const KPage p = group_page_raw(a, g, k);
        if (k_page_folded(a, p)) continue;
        if (n++ == i) return p;
    }
    return K_PAGE_HOME;
}
static int group_has(const KApp *a, int g, KPage p) { int i; for (i = 0; i < group_sizes[g]; i++) if (group_page(a, g, i) == p) return 1; return 0; }
KPage k_menu_page(const KApp *a, int i) { return group_page(a, a->menu_group, clamp(i, 0, k_menu_count(a)-1)); }
/* ---- the piano roll's notes (KApp.roll): a pool for the track's four patterns; steps[t][p] follows them ---- */
static void roll_bits(KApp *a, int t, int p) {
    int i; uint16_t b = 0;
    for (i = 0; i < K_ROLL_NOTES; i++) { const KNote *n = &a->roll[t][i]; if (n->len && (n->at >> 4) == p) b |= (uint16_t)(1u << (n->at & 15)); }
    a->steps[t][p] = b;
}
int k_roll_count(const KApp *a, int t, int p, int step) {
    int i, c = 0;
    if (t < 0 || t >= K_TRACKS) return 0;
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && a->roll[t][i].at == (uint8_t)(p << 4 | (step & 15))) c++;
    return c;
}
/* the steps a note from `step` may last: to the pattern's end, or to the next note on its row (lengthened, it stops at
 * the note it meets); in the tracker view, the next note of the track at any pitch (its column's next entry). self: the note itself */
static int len_room(const KApp *a, int t, int p, int step, int note, int self) {
    int i, room = K_STEPS - step;
    for (i = 0; i < K_ROLL_NOTES; i++) {
        const KNote *n = &a->roll[t][i];
        const int s = n->at & 15;
        if (i == self || !n->len || (n->at >> 4) != p || s <= step || (n->note != note && !a->rows_view)) continue;
        if (s - step < room) room = s - step;
    }
    return room;
}
int k_roll_add(KApp *a, int t, int p, int step, int note, int len, int vel) {
    int i, free_ = -1;
    if (t < 0 || t >= K_TRACKS || p < 0 || p > 3 || step < 0 || step >= K_STEPS || note < 0 || note > 127) return -1;
    len = clamp(len, 1, len_room(a, t, p, step, note, -1)); vel = clamp(vel, 1, 127);
    for (i = 0; i < K_ROLL_NOTES; i++) {
        KNote *n = &a->roll[t][i];
        if (n->len && n->at == (uint8_t)(p << 4 | step) && n->note == note) { n->len = (uint8_t)len; n->vel = (uint8_t)vel; return i; }
        if (!n->len && free_ < 0) free_ = i;
    }
    if (free_ < 0) return -1;
    a->roll[t][free_] = (KNote){(uint8_t)(p << 4 | step), (uint8_t)len, (uint8_t)note, (uint8_t)vel};
    roll_bits(a, t, p);
    return free_;
}
int k_roll_at(const KApp *a, int t, int p, int step, int note) {
    int i, best = -1;
    if (t < 0 || t >= K_TRACKS) return -1;
    for (i = 0; i < K_ROLL_NOTES; i++) {
        const KNote *n = &a->roll[t][i];
        const int s0 = n->at & 15;
        if (!n->len || (n->at >> 4) != p || n->note != note || step < s0 || step >= s0 + n->len) continue;
        if (best < 0 || s0 > (a->roll[t][best].at & 15)) best = i;   /* (two over it: the later one, the one on top) */
    }
    return best;
}
void k_roll_set(KApp *a, int t, int slot, int step, int note, int len) {
    KNote *n;
    if (t < 0 || t >= K_TRACKS || slot < 0 || slot >= K_ROLL_NOTES || !a->roll[t][slot].len) return;
    n = &a->roll[t][slot];
    step = clamp(step, 0, K_STEPS - 1); note = clamp(note, 0, 127);
    {   /* a move onto another note (the same step and pitch): refused, the note stays (2026-11-12: two made one) */
        int o;
        for (o = 0; o < K_ROLL_NOTES; o++)
            if (o != slot && a->roll[t][o].len && a->roll[t][o].at == (uint8_t)((n->at & 0xF0) | step) && a->roll[t][o].note == note) return;
    }
    n->at = (uint8_t)((n->at & 0xF0) | step); n->note = (uint8_t)note;
    n->len = (uint8_t)clamp(len, 1, len_room(a, t, n->at >> 4, step, n->note, slot));   /* (it stops at the next note it meets) */
    roll_bits(a, t, n->at >> 4);
}
void k_roll_remove(KApp *a, int t, int slot) {
    int p;
    if (t < 0 || t >= K_TRACKS || slot < 0 || slot >= K_ROLL_NOTES || !a->roll[t][slot].len) return;
    p = a->roll[t][slot].at >> 4;
    a->roll[t][slot].len = 0;
    if (a->rsel[t] == slot) a->rsel[t] = -1;
    roll_bits(a, t, p);
}
void k_roll_clear(KApp *a, int t, int p, int step) {
    int i;
    if (t < 0 || t >= K_TRACKS) return;
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && a->roll[t][i].at == (uint8_t)(p << 4 | (step & 15))) { a->roll[t][i].len = 0; if (a->rsel[t] == i) a->rsel[t] = -1; }
    roll_bits(a, t, p);
}
void k_roll_fill(KApp *a, int t, int p, unsigned bits, int note) {
    int i;
    if (t < 0 || t >= K_TRACKS || p < 0 || p > 3) return;
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && (a->roll[t][i].at >> 4) == p) a->roll[t][i].len = 0;
    for (i = 0; i < K_STEPS; i++) if (bits >> i & 1u) k_roll_add(a, t, p, i, note, 1, 100);
    roll_bits(a, t, p);
    a->rsel[t] = -1;
}
/* ROLL's note in hand: the step in hand's lowest (its knobs then turn it), none when the step is empty (they set the next one's) */
static void roll_pick(KApp *a, int t) {
    int i, best = -1;
    const uint8_t at = (uint8_t)(a->pattern[t] << 4 | (a->cur[t] & 15));
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && a->roll[t][i].at == at && (best < 0 || a->roll[t][i].note < a->roll[t][best].note)) best = i;
    if (a->rsel[t] >= 0 && a->rsel[t] < K_ROLL_NOTES && a->roll[t][a->rsel[t]].len && a->roll[t][a->rsel[t]].at == at) best = a->rsel[t];   /* (the one picked, kept) */
    a->rsel[t] = best;
    if (best >= 0) { a->rnote[t] = a->roll[t][best].note; a->rlen[t] = a->roll[t][best].len; a->rvel[t] = a->roll[t][best].vel; }
}
void k_roll_select(KApp *a, int t, int slot) {
    if (t < 0 || t >= K_TRACKS) return;
    if (slot < 0 || slot >= K_ROLL_NOTES || !a->roll[t][slot].len) { a->rsel[t] = -1; return; }
    a->rsel[t] = slot; a->cur[t] = a->roll[t][slot].at & 15;
    a->rnote[t] = a->roll[t][slot].note; a->rlen[t] = a->roll[t][slot].len; a->rvel[t] = a->roll[t][slot].vel;
}
/* ROLL's knobs turned: the note in hand takes them (its step stays; a note onto another's pitch there takes that one's place) */
static void roll_commit(KApp *a, int t) {
    KNote *n;
    int o;
    if (a->rsel[t] < 0 || a->rsel[t] >= K_ROLL_NOTES || !a->roll[t][a->rsel[t]].len) return;
    n = &a->roll[t][a->rsel[t]];
    a->rnote[t] = clamp(a->rnote[t], 0, 127); a->rvel[t] = clamp(a->rvel[t], 1, 127);
    {   /* a pitch another note of the step holds is passed over, the way NOTE turns (2026-11-12: it took that note away: a chord lost
         * a note as NOTE went through it); none free that way: it stays */
        const int d = a->rnote[t] > n->note ? 1 : -1;
        int q = a->rnote[t], busy = 1;
        while (busy && q >= 0 && q <= 127) {
            busy = 0;
            for (o = 0; o < K_ROLL_NOTES; o++) if (o != a->rsel[t] && a->roll[t][o].len && a->roll[t][o].at == n->at && a->roll[t][o].note == q) { busy = 1; break; }
            if (busy) q += d;
        }
        a->rnote[t] = q >= 0 && q <= 127 ? q : n->note;
    }
    a->rlen[t] = clamp(a->rlen[t], 1, len_room(a, t, n->at >> 4, n->at & 15, a->rnote[t], a->rsel[t]));   /* (it stops at the next note it meets) */
    if (n->note == a->rnote[t] && n->len == a->rlen[t] && n->vel == a->rvel[t]) return;
    n->note = (uint8_t)a->rnote[t]; n->len = (uint8_t)a->rlen[t]; n->vel = (uint8_t)a->rvel[t];
}
/* a step switched (STEP, ROLL's cell): its notes gone, or the note in hand's pitch, length and velocity put there */
static void step_toggle(KApp *a, int t, int s) {
    const int p = a->pattern[t];
    a->cur[t] = s;
    if (k_roll_count(a, t, p, s)) k_roll_clear(a, t, p, s);
    else a->rsel[t] = k_roll_add(a, t, p, s, a->rnote[t], a->rlen[t], a->rvel[t]);
    roll_pick(a, t);
}
int k_lane_step_set(KApp *a, int t, int l, int s, int on) {
    uint16_t m;
    if ((unsigned)t >= K_TRACKS || (unsigned)l >= K_LANES || (unsigned)s >= K_STEPS) return 0;
    m = (uint16_t)(1u << s);
    if (!!(a->lane_on[t][l] & m) == !!on) return 0;
    a->lane_on[t][l] ^= m;
    return 1;
}
int k_lane_step_toggle(KApp *a, int t, int l, int s) {
    if ((unsigned)t >= K_TRACKS || (unsigned)l >= K_LANES || (unsigned)s >= K_STEPS) return 0;
    a->cur[t] = (KVal)s;   /* (the step in hand: a tool's, CHANCE) */
    return k_lane_step_set(a, t, l, s, !((a->lane_on[t][l] >> s) & 1));
}
int k_chance(const KApp *a, int t, int p, int s) {
    if ((unsigned)t >= K_TRACKS || (unsigned)p >= 4 || (unsigned)s >= K_STEPS) return 0;
    return (int)(a->chance[t][p] >> (2 * s) & 3u);
}
int k_chance_set(KApp *a, int t, int p, int s, int c) {
    uint32_t w;
    if ((unsigned)t >= K_TRACKS || (unsigned)p >= 4 || (unsigned)s >= K_STEPS) return 0;
    c = c < 0 ? 0 : c > 3 ? 3 : c;
    w = (a->chance[t][p] & ~(3u << (2 * s))) | (uint32_t)c << (2 * s);
    if (w == a->chance[t][p]) return 0;
    a->chance[t][p] = w;
    return 1;
}
int k_step_on(const KApp *a, int t, int s) {
    int l;
    if (!k_is_drum(a, t)) return (a->steps[t][a->pattern[t]] >> s) & 1;
    for (l = 0; l < K_LANES; l++) if ((a->lane_on[t][l] >> s) & 1) return 1;
    return 0;
}
/* the keys of the bed in the order of pitch: 16 white and 11 black make 27 semitones from F: a white key's place is its index, a
 * black key follows the white key given in BLACK_AFTER (the FM-1's bed, as Felucca's: key k plays 53 + k) */
static const int WHITE_SEMI[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const int BLACK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
static int key_chroma(int id) { return id < 16 ? WHITE_SEMI[id] : WHITE_SEMI[BLACK_AFTER[id - 16]] + 1; }
int k_track_missing(const KApp *a, int t) { return (unsigned)t < K_TRACKS && a->missing[t].uid != 0; }
int k_track_newer(const KApp *a, int t) { return k_track_missing(a, t) && a->missing[t].newer; }
int k_page_unused(const KApp *a, KPage page)
{
    const int t = a->track;
    if (page != K_PAGE_ENV1 && page != K_PAGE_ENV2 && page != K_PAGE_LFO) return 0;
    return !k_track_missing(a, t) && a->sound[t] >= 0 && a->sound[t] < K_ENGINE_COUNT &&
           (k_engine_at(a->sound[t])->gate || k_engine_at(a->sound[t])->uid == K_UID('D', 'R', 'U', 'M'));   /* (DRUM: its lanes' are on SOUND) */
}
int k_track_audible(const KApp *a, int t) { return a->solo ? (a->solo >> t) & 1 : !((a->mute >> t) & 1); }
void k_mute_toggle(KApp *a, int t) { if ((unsigned)t < K_TRACKS) a->mute ^= (uint8_t)(1u << t); }
void k_solo_toggle(KApp *a, int t) { if ((unsigned)t < K_TRACKS) a->solo ^= (uint8_t)(1u << t); }
int k_key_pitch(const KApp *a, int id) { return clamp(53 + key_chroma(clamp(id, 0, 26)) + 12 * a->octave, 0, 127); }
int k_key_playing(const KApp *a, int i) {
    return a->playing && k_is_drum(a, a->track) && i >= 0 && i < K_STEPS && a->step >= 0 && a->step % K_STEPS == i;
}
int k_key_lit(const KApp *a, int i) {
    int t = a->track;
    if (i < 0 || i > 26) return 0;
    if (!k_is_drum(a, t) && (a->page == K_PAGE_ARP || a->params[t][K_PAGE_ARP][0])) {   /* the arp on (or its page): the key of the note it is
                                                 * playing, on every page (outside the bed: folded in by octaves), lit until the next one */
        int n, lo = k_key_pitch(a, 0);
        if (!a->arp_n[t] || a->clock_ms - a->arp_ms[t] >= 400u) return 0;
        n = a->arp_hist[t][(a->arp_n[t] - 1) & 7];
        while (n < lo) n += 12;
        while (n > lo + 26) n -= 12;
        return k_key_pitch(a, i) == n;
    }
    if (a->page == K_PAGE_ROLL) {   /* the keys that play the note of the step in hand (while stopped) */
        int c = a->cur[t];
        if (a->playing) return 0;
        { int k; for (k = 0; k < K_ROLL_NOTES; k++) if (a->roll[t][k].len && a->roll[t][k].at == (uint8_t)(a->pattern[t] << 4 | c) && k_key_pitch(a, i) == a->roll[t][k].note) return 1; }
        return 0;
    }
    if (k_is_drum(a, t)) return i < K_STEPS ? (a->lane_on[t][a->cur_lane[t]] >> i) & 1 : i - K_STEPS == a->cur_lane[t];   /* a drum track: the lane in hand's steps, and its black key */
    if (i >= K_STEPS) return 0;
    if (a->page == K_PAGE_STEP) return (a->steps[t][a->pattern[t]] >> i) & 1;
    if (a->page == K_PAGE_DRUM) return (a->lane_on[t][a->cur_lane[t]] >> i) & 1;
    return 0;
}
/* the track or its engine changed: SEQ goes to the view that suits the track (a drum track has lanes, not notes) */
static void layout(KApp *a);
static void engine_step(KApp *a,int d,uint32_t ms);
static void seq_follow(KApp *a) {
    if ((a->page == K_PAGE_ROLL || a->page == K_PAGE_STEP) && !k_is_drum(a, a->track)) return;   /* a synth track: the roll and the steps both fit */
    if ((a->page == K_PAGE_STEP || a->page == K_PAGE_DRUM || a->page == K_PAGE_ROLL) && a->page != k_seq_page(a)) {
        a->page = k_seq_page(a); a->focused = 0; a->win = 0; a->touched = 0; layout(a);
    }
}

static void node(KApp *a, KNodeKind kind, int index) {
    if (a->focus_count >= K_MAX_FOCUS) return;
    a->focus[a->focus_count++] = (KFocusNode){kind, index, {0,0,0,0}};
}
/* The FX page: two insert slots after the synth, four items each: the effect (0 NONE .. 4 REV: a knob steps it), two
 * parameters (what they are depends on the effect) and the mix. Item i is slot i / 4, field i % 4.
 * Every page has four parameters for the selected track, but the mixer: its items run across the tracks, a row for each of
 * LEVEL, PAN, BUS1, BUS2 (item i is parameter i / 4 of track i % 4), so four knobs reach four tracks and SELECT steps down. */
/* a track's synth parameters at the defaults of its engine */
static void engine_defaults(KApp *a,int t);
/* a switch of a track's engine from `old`: what the old one had is kept, and the new one comes back as the track last had it
 * (its defaults the first time). Nothing is lost by turning past an engine: turning back finds the sound as it was. */
static void engine_switch(KApp *a,int t,int old) {
    int i,e=a->sound[t];
    a->preset_slot[t]=-1;a->factory[t]=-1;a->preset_name[t][0]=0;   /* (another engine: no preset's sound any more) */
    a->preset_dirty[t]=0;
    if(old>=0 && old<K_ENGINE_COUNT+8 && !a->missing[t].uid){
        for(i=0;i<K_ENGINE_PARAMS;i++)a->eng_mem[t][old][i]=(int16_t)a->params[t][K_PAGE_SOUND][4+i];
        a->eng_mem_set[t]|=1u<<old;
    }
    if(e>=0 && e<K_ENGINE_COUNT+8 && (a->eng_mem_set[t]>>e&1u)){
        a->missing[t].uid=0;
        for(i=0;i<k_engine_at(e)->nparams;i++)a->params[t][K_PAGE_SOUND][4+i]=k_param_clamp(&k_engine_at(e)->edit[i],a->eng_mem[t][e][i]);
    } else engine_defaults(a,t);
}
static void engine_defaults(KApp *a,int t) {
    int i;
    a->missing[t].uid=0;   /* another engine chosen: what the missing plugin held is let go, as a DAW does on a replace */
    for(i=0;i<K_ENGINE_PARAMS;i++)a->params[t][K_PAGE_SOUND][4+i]=i<k_engine_at(a->sound[t])->nparams?k_engine_at(a->sound[t])->edit[i].def:0;   /* items 4..: its rows */
    if(a->plug_lane_uid[t]==k_engine_uid(a->sound[t]))a->plug_lane_uid[t]=0;   /* (an outside drum's lanes: at the defaults too) */
    /* an organ (KEngine.gate) plays its own gate (kreisel_audio.c): the track's ENV 1 is left as it was, for the next engine */
}
#include "presets_factory.c"   /* the factory presets */
int k_factory_apply(KApp *a, int t, int n) {
    const int i = factory_at(k_engine_uid(a->sound[t]), n);
    int k;
    if (i < 0 || k_track_missing(a, t)) return -1;
    for (k = 0; k < K_MAX_PARAMS; k++) a->params[t][K_PAGE_ENV_DEST][k] = 50;   /* (where ENV 1 goes: none, as the last sound's must not
                                                * stay; then the engine's own defaults, its ENV link among them) */
    engine_defaults(a, t);
    for (k = 0; k < 8; k++) a->params[t][K_PAGE_ENV1][k] = 50;
    for (k = 0; k < 8; k++) a->params[t][K_PAGE_ENV2][k] = 50;
    for (k = 0; k < 8; k++) a->params[t][K_PAGE_LFO][k] = k_page_desc(K_PAGE_LFO)[k].def;
    for (k = 0; k < K_MAX_PARAMS; k++) a->params[t][K_PAGE_FX][k] = k == 0 || k == 4 ? 0 : 50;   /* the inserts empty: a preset holds them
                                                * (a user preset brings its own), so a factory one does not keep the last sound's */
    factory_set(a, t, i);
    a->preset_slot[t] = -1; a->factory[t] = (int8_t)n; a->preset_dirty[t] = 0;
    snprintf(a->preset_name[t], sizeof a->preset_name[t], "%s", FACTORY[i].name);
    return 0;
}
int k_engine_macro(int index, int k) {   /* an engine's macros are its own declaration (KEngine.macro) */
    int m = k_engine_at(index)->macro[k < 0 ? 0 : k > 3 ? 3 : k];
    return m < k_engine_at(index)->nparams ? m : 0;
}
int k_page_rowed(KPage page) { return K_IS_HOME(page) || page == K_PAGE_SOUND || page == K_PAGE_EDIT || page == K_PAGE_ENV1 || page == K_PAGE_ENV2 || page == K_PAGE_FX || page == K_PAGE_LFO; }
int k_param_count(KPage page) { return page == K_PAGE_HOME ? 4 : K_IS_HOME(page) ? K_HOME_ITEMS :   /* (HOME: the macros alone; TRACKS: the levels, pans, colours too) */ page == K_PAGE_SONG ? 0 : page == K_PAGE_CLOCK ? 3 : page == K_PAGE_STEP || page == K_PAGE_DRUM ? 4 :   /* (SONG: no cards, its knobs are its tracks) */ page == K_PAGE_SOUND ? K_ENGINE_PARAMS : page == K_PAGE_EDIT || page == K_PAGE_ENV1 || page == K_PAGE_ENV2 ? 8 : page == K_PAGE_FX ? K_FX_ITEMS : page == K_PAGE_LFO ? 8 : 4; }
static int drum_kit(const KApp *a) { return k_engine_uid(a->sound[a->track]) == K_UID('D', 'R', 'U', 'M') && !k_track_missing(a, a->track); }   /* Kreisel's DRUM */
/* PHASE has three envelopes, as phase-distortion synths have them: DCO (pitch), DCW (wave), DCA (level), on one ENV page of three rows. They are kept where
 * the songs had them: DCW ENV 1's A D S R, DCA ENV 2's, DCO ENV 2's items 4 .. 7 (saved with ENV 2 since 2026-11-06). */
static int phase_envelopes(const KApp *a) {
    return !k_track_missing(a, a->track) && k_engine_uid(a->sound[a->track]) == K_UID('K','P','H','S');
}
static int env_pair(const KApp *a, KPage page) { return (a->lay & K_LAY_ENV_PAIR) && page == K_PAGE_ENV1 && !drum_kit(a) && !phase_envelopes(a); }
/* Keep the original FX storage indexes 0..7 for legacy songs, but show only
 * the rows with parameters of the selected effects. The two added storage
 * ranges 8..15 and 16..23 provide up to ten parameters per slot. */
static int fx_extra_rows(const KApp *a, int slot)
{
    const KPlugin *plug = k_effect_at(a->params[a->track][K_PAGE_FX][slot * 4]);
    int n = plug && plug->desc ? (int)plug->desc->nparams - ((a->lay & K_LAY_FX_ALGO) ? 3 : 2) : 0;
    return n > 0 ? (n + 3) / 4 : 0;
}
int k_fx_item(const KApp *a, int item, int *slot, int *param)
{
    int s, k, store;
    if (item < 0 || item >= 8 + 4 * (fx_extra_rows(a, 0) + fx_extra_rows(a, 1))) {
        if (slot) *slot = 1;
        if (param) *param = -3;
        return K_FX_ITEMS - 1; /* invalid/unused virtual item: never index past stored values */
    }
    if (item < 8 && (a->lay & K_LAY_FX_ALGO)) {   /* ALGORITHM picks the type: P1 P2 P3 MIX (P3 kept with the extras) */
        s = item < 4 ? 0 : 1;
        k = item % 4 == 3 ? -2 : item % 4;
        store = k >= 0 ? k_fx_store_param(s, k) : s * 4 + 3;
    } else if (item < 8) {
        s = item < 4 ? 0 : 1;
        k = item % 4 == 0 ? -1 : item % 4 == 3 ? -2 : item % 4 - 1;
        store = item;
    } else {
        int first = fx_extra_rows(a, 0) * 4;
        s = item - 8 < first ? 0 : 1;
        k = ((a->lay & K_LAY_FX_ALGO) ? 3 : 2) + item - 8 - (s ? first : 0);
        store = k_fx_store_param(s, k);
    }
    if (k >= 0) {
        const KPlugin *plug = k_effect_at(a->params[a->track][K_PAGE_FX][s * 4]);
        if (!plug || !plug->desc || k >= plug->desc->nparams) k = -3;
    }
    if (slot) *slot = s;
    if (param) *param = k;
    return store;
}
int k_fx_type_set(KApp *a, int slot, int type)
{
    KVal *v;
    if (slot < 0 || slot >= K_FX_SLOTS) return 0;
    v = &a->params[a->track][K_PAGE_FX][slot * 4];
    type = clamp(type, 0, k_effect_count());
    if (*v == type) return 0;
    *v = type;
    a->preset_dirty[a->track] = 1;
    return 1;
}
int k_fx_type_step(KApp *a, int slot, int d, int wrap)
{
    const int top = k_effect_count();
    int nv;
    if (slot < 0 || slot >= K_FX_SLOTS || !d) return 0;
    nv = a->params[a->track][K_PAGE_FX][slot * 4] + (d > 0 ? 1 : -1);
    if (wrap) nv = nv < 0 ? top : nv > top ? 0 : nv;
    return k_fx_type_set(a, slot, nv);
}
int k_song_cell_set(KApp *a, int scene, int t, int v)
{
    if ((unsigned)scene >= K_SCENES || (unsigned)t >= K_TRACKS) return 0;
    v = clamp(v, 0, 4);
    if (a->song[scene][t] == v) return 0;
    a->song[scene][t] = (uint8_t)v;
    return 1;
}
uint16_t *k_lanes_mut(KApp *a, int t, int p) { p &= 3; return p == (a->pattern[t] & 3) ? a->lane_on[t] : a->lane_pat[t][p]; }
const uint16_t *k_lanes(const KApp *a, int t, int p) { return k_lanes_mut((KApp *)a, t, p); }
int k_pattern_set(KApp *a, int t, int p)
{
    if ((unsigned)t >= K_TRACKS) return 0;
    p = clamp(p, 0, 3);
    if (a->pattern[t] == p) return 0;
    memcpy(a->lane_pat[t][a->pattern[t] & 3], a->lane_on[t], sizeof a->lane_on[t]);   /* (the drum lanes: the one in hand's put back, */
    memcpy(a->lane_on[t], a->lane_pat[t][p], sizeof a->lane_on[t]);                    /* the new one's taken) */
    a->pattern[t] = p;
    return 1;
}
int k_song_cell_step(KApp *a, int scene, int t, int d, int wrap)
{
    int v;
    if ((unsigned)scene >= K_SCENES || (unsigned)t >= K_TRACKS) return 0;
    v = a->song[scene][t];
    v = (v >= 4 ? 0 : v + 1) + d;   /* (in the knobs' order: none, A, B, C, D; a value key from none: A, not past D) */
    v = wrap ? ((v % 5) + 5) % 5 : v < 0 ? 0 : v > 4 ? 4 : v;
    return k_song_cell_set(a, scene, t, v == 0 ? 4 : v - 1);
}
/* the items a page has on this screen and track */
int k_page_count(const KApp *a, KPage page)
{
    if (page == K_PAGE_FX) return 8 + 4 * (fx_extra_rows(a, 0) + fx_extra_rows(a, 1));
    if (phase_envelopes(a) && (page == K_PAGE_ENV1 || page == K_PAGE_ENV2)) return 12;   /* DCO | DCW | DCA, A D S R each */
    return env_pair(a, page) ? 16 : drum_kit(a) && page == K_PAGE_ENV1 ? 4 :
           drum_kit(a) && page == K_PAGE_LFO ? 9 : k_param_count(page);
}
KPage k_item_page(const KApp *a, KPage page, int *i) { if (env_pair(a, page) && *i >= 8) { *i -= 8; return K_PAGE_ENV2; } return page; }
/* the Switch's SOUND (K_LAY_SYNTH_ALL): its items past the engine's are ENV 1's eight, ENV 2's five, the LFO's eight, as those pages
 * number them; 1 and *pg, *j that page and item, when i is one */
int k_synth_all_sub(const KApp *a, KPage page, int i, KPage *pg, int *j) {
    int k;
    if (!(a->lay & K_LAY_SYNTH_ALL) || page != K_PAGE_SOUND || k_track_missing(a, a->track)) return 0;
    k = i - k_engine_at(a->sound[a->track])->nparams;
    if (k < 0 || k >= K_SYNTH_ALL_MOD) return 0;
    {   /* ENV 1's page as it numbers its items on this screen (paired with ENV 2: 16; PHASE: DCO DCW DCA, 12; a kit: 4), then the LFO's */
        const int n1 = k_page_count(a, K_PAGE_ENV1);
        if (k < n1) { *pg = K_PAGE_ENV1; *j = k; return 1; }
        if (k - n1 < k_page_count(a, K_PAGE_LFO)) { *pg = K_PAGE_LFO; *j = k - n1; return 1; }
    }
    return 0;
}
/* a page's item: a lookup into the values' table (src/kreisel_param.c), which says where it lives, its range and its default */
KVal *k_param(KApp *a, KPage page, int i) { return k_param_at(a, k_param_ref(a, page, i)); }
int k_param_get(const KApp *a, KPage page, int i) { return *k_param((KApp *)a, page, i); }
/* a value set outright (a desktop's paste), within what it can be; an insert's effect and ROLL's step not. 1: set */
int k_param_set(KApp *a, KPage page, int i, int v) { return k_ref_set(a, k_param_ref(a, page, i), v); }
/* a value back where it starts (a desktop's option-click): its default in the table. An insert's effect and ROLL's step stay. 1: done */
int k_param_reset(KApp *a, KPage page, int i) { return k_ref_reset(a, k_param_ref(a, page, i)); }
int k_params_visible(const KApp *a) {
    int n = k_page_count(a, a->page);
    if ((a->lay & K_LAY_SYNTH_ALL) && a->page == K_PAGE_SOUND) {   /* (all of it, and its ENV and LFO: as many as k_synth_all_sub maps) */
        int m;
        if (k_track_missing(a, a->track)) return 1;
        m = k_page_count(a, K_PAGE_ENV1) + k_page_count(a, K_PAGE_LFO);
        return k_engine_at(a->sound[a->track])->nparams + (m < K_SYNTH_ALL_MOD ? m : K_SYNTH_ALL_MOD);
    }
    if ((a->lay & K_LAY_SYNTH_ALL) && a->page == K_PAGE_FX) return n;   /* (a big screen's inserts: every control of both, at once) */
    return a->shown < n ? a->shown : n;
}
/* Nodes only say what can be focused; the platform's UI places them (k_set_bounds). The parameters are the window. */
static void layout(KApp *a);
/* SONG: the cursor onto track t's cell of scene sc, the window moved to show it */
static void song_focus(KApp *a, int t, int sc) {
    sc = clamp(sc, 0, K_SCENES - 1); t = clamp(t, 0, K_TRACKS - 1);
    if (sc < a->song_off) a->song_off = (uint8_t)sc;
    else if (sc >= a->song_off + K_SONG_VIS) a->song_off = (uint8_t)(sc - K_SONG_VIS + 1);
    layout(a);
    a->focused = t * K_SONG_VIS + (sc - a->song_off);
}
static void layout(KApp *a) {
    int i, n, vis = k_params_visible(a);
    a->win = clamp(a->win, 0, k_page_rowed(a->page) && vis > 0 ? (k_page_count(a, a->page) - 1) / vis * vis : k_page_count(a, a->page) - vis);   /* (a rowed page: the last row may be short) */
    a->focus_count = 0;
    if (K_IS_HOME(a->page)) {
        for (i=0; i<K_TRACKS; ++i) node(a,K_NODE_TRACK,i);
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);
    } else if (a->page == K_PAGE_STEP || a->page == K_PAGE_ROLL) {
        for (i=0; i<K_STEPS; ++i) node(a,K_NODE_STEP,i);
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);
    } else if (a->page == K_PAGE_DRUM) {   /* a cell is lane * 16 + step */
        for (i=0; i<K_LANES*K_STEPS; ++i) node(a,K_NODE_STEP,i);
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);
    } else if (a->page == K_PAGE_SONG) {
        a->song_off = (uint8_t)clamp(a->song_off, 0, K_SCENES - K_SONG_VIS);
        for (n=0; n<K_TRACKS*K_SONG_VIS; ++n) node(a,K_NODE_SCENE,(n/K_SONG_VIS)*K_SCENES+a->song_off+n%K_SONG_VIS);   /* (the window's cells: a
                                                 * node's index is still track * K_SCENES + scene) */
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);   /* the sequencer's cards: tempo, swing, clock, the step's chance */
    } else if (a->page == K_PAGE_MIXER) {
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);
        for (i=0; i<K_TRACKS; ++i) node(a,K_NODE_TRACK,i);   /* the strips (after the controls: a tap on the strip in hand's finds those first) */
    } else {
        for (i=0; i<vis; ++i) node(a,K_NODE_PARAM,a->win+i);
        if (a->page == K_PAGE_SOUND) node(a,K_NODE_SOUND,0);   /* the engine's bar, last (the row's first knob is in hand first) */
    }
    a->focused = clamp(a->focused,0,a->focus_count-1);
}
void k_set_bounds(KApp *a, KNodeKind kind, int index, KRect r) {
    int i;
    for (i=0; i<a->focus_count; ++i)
        if (a->focus[i].kind==kind && a->focus[i].index==index) { a->focus[i].bounds=r; return; }
}
void k_init(KApp *a) {
    int t,s,p,j;
    memset(a,0,sizeof(*a));
    {static const int TC[8]={7,1,4,6,3,2,5,0};for(t=0;t<K_TRACKS;t++)a->tcolor[t]=TC[t];}   /* Pink, Orange, Cyan, Purple, then the other four */
    a->bpm=120;
    for(t=0;t<K_TRACKS;t++){a->preset_slot[t]=-1;a->factory[t]=-1;}
    a->shown=K_DEFAULT_SHOWN;
    a->touched=0;
    a->beat=-1;
    a->step=-1;a->last_idx=-1;
    a->battery=-1;
    a->master=80;
    a->group_held=-1;
    a->undo_t=-1;a->tool_rng=0x2545F491u;k_tools_init(a);
    for(t=0;t<K_TRACKS;t++) {
        memset(a->roll[t],0,sizeof a->roll[t]);a->steps[t][0]=a->steps[t][1]=a->steps[t][2]=a->steps[t][3]=0;
        a->rsel[t]=-1;a->rnote[t]=60;a->rlen[t]=1;a->rvel[t]=100;
        if(t==0)k_roll_fill(a,0,0,0x1111,60);   /* a pulse to start from */
        a->lane_on[t][0]=0x1111;a->lane_on[t][1]=0x1010;a->lane_on[t][2]=0x5555;   /* a beat to start a drum track from */
        for(s=0;s<K_LANES;s++){   /* a kit: BD SD CH OH CP, a low and a high tom, a rim (drum_kreisel.c's pieces) */
            static const int8_t PC[K_LANES]={0,3,21,22,15,6,6,10},TU[K_LANES]={64,64,64,64,64,40,90,64};
            a->lane_p[t][s][0]=PC[s];a->lane_p[t][s][1]=TU[s];a->lane_p[t][s][2]=64;a->lane_p[t][s][3]=64;a->lane_p[t][s][4]=100;
            a->lane_p[t][s][5]=0;a->lane_p[t][s][6]=50;a->lane_p[t][s][7]=50;a->lane_p[t][s][8]=0;a->lane_p[t][s][K_LANE_PAN]=50;}   /* ENV: no attack, the piece's sweep, no click; centred */
        for(s=0;s<K_SCENES;s++)a->song[s][t]=4;   /* the song empty: nothing in any scene (was A..D in pairs) */
        for(p=0;p<K_PAGE_COUNT;p++)
            for(j=0;j<K_MAX_PARAMS;j++)a->params[t][p][j]=50;
        a->params[t][K_PAGE_FX][0]=0;a->params[t][K_PAGE_FX][4]=0;   /* both inserts start empty */
        a->params[t][K_PAGE_MIXER][0]=K_LEVEL_0DB;   /* the level at 0 dB (kreisel_level.h) */
        a->params[t][K_PAGE_BUS1][3]=a->params[t][K_PAGE_BUS2][3]=K_LEVEL_0DB;   /* the buses' RTN: 0 dB */
        a->params[t][K_PAGE_MIXER][2]=a->params[t][K_PAGE_MIXER][3]=0;   /* B1, B2: sending nothing (a bus runs only while sent to) */
        engine_defaults(a,t);
        {int pg,k;for(pg=0;pg<K_PAGE_COUNT;pg++){const KParamDesc *d=k_page_desc((KPage)pg);if(d&&pg!=K_PAGE_CLOCK&&pg!=K_PAGE_SONG)for(k=0;k<(pg==K_PAGE_LFO?9:4);k++)a->params[t][pg][k]=d[k].def;}}   /* LFO, ARP, SCL start at their defaults */
    }
    a->page=K_PAGE_MIXER;   /* HOME is the mixer */
    layout(a);
}
void k_open_page(KApp *a,KPage p) {
    int env2_half;
    if ((unsigned)p>=K_PAGE_COUNT) return;
    if (k_page_folded(a,p)) p=K_PAGE_SOUND;   /* (folded into SOUND on this screen) */
    a->tools_open=0;a->tools_try=-1;a->tools_stage=0;   /* (another page: the tools' list closes, what was tried kept) */
    /* Paired ENV pages are one canvas, but choosing ENV2 must focus its
     * right-hand DCA/ENV2 controls. The old redirect always reset to row 0
     * (ENV1), making the ENV2 menu choice appear broken on wide screens. */
    env2_half = p==K_PAGE_ENV2 && (a->lay&K_LAY_ENV_PAIR) && !drum_kit(a);
    if (p==K_PAGE_ENV2 && phase_envelopes(a)) {p=K_PAGE_ENV1;env2_half=1;}   /* PHASE: one page; ENV 2 opens its DCA row */
    if (p==K_PAGE_ENV2 && ((a->lay&K_LAY_ENV_PAIR) || drum_kit(a))) p=K_PAGE_ENV1;
    if (p==K_PAGE_ROLL && k_is_drum(a,a->track)) p=K_PAGE_DRUM;   /* a drum track has lanes, not a roll */
    if (p!=a->page && p==K_PAGE_SOUND && !a->set_v[K_SET_HELP]) k_balloon_show(&a->balloon,K_BALLOON_HINT,a->clock_ms);   /* (SYNTH: how to move its rows; HELP OFF: none) */
    else if (p!=a->page && K_PAGE_BALLOON) k_balloon_show(&a->balloon,K_BALLOON_PAGE,a->clock_ms);   /* a new page names itself for a moment */
    a->page=p;a->focused=0;a->win=env2_half?8:0;a->touched=0;a->menu_open=0;a->menu_is_hold=0;a->menu_changed=0;layout(a);
}
/* the settings: a row each, its value turned or pressed */
#include "kreisel_version.h"
#include "kreisel_io.h"
static const char *const SV_ONOFF[2]={"On","Off"}, *const SV_LEDS[3]={"On","Dim","Off"};   /* (words as words, not in capitals) */
static const char *const SV_SCREEN[5]={"Never","5 min","15 min","30 min","60 min"};
static const char *const SV_USB[2]={"Fixed","Volume"};   /* (the USB audio: at full level, or as the volume knob) */
static const char *const SV_OFFON[2]={"Off","On"}, *const SV_CLICK[3]={"Beep","Click","Wood"};
#if K_WITH_CONNECT
static const char *const SV_MIDI_IN[3]={"All","None","Named"}, *const SV_MIDI_CH[17]={"All","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
#endif
static const struct { const char *name; uint8_t tab, n; const char *const *v; } SETS[K_SETTINGS]={
    {"LEDs",0,3,SV_LEDS},{"Animation",0,2,SV_ONOFF},{"Screen off",0,5,SV_SCREEN},{"Help",0,2,SV_ONOFF},
    {"Restore last",1,2,SV_ONOFF},{"Clear all",1,0,0},{"About",1,0,0},
    {"Metronome",2,2,SV_OFFON},{"Sound",2,3,SV_CLICK},{"Count-in",2,2,SV_OFFON},
#if K_WITH_CONNECT
    {"MIDI in",3,3,SV_MIDI_IN},{"Channel",3,17,SV_MIDI_CH},{"Clock",3,0,0},   /* (the clock: KApp.clock_src itself, as SEQ's Clk card) */
    {"Learn",3,0,0},   /* (an action: OK starts MIDI learn, k_learn_start) */
#endif
    {"USB level",1,2,SV_USB},
};
static const char *const SET_TABS[K_SETTING_TABS]={"Display","System","Metro",
#if K_WITH_CONNECT
    "Connect",
#endif
};
const char *k_setting_name(int i) { return i>=0&&i<K_SETTINGS?SETS[i].name:""; }
int k_setting_tab(int i) { return i>=0&&i<K_SETTINGS?SETS[i].tab:0; }
const char *k_setting_tab_name(int t) { return t>=0&&t<K_SETTING_TABS?SET_TABS[t]:""; }
int k_setting_tabs(const KApp *a) { return K_WITH_CONNECT&&!(a->lay&K_LAY_CONNECT)?K_SETTING_TABS-1:K_SETTING_TABS; }
int k_setting_visible(const KApp *a,int i) {
    if(i<0||i>=K_SETTINGS)return 0;
    if(i==K_SET_LEDS)return (a->lay&K_LAY_LEDS)!=0;
    if(i==K_SET_ANIM)return 0;
    if(i==K_SET_USB_LEVEL)return (a->lay&K_LAY_USB_AUDIO)!=0;   /* (the animations stay; the row, which nothing read, is gone; its value kept in the files) */
#if K_WITH_CONNECT
    if(i>=K_SET_MIDI_IN&&i<=K_SET_MIDI_LEARN)return (a->lay&K_LAY_CONNECT)!=0;
#endif
    return 1;
}
int k_setting_first(const KApp *a) { int i; for(i=0;i<K_SETTINGS;i++)if(k_setting_visible(a,i))return i; return 0; }
static int setting_next(const KApp *a,int i,int d) {   /* the next row shown, round the ends */
    int k;
    for(k=0;k<K_SETTINGS;k++){i=wrap(i+d,K_SETTINGS);if(k_setting_visible(a,i))return i;}
    return i;
}
const char *k_setting_value(const KApp *a,int i) {
    if(i==K_SET_CLEAR)return a->set_ask?"Again: clear":"Turn";
    if(i==K_SET_ABOUT)return "Fune " KREISEL_VERSION;   /* (the name shown: Fune, 2026-11-13; the code keeps Kreisel's) */
#if K_WITH_CONNECT
    if(i==K_SET_MIDI_CLOCK)return a->clock_src?"Follow":"Internal";
    if(i==K_SET_MIDI_IN&&a->set_v[i]==2)return k_io_in_name()[0]?k_io_in_name():"Named";   /* (a part of a port's name, from io.conf) */
    if(i==K_SET_MIDI_LEARN)return "Start";
#endif
    if(i<0||i>=K_SETTINGS||!SETS[i].n)return "";
    return SETS[i].v[a->set_v[i]<SETS[i].n?a->set_v[i]:0];
}
static void song_clear(KApp *a);
static void setting_turn_at(KApp *a,int i,int d,int wrap);
static void setting_turn(KApp *a,int i,int d) { setting_turn_at(a,i,d,0); }   /* a knob: it stops at the ends */
static void setting_turn_at(KApp *a,int i,int d,int wrap) {   /* wrap: a tap or ACCEPT steps on, round the end (OFF back to ON) */
    if(!d||i<0||i>=K_SETTINGS)return;
    if(i==K_SET_CLEAR){   /* asked twice: the first turn arms it, the second makes a new song (the device's own state kept) */
        if(!a->set_ask){a->set_ask=1;return;}
        a->set_ask=0;song_clear(a);a->settings_open=0;return;
    }
#if K_WITH_CONNECT
    if(i==K_SET_MIDI_CLOCK){a->clock_src=a->clock_src?0:1;return;}   /* Internal / Follow (the MIDI clock in on port 1: a computer's) */
    if(i==K_SET_MIDI_LEARN){if(d>0)k_learn_start(a);return;}   /* (the settings close: the page's controls are pointed at) */
    if(i==K_SET_MIDI_IN&&!k_io_in_name()[0]){int v=a->set_v[i]?0:1;a->set_v[i]=(uint8_t)v;a->set_pending=1;return;}   /* (no name in io.conf: All, None) */
#endif
    if(SETS[i].n){int v=wrap?((int)a->set_v[i]+1)%SETS[i].n:clamp((int)a->set_v[i]+(d>0?1:-1),0,SETS[i].n-1);if(v!=a->set_v[i]){a->set_v[i]=(uint8_t)v;a->set_pending=1;}}
}
/* CLEAR ALL: a new song, as at the first power-on; what the device owns (its screen, battery, level, octave, settings) kept */
static void song_clear(KApp *a) {
    const unsigned lay=a->lay;const int shown=a->shown,battery=a->battery,charging=a->charging,master=a->master,octave=a->octave;
    const uint32_t clock_ms=a->clock_ms;const uint8_t pending=a->set_pending;uint8_t set_v[K_SETTINGS];
    memcpy(set_v,a->set_v,sizeof set_v);
    k_init_released(a);
    a->lay=lay;a->clock_ms=clock_ms;memcpy(a->set_v,set_v,sizeof set_v);a->set_pending=pending;   /* (a setting changed before: still to keep) */
    k_set_battery(a,battery,charging);k_set_shown(a,shown);a->master=master;a->octave=octave;
    memset(a->steps,0,sizeof a->steps);memset(a->lane_on,0,sizeof a->lane_on);memset(a->lane_pat,0,sizeof a->lane_pat);   /* (no first pattern either: all clear) */
    k_balloon_show(&a->balloon,K_BALLOON_ERASED,clock_ms);
}
void k_set_battery(KApp *a,int level,int charging) { a->battery=clamp(level,-1,4);a->charging=charging!=0; }
/* a peak 0..32767 as the meter's 0..100: its dB over -48 (6.02 dB a bit: the top bit and the next six as a fraction, Q8) */
static int peak_level(int p) {
    int b=0,f,l2;
    if(p<=0)return 0;
    if(p>32767)p=32767;
    while((p>>(b+1))>0)b++;
    f=b>=6?(p>>(b-6))&63:(p<<(6-b))&63;
    l2=(b<<8)+(f<<2)-(15<<8);   /* log2(p / 32768), Q8: -15*256 .. 0 */
    l2=100+l2*602/(256*48);     /* (6.02 dB a bit) over 48 dB */
    return clamp(l2,0,100);
}
void k_arp_note(KApp *a,int t,int note,uint32_t ms) {
    if((unsigned)t>=K_TRACKS||note<0||note>127)return;
    a->arp_hist[t][a->arp_n[t]&7]=(uint8_t)note;a->arp_n[t]=(uint8_t)(a->arp_n[t]==255?248:a->arp_n[t]+1);   /* (never back to 0, "none yet": 248 is 256 mod 8) */a->arp_ms[t]=ms;
}
void k_set_track_peaks(KApp *a,const uint16_t *pk,uint32_t ms) {   /* (up at once, down ~77 dB a second: a hit gone in ~0.6 s; at ~30 dB
                                                 * a second, as the output's, it seemed to hold) */
    const uint32_t dt=ms-a->trk_ms;
    const int fall=dt>1000u?100:(int)(dt*160u/1000u);
    int t;
    for(t=0;t<K_TRACKS;t++){const int n=peak_level(pk[t]),o=a->trk_lv[t]-fall;a->trk_lv[t]=(uint8_t)(n>o?n:o<0?0:o);}
    a->trk_ms=ms;a->trk_live=1;
}
void k_set_out_peak(KApp *a,int l,int r,uint32_t ms) {
    const uint32_t dt=ms-a->out_ms;
    const int fall=dt>1000u?100:(int)(dt*63u/1000u);   /* ~30 dB a second (63 of the 100 over 48 dB) */
    const int nl=peak_level(l),nr=peak_level(r);
    int ol=a->out_lv[0]-fall,or_=a->out_lv[1]-fall;
    a->out_lv[0]=(uint8_t)(nl>ol?nl:ol<0?0:ol);
    a->out_lv[1]=(uint8_t)(nr>or_?nr:or_<0?0:or_);
    a->out_ms=ms;
}
void k_set_shown(KApp *a,int n) { a->shown=clamp(n,1,K_MAX_PARAMS); layout(a); }
void k_set_layout(KApp *a,unsigned lay) {
    unsigned prior=a->lay;
    /* Preserve which envelope the user was editing on a layout change. */
    if (a->page==K_PAGE_ENV2 && (lay&K_LAY_ENV_PAIR)) {
        a->page=K_PAGE_ENV1;a->win=drum_kit(a)?0:8;
    } else if (a->page==K_PAGE_ENV1 && (prior&K_LAY_ENV_PAIR) &&
               !(lay&K_LAY_ENV_PAIR) && a->win>=8 && !drum_kit(a) && !phase_envelopes(a)) {   /* (PHASE: one page either way) */
        a->page=K_PAGE_ENV2;a->win=0;
    }
    a->lay=lay;
    layout(a);
}
/* Moving past the edge card of the window slides it, when parameters lie beyond: the focus goes on to the next one.
 * Returns 1 if it slid. The other nodes keep their places until the UI places them again. */
/* On a rowed page (the mixer: LEVEL, PAN, BUS1, BUS2 of the four tracks; the synth: the oscillator, the filter) SELECT steps
 * a whole row, round the end: the four knobs always hold one row, so there is no one card to be on. (A cursor, left or
 * right, still goes card to card.) */
/* DRUM: SELECT picks the lane (round the end), and the focus goes to that lane's first cell */
static void select_lane(KApp *a,int lane) {
    int i,t=a->track;
    a->cur_lane[t]=wrap(lane,K_LANES);
    for(i=0;i<a->focus_count;i++)
        if(a->focus[i].kind==K_NODE_STEP && a->focus[i].index==a->cur_lane[t]*K_STEPS){a->focused=i;break;}
}
/* ROLL: SELECT moves along the time, a step at a time (round the end) */
static void select_step(KApp *a,int s) {
    int i,t=a->track;
    a->cur[t]=wrap(s,K_STEPS);
    for(i=0;i<a->focus_count;i++)
        if(a->focus[i].kind==K_NODE_STEP && a->focus[i].index==a->cur[t]){a->focused=i;break;}
}
/* ROLL with the cursor a column (the FM-1's): SELECT walks the notes of a chord, low to high, before the next step; back, high to low
 * before the step before, which it enters at its top (a note of a chord picked, not only the lowest) */
static void roll_walk(KApp *a,int dir) {
    const int t=a->track;
    int i,n=0,idx=-1,list[K_ROLL_NOTES];
    uint8_t at=(uint8_t)(a->pattern[t]<<4|(a->cur[t]&15));
    for(i=0;i<K_ROLL_NOTES;i++)if(a->roll[t][i].len&&a->roll[t][i].at==at){   /* the step's notes, by pitch (insertion) */
        int k=n++;
        while(k>0&&a->roll[t][list[k-1]].note>a->roll[t][i].note){list[k]=list[k-1];k--;}
        list[k]=i;
    }
    if(a->rsel[t]<0||a->rsel[t]>=K_ROLL_NOTES||a->roll[t][a->rsel[t]].at!=at)roll_pick(a,t);
    for(i=0;i<n;i++)if(list[i]==a->rsel[t])idx=i;
    if(idx>=0&&idx+dir>=0&&idx+dir<n){k_roll_select(a,t,list[idx+dir]);return;}
    select_step(a,a->cur[t]+dir);
    a->rsel[t]=-1;roll_pick(a,t);   /* (the lowest of the next) */
    if(dir<0){   /* back: the step before at its highest */
        int best=-1;
        at=(uint8_t)(a->pattern[t]<<4|(a->cur[t]&15));
        for(i=0;i<K_ROLL_NOTES;i++)if(a->roll[t][i].len&&a->roll[t][i].at==at&&(best<0||a->roll[t][i].note>a->roll[t][best].note))best=i;
        if(best>=0)k_roll_select(a,t,best);
    }
}
static int page_rows(const KApp *a,KPage pg) {   /* the rows (of the knobs' count) a page has, the knobs as many as now */
    int vis=k_params_visible(a),cnt=k_page_count(a,pg);
    if(pg==K_PAGE_SOUND)cnt=k_engine_at(a->sound[a->track])->page2;   /* SOUND: the engine's first half */
    if(pg==K_PAGE_EDIT)cnt=k_engine_at(a->sound[a->track])->nparams-k_engine_at(a->sound[a->track])->page2;   /* EDIT: the rest */
    if(pg==K_PAGE_ENV1)cnt=drum_kit(a)?4:phase_envelopes(a)?12:(a->lay&K_LAY_ENV_PAIR)?8+5:7; /* PHASE: DCO DCW DCA, a row each */
    if(pg==K_PAGE_LFO&&drum_kit(a))cnt=9;   /* (LANE on a third row) */
    if(pg==K_PAGE_ENV2)cnt=phase_envelopes(a)?12:5;   /* (PHASE opens ENV 1); others: DLY HOLD CRV AMT, LVL */
    if(cnt<1)cnt=1;
    return vis>0?(cnt+vis-1)/vis:1;
}
static void mixer_row(KApp *a,int dir) {
    int vis=k_params_visible(a),rows=page_rows(a,a->page);
    if(vis<1)vis=1;
    a->win=clamp(a->win/vis+dir,0,rows-1)*vis;   /* SELECT: a cursor over the rows, stopping at the first and the last (no wrap) */
    layout(a);
    a->focused=0;
}
/* a page key pressed again on its own page turns to the group's next page (ENV: ENV 1, ENV 2), round to the first; the tabs
 * beside the preset are those pages (k_page_tabs). (A page's rows are all on its screen: SELECT picks them, not this key.) Where
 * the group has one page, or the page is not the key's, the key opens the group's first page, as ever. */
static int flip_pages(const KApp *a,int g,KPage *list) {   /* the group's synth pages, each once, in order */
    int j,k,n=0;
    for(j=0;j<group_sizes[g];j++){
        KPage q=group_page(a,g,j);
        if(q!=K_PAGE_SOUND&&q!=K_PAGE_EDIT&&q!=K_PAGE_ENV1&&q!=K_PAGE_ENV2&&q!=K_PAGE_LFO&&q!=K_PAGE_ARP&&q!=K_PAGE_SCL&&q!=K_PAGE_FX&&
           q!=K_PAGE_MIXER&&q!=K_PAGE_BUS1&&q!=K_PAGE_BUS2&&q!=K_PAGE_SONG)return 0;
        if(k_page_unused(a,q))continue;
        if(q==K_PAGE_ENV2&&((a->lay&K_LAY_ENV_PAIR)||phase_envelopes(a)||drum_kit(a)))continue;   /* (ENV 2 is on ENV 1's page there) */
        for(k=0;k<n&&list[k]!=q;k++){}
        if(k==n)list[n++]=q;
    }
    return n;
}
static int flip_at(const KApp *a,int g,KPage *list,int *n) {   /* the page in hand's place among them, or -1 */
    int j;
    *n=flip_pages(a,g,list);
    for(j=0;j<*n&&list[j]!=a->page;j++){}
    return j<*n?j:-1;
}
int k_page_tabs(const KApp *a,int *at) {   /* the tabs beside the preset (0: none) */
    KPage list[8];
    int g,n,j;
    *at=0;
    for(g=0;g<K_GROUP_ALL;g++)if((j=flip_at(a,g,list,&n))>=0){*at=j;return n>1?n:0;}
    return 0;
}
static int flip_page(KApp *a,int g) {
    KPage list[8];
    int n,j=flip_at(a,g,list,&n);
    if(j<0||n<2)return 0;
    k_open_page(a,list[(j+1)%n]);
    return 1;
}
static int slide(KApp *a,int dir) {
    KFocusNode old[K_MAX_FOCUS],n;
    int oc=a->focus_count,i,j,vis=k_params_visible(a);
    if(!a->focus_count) return 0;
    n=a->focus[a->focused];
    if(n.kind!=K_NODE_PARAM) return 0;
    int step=k_page_rowed(a->page)?vis:1;   /* a rowed page's window moves a whole row at a time */
    const int cnt=(a->lay&K_LAY_MIX_KEYS)&&vis>0?page_rows(a,a->page)*vis:k_page_count(a,a->page);   /* (the Zero: as far as its panel draws) */
    if(dir>0 && n.index==a->win+vis-1 && a->win+vis<cnt) a->win+=step;
    else if(dir<0 && n.index==a->win && a->win>0) a->win-=step;
    else return 0;
    memcpy(old,a->focus,sizeof old);
    layout(a);
    for(i=0;i<a->focus_count;i++) {
        for(j=0;j<oc;j++)
            if(old[j].kind==a->focus[i].kind && old[j].index==a->focus[i].index){a->focus[i].bounds=old[j].bounds;break;}
        if(a->focus[i].kind==K_NODE_PARAM && a->focus[i].index==n.index+dir)a->focused=i;
    }
    return 1;
}
KRect k_focus_rect(const KApp *a) {
    if (!a->focus_count) return (KRect){0,0,0,0};
    return a->focus[a->focused].bounds;
}
int k_hit_focus(const KApp *a,int x,int y) {
    int i;
    for(i=0;i<a->focus_count;i++){
        KRect r=a->focus[i].bounds;
        if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) return i;
    }
    return -1;
}
static int overlaps(int a0, int a1, int b0, int b1) { return a0 < b1 && b0 < a1; }
/* A direction goes first to what lies in line with the focus (a row for left and right, a column for up and down),
 * the nearest of it; with nothing in line, to the nearest by distance (the off-axis counted four times). */
/* The cursor on a grid: the targets in rows by where they stand (two are of a row when their heights overlap by half
 * the smaller one). Left and right: the next of the row that way, none past its end (it stays). Up and down: the nearest row that
 * way, and in it the target nearest across (its middle nearest the cursor's). */
static int same_row(KRect p,KRect q){
    const int lo=p.y>q.y?p.y:q.y,hi=p.y+p.h<q.y+q.h?p.y+p.h:q.y+q.h,mh=p.h<q.h?p.h:q.h;
    return hi-lo>=(mh+1)/2;
}
static int move_grid(KApp *a,int dx,int dy) {
    const KRect here=k_focus_rect(a);
    const int hx=here.x+here.w/2,hy=here.y+here.h/2;
    int i,best=-1,bd=INT_MAX;
    if(here.w<=0||here.h<=0)return 0;
    if(dx){
        for(i=0;i<a->focus_count;i++){
            const KRect r=a->focus[i].bounds;
            const int x=r.x+r.w/2-hx;
            if(i==a->focused||r.w<=0||r.h<=0||x*dx<=0||!same_row(here,r))continue;
            if(abs(x)<bd){bd=abs(x);best=i;}
        }
    }else{
        int row_y=INT_MAX;   /* the nearest row that way: the smallest step to a target's middle */
        for(i=0;i<a->focus_count;i++){
            const KRect r=a->focus[i].bounds;
            const int y=r.y+r.h/2-hy;
            if(i==a->focused||r.w<=0||r.h<=0||y*dy<=0||same_row(here,r))continue;
            if(abs(y)<row_y)row_y=abs(y);
        }
        if(row_y==INT_MAX)return 1;   /* (the last row: it stays) */
        {   KRect ref={0,0,0,0};   /* the row: those of a row with the nearest one */
            for(i=0;i<a->focus_count;i++){const KRect r=a->focus[i].bounds;const int y=r.y+r.h/2-hy;if(i!=a->focused&&r.w>0&&r.h>0&&y*dy>0&&abs(y)==row_y){ref=r;break;}}
            for(i=0;i<a->focus_count;i++){
                const KRect r=a->focus[i].bounds;
                const int y=r.y+r.h/2-hy,x=abs(r.x+r.w/2-hx);
                if(i==a->focused||r.w<=0||r.h<=0||y*dy<=0||!same_row(ref,r))continue;
                if(x<bd){bd=x;best=i;}
            }
        }
    }
    if(best>=0)a->focused=best;
    return 1;
}
static void move_spatial(KApp *a,int dx,int dy) {
    int i,best=-1,score=INT_MAX;
    KRect here=k_focus_rect(a);
    int hx=here.x+here.w/2,hy=here.y+here.h/2;
    for(i=0;i<a->focus_count;i++) {
        KRect r=a->focus[i].bounds;
        int x=r.x+r.w/2-hx,y=r.y+r.h/2-hy;
        if(i==a->focused || r.w<=0 || r.h<=0 || (dx && x*dx<=0) || (dy && y*dy<=0)) continue;   /* (a node not drawn: no place to go) */
        int primary=dx?abs(x):abs(y), secondary=dx?abs(y):abs(x);
        int inline_=dx?overlaps(here.y,here.y+here.h,r.y,r.y+r.h):overlaps(here.x,here.x+here.w,r.x,r.x+r.w);
        int s=inline_?primary*1000+secondary:1000000+primary*primary+secondary*secondary*4;
        if(s<score) {score=s;best=i;}
    }
    if(best>=0)a->focused=best;
}
/* a big screen's mixer (the strips side by side, only the track in hand's controls targets): left and right go on across the strips,
 * B1 B2 then the next track's B1 B2, a pan or a fader to the next track's; 1 when it moved (0: the move is the usual one) */
static int mixer_across(KApp *a,int d) {
    int idx,to,t,i;
    if(a->page!=K_PAGE_MIXER||!(a->lay&K_LAY_SYNTH_ALL)||!a->focus_count||a->focus[a->focused].kind!=K_NODE_PARAM)return 0;
    idx=a->focus[a->focused].index;
    if(idx<0||idx>3)return 0;
    if(idx==2&&d>0){to=3;t=a->track;}          /* B1 -> B2 */
    else if(idx==3&&d<0){to=2;t=a->track;}     /* B2 -> B1 */
    else{to=idx==2?3:idx==3?2:idx;t=a->track+d;}   /* (past the strip's edge: the next one's, from its near side) */
    if(t<0||t>=K_TRACKS)return 1;               /* (the first, the last strip: it stays) */
    if(t!=a->track){a->track=t;seq_follow(a);}
    for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_PARAM&&a->focus[i].index==to){a->focused=i;break;}
    return 1;
}
static void open_menu(KApp *a,KPageGroup group,int by_hold) {
    if((unsigned)group>=K_GROUP_COUNT)return;
    a->menu_open=1;a->menu_is_hold=by_hold;a->menu_changed=0;a->menu_group=group;a->menu_selected=0;
    int i;
    for(i=0;i<group_sizes[group];i++)
        if(group_page(a,group,i)==a->page){a->menu_selected=i;break;}
    /* In paired mode ENV1 and ENV2 map to one displayed page. The current
     * window tells us which half was active, both in ENV's group menu and
     * the global PAGES menu (which also contains both entries). */
    if ((a->lay&K_LAY_ENV_PAIR) && a->page==K_PAGE_ENV1 &&
        a->win>=8 && !drum_kit(a)) {
        for (i=0;i<k_menu_count(a);i++)
            if (group_page(a,group,i)==K_PAGE_ENV2) {a->menu_selected=i;break;}
    }
}
/* the page switcher's Config (the settings): one more place for the cursor, after the pages (a keyboard could not
 * reach it) */
static int menu_config(const KApp *a) { return a->menu_group==K_GROUP_ALL&&K_SETTINGS>0; }
static void menu_step(KApp *a,int delta) {
    a->menu_selected=wrap(a->menu_selected+delta,k_menu_count(a)+menu_config(a));
    a->menu_changed=1;
}
static void menu_row(KApp *a,int d) {   /* the tiles: a row up or down (none past the first or the last: it stays); up from the first row: Config */
    const int n=a->menu_selected+d*a->menu_cols,cfg=k_menu_count(a);
    if(menu_config(a)&&a->menu_selected==cfg){if(d>0){a->menu_selected=0;a->menu_changed=1;}return;}
    if(n>=0&&n<cfg){a->menu_selected=n;a->menu_changed=1;}
    else if(n<0&&menu_config(a)){a->menu_selected=cfg;a->menu_changed=1;}
}
static void activate(KApp *a) {
    const KFocusNode *n;
    int t=a->track;
    if(!a->focus_count)return;
    n=&a->focus[a->focused];
    switch(n->kind){
        case K_NODE_TRACK:a->track=n->index;break;
        case K_NODE_STEP:
            if(a->page==K_PAGE_DRUM){a->cur_lane[t]=n->index/K_STEPS;k_lane_step_toggle(a,t,a->cur_lane[t],n->index%K_STEPS);}
            else step_toggle(a,t,n->index);   /* a step is taken as it is switched */
            break;
        case K_NODE_SCENE:{int tr=n->index/K_SCENES,scene=n->index%K_SCENES;
            k_song_cell_step(a,scene,tr,1,1);break;}   /* (pressed: the next pattern, round to none) */
        case K_NODE_SOUND:engine_step(a,1,a->clock_ms);break;   /* the engine's bar: the next engine */
        case K_NODE_PARAM:break;
    }
}
/* a turn of a parameter: a percentage, but an insert's effect steps one way or the other */
static void add_param_raw(KApp *a,KPage page,int i,int delta);
/* the value a turn stops at on its way across (a bipolar's 0, a pan's centre, a fader's 0 dB), or K_NO_CENTER: none. A turn that would
 * pass over it lands on it (the FM-1's knobs step 2 a detent: from an odd value 0 would be stepped over), the next goes on. */
static int param_center(const KApp *a,KPage page,int i) {
    if(k_page_unused(a,page))return K_NO_CENTER;
    { KPage pg; int j; if(k_synth_all_sub(a,page,i,&pg,&j))return param_center(a,pg,j); }   /* (the Switch's SOUND: its ENV and LFO items) */
    return k_ref_center(a,k_param_ref(a,page,i));
}
/* a turn marks the preset changed where the value is part of a sound (the engine, ENV, ENV DEST, LFO, FX, HOME's macros) */
static int preset_part(const KApp *a,KPage page,int i) { KParamProps p; k_param_props(a,k_param_ref(a,page,i),&p); return (p.flags&K_PF_PRESET)!=0; }
static void add_param(KApp *a,KPage page,int i,int delta) {
    int old,c;
    KVal *p;
    { KPage pg; int j; if (k_synth_all_sub(a, page, i, &pg, &j)) { page = pg; i = j; } }   /* (the Switch's SOUND: its ENV and LFO items) */
    p=k_param(a,page,i);
    old=*p;
    add_param_raw(a,page,i,delta);
    c=param_center(a,page,i);
    if(c!=K_NO_CENTER&&((old<c&&*p>c)||(old>c&&*p<c)))*p=c;   /* across the centre: it stops there */
    if(*k_param(a,page,i)!=old && preset_part(a,page,i))a->preset_dirty[a->track]=1;
}
static void add_param_raw(KApp *a,KPage page,int i,int delta);
/* a MIDI control change (any channel): CC 7 the level of the track in hand, CC 10 its pan, CC 16..19 its four macros (HOME's first
 * row), set where the controller is (0..127 over the parameter's range); the rest are not heard */
void k_cc_in(KApp *a,int cc,int v) {
    KPage page;int i,lo,hi,old;
    KVal *p;
    v=clamp(v,0,127);
    if(cc==7||cc==10){page=K_PAGE_MIXER;i=cc==7?0:1;}
    else if(cc>=16&&cc<=19){page=K_PAGE_HOME;i=cc-16;}
    else return;
    { KParamProps pr; if(!k_param_props(a,k_param_ref(a,page,i),&pr))return; lo=pr.lo;hi=pr.hi; }   /* (no plugin: its macros are not turned) */
    p=k_param(a,page,i);old=*p;
    add_param_raw(a,page,i,lo+((hi-lo)*v+63)/127-old);
    if(*p!=old&&preset_part(a,page,i))a->preset_dirty[a->track]=1;
}
void k_knob_in(KApp *a,int slot,int v) {   /* (as k_cc_in, the page in hand's slot-th knob; the knob shown in use) */
    const int i=a->win+slot;
    int lo,hi,old;
    KVal *p;
    if(slot<0||slot>=(k_params_visible(a)?k_params_visible(a):K_DEFAULT_SHOWN)||i>=k_param_count(a->page)||k_page_unused(a,a->page))return;   /* (a device with
                                                 * no knobs of its own, the Zero: the window's first four) */
    { KParamProps pr; if(!k_param_props(a,k_param_ref(a,a->page,i),&pr)||pr.hi<=pr.lo)return; lo=pr.lo;hi=pr.hi; }
    v=clamp(v,0,127);
    p=k_param(a,a->page,i);old=*p;
    add_param_raw(a,a->page,i,lo+((hi-lo)*v+63)/127-old);
    if(*p!=old&&preset_part(a,a->page,i))a->preset_dirty[a->track]=1;
    if(i<64){a->touched|=(uint64_t)1<<i;a->touch_ms[i]=a->clock_ms;}
}
void k_knob_turn(KApp *a,int slot,int d) {   /* an endless encoder's turn */
    const int i=a->win+slot;
    if(!d||slot<0||slot>=(k_params_visible(a)?k_params_visible(a):K_DEFAULT_SHOWN)||i>=k_param_count(a->page))return;
    add_param(a,a->page,i,d);
    if(i<64){a->touched|=(uint64_t)1<<i;a->touch_ms[i]=a->clock_ms;}
}
void k_level_in(KApp *a,int t,int v) {
    const int t0=a->track;
    if(t>=K_TRACKS)return;
    if(t>=0)a->track=t;
    k_cc_in(a,7,v);
    a->track=t0;
}
static void add_param_raw(KApp *a,KPage page,int i,int delta) {
    if(k_page_unused(a,page))return;   /* (ORGAN's ENV and LFO: none) */
    k_ref_turn(a,k_param_ref(a,page,i),delta);   /* within its range in the table, one at a time where there are few values */
}
static void value_delta(KApp *a,int delta,uint32_t ms) {
    KFocusNode n=a->focus[a->focused];
    int t=a->track;
    if(n.kind==K_NODE_PARAM){
        add_param(a,a->page,n.index,delta);(void)t;
        a->touched|=(uint64_t)1<<n.index;a->touch_ms[n.index]=ms;
    } else if(n.kind==K_NODE_SCENE){
        int tr=n.index/K_SCENES,s=n.index%K_SCENES;
        k_song_cell_step(a,s,tr,delta,0);
    } else if(n.kind==K_NODE_SOUND){engine_step(a,delta,ms);}   /* the engine's bar (a D-pad, a touch): the engine */
    else if(n.kind==K_NODE_TRACK&&K_IS_HOME(a->page)&&n.index>=0&&n.index<K_TRACKS){   /* a track's tile (the Zero's K L): its level, as
                                                 * the mixer's fader (TODO 2026-11-12: they did nothing there) */
        a->track=n.index;add_param(a,K_PAGE_MIXER,0,delta);a->track=t;
    }
}
void k_tick(KApp *a,uint32_t time_ms) {
    a->clock_ms=time_ms;
    if(a->page==K_PAGE_SONG&&a->song_play&&a->playing&&a->song_scene>=0&&
       (a->song_scene<a->song_off||a->song_scene>=a->song_off+K_SONG_VIS)){   /* SONG playing past its window: the window follows it */
        const int col=a->focused<a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE?a->focused%K_SONG_VIS:0,tr=a->focused<a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE?a->focused/K_SONG_VIS:a->track;
        a->song_off=(uint8_t)clamp(a->song_scene,0,K_SCENES-K_SONG_VIS);layout(a);a->focused=tr*K_SONG_VIS+col;
    }
    if(a->boom){   /* Dynamite's fuse: lit at this tick, off K_BOOM_MS later (on its track, whichever is in hand by then) */
        if(!a->boom_ms)a->boom_ms=time_ms?time_ms:1;
        else if(time_ms-a->boom_ms>=K_BOOM_MS){
            const int t=a->track;
            a->boom=0;a->track=a->boom_track;
            k_seq_tool(a,(KTool)a->boom_tool,a->boom_x,a->boom_y);
            a->track=t;
        }
    }
#if K_WITH_CONNECT
    if(a->learn==1||a->learn==2)k_balloon_show(&a->balloon,K_BALLOON_LEARN,time_ms);   /* (waiting: it stays up) */
#endif
    k_clock_poll(a,time_ms);
    if(a->playing&&a->count_in&&(int32_t)(time_ms-a->play_ms)<0){   /* a bar counted in: no step yet, the beats counted down */
        const uint32_t left=a->play_ms-time_ms;
        const int b=3-(int)(k_umul(left,(uint32_t)a->bpm)/60000u);
        a->step=-1;a->beat=b<0?0:b;
    } else if(a->playing) {
        uint64_t idx;
        a->count_in=0;
        idx=k_umul((uint32_t)(time_ms-a->play_ms),(uint32_t)a->bpm)/15000u;   /* (k_umul: the FM-1's erratum, a->bpm a 16-bit load) */   /* a 16th is 15000 / bpm ms */
        if(a->clock_src>0) idx=a->clk.pulses?(a->clk.pulses-1u)/6u:0u;   /* an external clock: 6 clocks a 16th, the first clock after Start is step 0 */
        a->beat=(int)((idx/4u)%4u);
        a->step=(int)(idx%16u);
        if((int)idx!=a->last_idx) {   /* a new 16th: the tracks with a step on it sound (here only the meter knows) */
            int t;
            a->last_idx=(int)idx;
            for(t=0;t<K_TRACKS;t++)
                if(k_step_on(a,t,a->step)) {
                    a->hit_ms[t]=time_ms;a->hit_v[t]=(uint8_t)(a->params[t][K_PAGE_MIXER][0]*9/10);   /* as hard as its fader is up */
                }
        }
    }
    if(a->set_pending && !a->settings_open && !a->playing && (uint32_t)(time_ms-a->set_try_ms)>=2000u){   /* (a flash erase: not while playing) */
        a->set_try_ms=time_ms;
        if(k_settings_save(a)==0)a->set_pending=0;   /* (a failed write is tried again, every 2 s) */
    }
    if(a->home_held && !a->home_fired && (uint32_t)(time_ms-a->home_since)>=500){   /* HOME held: the settings (their release does nothing) */
        a->home_fired=2;   /* (2: the hold opened them; SELECT or OCT turned while HOME is still held closes them again) */
        if(K_SETTINGS>0){a->settings_open=1;a->settings_sel=k_setting_first(a);a->menu_open=0;a->save_open=0;a->save_ask=0;a->set_ask=0;}
    }
    /* (a page key held alone does nothing: held + SELECT opens its tools, k_dispatch) */
}
/* a key of the bed. A drum track, on every page: a black key picks its lane and plays it, a white key switches a step of that lane. STEP: a white key switches a step. ROLL, after Felucca's step
 * entry: with the track armed (REC) and stopped, the keys pressed together write the step in hand and the cursor moves on when
 * they are all up; playing and armed, a key writes the step the playhead is on (live recording). Not armed, a key does nothing yet. */
/* a bar still counted in before recording (MET COUNT): the keys sound, nothing is written (the playhead, once the audio's, ends it) */
static int counting_in(const KApp *a){return a->playing&&a->count_in&&a->step<0;}
static void note_push(KApp *a,int t,int note,int vel){
    uint8_t w=(uint8_t)((a->note_w+1)&(K_NOTE_Q-1));
    if(w==a->note_r){   /* full: a note-on is dropped, a note-off never. It cancels its own note-on still queued (that note then not
                         * heard at all), else takes the newest note-on's place (that one not heard); only a queue of offs alone loses it */
        int i,on=-1;
        if(vel>0)return;
        for(i=(a->note_w-1)&(K_NOTE_Q-1);;i=(i-1)&(K_NOTE_Q-1)){
            if(a->note_q[i].vel>0){if(on<0)on=i;if(a->note_q[i].track==t&&a->note_q[i].note==note){a->note_q[i].vel=0;return;}}
            if(i==a->note_r)break;
        }
        if(on>=0)a->note_q[on]=(KNoteEv){(uint8_t)t,(uint8_t)note,0};
        return;
    }
    a->note_q[a->note_w]=(KNoteEv){(uint8_t)t,(uint8_t)note,(uint8_t)vel};
    a->note_w=w;
}
/* k_init, the notes still sounding let go: a key or a MIDI note held through a song's load or CLEAR ALL rang on, its release
 * lost with the reset. Their note-offs (and what the platform had not taken yet) are queued again after it. */
void k_init_released(KApp *a){
    KNoteEv keep[K_NOTE_Q];
    int n=0,i;
    for(i=a->note_r;i!=a->note_w&&n<K_NOTE_Q-1;i=(i+1)&(K_NOTE_Q-1))if(a->note_q[i].vel==0)keep[n++]=a->note_q[i];
    for(i=0;i<27&&n<K_NOTE_Q-1;i++)if(a->keys_down>>i&1)keep[n++]=(KNoteEv){a->key_track[i],a->key_note[i],0};
    for(i=0;i<128&&n<K_NOTE_Q-1;i++)if(a->midi_on[i])keep[n++]=(KNoteEv){(uint8_t)(a->midi_on[i]-1),a->midi_lane[i],0};
    k_init(a);
    for(i=0;i<n;i++)note_push(a,keep[i].track,keep[i].note,0);
}
void k_preview(KApp *a,int note,int on){if(note>=0&&note<128&&!k_is_drum(a,a->track))note_push(a,a->track,note,on?100:0);}
int k_take_note(KApp *a,KNoteEv *out){
    if(a->note_r==a->note_w)return 0;
    *out=a->note_q[a->note_r];
    a->note_r=(uint8_t)((a->note_r+1)&(K_NOTE_Q-1));
    return 1;
}
/* armed (REC), a synth track: a note played is written. Playing: at the step the playhead is on, beside what is there (live, a
 * chord as it is played). Stopped, after Felucca's step entry: at the step in hand, the notes held together a chord there (the
 * first of them clears it), the length the last one had; all of them up, the cursor moves on (key_event, k_note_in) */
static int midi_held(const KApp *a){int i;for(i=0;i<128;i++)if(a->midi_on[i])return 1;return 0;}
static void rec_note(KApp *a,int t,int note,int vel){
    const int p=a->pattern[t];
    if(a->playing&&a->step>=0){
        const int s=a->step%K_STEPS;int i;
        a->rsel[t]=k_roll_add(a,t,p,s,note,1,vel);a->cur[t]=s;
        for(i=0;i<8&&a->rec_held[i].on;i++){}
        if(i<8&&a->rsel[t]>=0){a->rec_held[i].t=(uint8_t)t;a->rec_held[i].at=(uint8_t)(p<<4|s);a->rec_held[i].note=(uint8_t)note;a->rec_held[i].on=1;}   /* (a step long until let go) */
    }
    else{
        const int s=a->cur[t];
        if(!a->entry_open)k_roll_clear(a,t,p,s);
        a->rsel[t]=k_roll_add(a,t,p,s,note,a->rlen[t],vel);
        a->entry_open=1;
    }
    roll_pick(a,t);
}
/* live REC: a held note let go, its length the steps it was held (1 at least, to the pattern's end at most); */
static void rec_release(KApp *a,int t,int note){
    int i,k;
    for(i=0;i<8;i++)if(a->rec_held[i].on&&a->rec_held[i].t==t&&a->rec_held[i].note==note){
        const int s0=a->rec_held[i].at&15,now=a->playing&&a->step>=0?a->step%K_STEPS:s0;
        int len=(now-s0+K_STEPS)%K_STEPS;
        a->rec_held[i].on=0;
        if(len<1)len=1;
        if(len>K_STEPS-s0)len=K_STEPS-s0;
        for(k=0;k<K_ROLL_NOTES;k++)if(a->roll[t][k].len&&a->roll[t][k].at==a->rec_held[i].at&&a->roll[t][k].note==note){a->roll[t][k].len=(uint8_t)len;if(a->rsel[t]==k)a->rlen[t]=len;break;}   /* (the note in hand: ROLL's LEN knob too, or it puts it back) */
    }
}
static void entry_done(KApp *a,int t){   /* stopped, armed: every key and MIDI note of the chord up, on to the next step */
    if(a->entry_open&&!a->keys_down&&!midi_held(a)){a->cur[t]=wrap(a->cur[t]+1,K_STEPS);a->entry_open=0;roll_pick(a,t);}
}
static uint8_t lane_hit_t[K_LANES];   /* (the track each struck lane sounds on, + 1: its release goes there) */
void k_lane_hit(KApp *a,int lane,int vel){
    const int t=a->track;
    if(lane<0||lane>=K_LANES)return;
    if(vel<=0){if(lane_hit_t[lane]){note_push(a,lane_hit_t[lane]-1,lane,0);lane_hit_t[lane]=0;}return;}
    if(!k_is_drum(a,t))return;
    if(lane_hit_t[lane])note_push(a,lane_hit_t[lane]-1,lane,0);
    note_push(a,t,lane,clamp(vel,1,127));lane_hit_t[lane]=(uint8_t)(t+1);
    a->cur_lane[t]=lane;
    if(a->recording&&!counting_in(a))k_lane_step_set(a,t,lane,a->playing&&a->step>=0?a->step:a->cur[t],1);
}
void k_mod_in(KApp *a,int v127){
    const KParamRef r=k_param_ref(a,K_PAGE_HOME,0);
    KParamProps p;
    if(!k_param_props(a,r,&p))return;
    v127=clamp(v127,0,127);
    k_ref_set(a,r,p.lo+((int)p.hi-p.lo)*v127/127);
}
void k_bend_in(KApp *a,int v14){a->bend[a->track]=(int16_t)(clamp(v14,0,16383)-8192);}
void k_note_in(KApp *a,int note,int vel){
    int t=a->track;
    if(note<0||note>127)return;
    if(vel<=0){   /* a release: to the track (and the lane) its note went to, whatever track is in hand now */
        if(a->midi_on[note]){const int t0=a->midi_on[note]-1;note_push(a,t0,a->midi_lane[note],0);a->midi_on[note]=0;if(!k_is_drum(a,t0)){rec_release(a,t0,note);entry_done(a,t0);}}
        return;
    }
    if(k_is_drum(a,t)){   /* a drum track: the GM drum notes hit the lanes */
        static const int8_t GM[12]={0,-1,1,4,1,5,2,5,2,6,3,7};   /* 36 BD .. 47: BD - SD CP SD LT CH LT CH MT OH MT */
        int l=note>=36&&note<48?GM[note-36]:note==48||note==50?7:note==49||note==57?9:note==51||note==59?8:-1;
        if(l>=K_LANES)l=-1;   /* (the cymbals: no lane of the eight) */
        if(l>=0){if(a->midi_on[note])note_push(a,a->midi_on[note]-1,a->midi_lane[note],0);   /* (that note held before, maybe on a synth track: let go first) */
            note_push(a,t,l,clamp(vel,1,127));a->midi_on[note]=(uint8_t)(t+1);a->midi_lane[note]=(uint8_t)l;
            if(a->recording&&!counting_in(a))k_lane_step_set(a,t,l,a->playing&&a->step>=0?a->step:a->cur[t],1);}
        return;
    }
    if(a->midi_on[note])note_push(a,a->midi_on[note]-1,a->midi_lane[note],0);   /* (the same note again: the one before let go first) */
    note_push(a,t,note,clamp(vel,1,127));a->midi_on[note]=(uint8_t)(t+1);a->midi_lane[note]=(uint8_t)note;
    if(a->recording&&!counting_in(a))rec_note(a,t,note,clamp(vel,1,127));
}
static void key_event(KApp *a,int id,int down,uint32_t ms) {
    int t=a->track;
    (void)ms;
    if(id<0||id>26)return;
    if(!down && !(a->keys_down>>id&1))return;   /* not a sounding key (it switched a step) */
    if(!down){   /* a release: to the track the key played, whatever track is in hand now (a synth key let go on a drum track rang on) */
        const int t0=a->key_track[id];
        a->keys_down&=~(1u<<id);
        note_push(a,t0,a->key_note[id],0);
        k_io_note_out(t0,k_is_drum(a,t0)?k_audio_gm_drum[a->key_note[id]&7]:a->key_note[id],0);   /* (MIDI out: the key's note off) */
        if(k_is_drum(a,t0))return;
        rec_release(a,t0,a->key_note[id]);
        entry_done(a,t0);   /* all up: on to the next step */
        layout(a);
        {int i;for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_STEP&&a->focus[i].index==a->cur[a->track]){a->focused=i;break;}}
        return;
    }
    a->key_track[id]=(uint8_t)t;
    if(k_is_drum(a,t)){   /* a drum track, on every page (the FM-1's bed): a black key picks its lane and plays it, a white key is a step of the lane in hand */
        if(id<K_STEPS){
            int i;
            k_lane_step_toggle(a,t,a->cur_lane[t],id);
            for(i=0;i<a->focus_count;i++)
                if(a->focus[i].kind==K_NODE_STEP && a->focus[i].index==a->cur_lane[t]*K_STEPS+id){a->focused=i;break;}
            return;
        }
        if(id-K_STEPS>=K_LANES)return;   /* (the last three black keys: no lane) */
        select_lane(a,id-K_STEPS);
        a->keys_down|=1u<<id;a->key_note[id]=(uint8_t)(id-K_STEPS);
        note_push(a,t,id-K_STEPS,100);
        k_io_note_out(t,k_audio_gm_drum[id-K_STEPS],100);   /* (MIDI out: the lane's General MIDI drum) */
        if(a->recording&&!counting_in(a))k_lane_step_set(a,t,id-K_STEPS,a->playing&&a->step>=0?a->step:a->cur[t],1);
        return;
    }
    if(down && (a->page==K_PAGE_STEP||a->page==K_PAGE_DRUM)){   /* the step pages: the keys are the steps */
        if(id<K_STEPS)k_dispatch(a,(KAction){K_ACT_STEP_TOGGLE,id,0,ms});
        else if(a->page==K_PAGE_DRUM)k_dispatch(a,(KAction){K_ACT_LANE_SELECT,id-16,0,ms});
        return;
    }
    if(down){
        a->keys_down|=1u<<id;
        a->key_note[id]=(uint8_t)k_key_pitch(a,id);
        note_push(a,t,a->key_note[id],100);
        if(!a->params[t][K_PAGE_ARP][0])k_io_note_out(t,a->key_note[id],100);   /* MIDI out: the bed's notes on the track's channel
                                                 * an arp's track: the notes it plays go out (the audio's), not the key's
                                                 * (its off still goes: an extra off is harmless) */
        if(a->recording&&!counting_in(a))rec_note(a,t,a->key_note[id],100);   /* (not while a bar is counted in) */
    }
    layout(a);
    {int i;for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_STEP&&a->focus[i].index==a->cur[t]){a->focused=i;break;}}
}
/* the engine of the track in hand, a step (ALGORITHM; PRESETS did it before there were presets): a new one starts at its defaults */
/* track t onto engine e, as ALGORITHM turns it (what the old one had kept, the new one as the track last had it), the SEQ page
 * following when it is the track in hand (PRESETS: a preset of another engine) */
void k_track_engine(KApp *a,int t,int e) {
    const int old=a->sound[t];
    if(t<0||t>=K_TRACKS||e<0||e>=k_engine_count()||e==old)return;
    a->sound[t]=e;engine_switch(a,t,old);
    if(t==a->track)seq_follow(a);
}
static void engine_step(KApp *a,int d,uint32_t ms) {
    int old=a->sound[a->track];
    a->sound[a->track]=clamp(old+(d>0)-(d<0),0,k_engine_count()-1);
    if(a->sound[a->track]!=old){engine_switch(a,a->track,old);seq_follow(a);}
    k_balloon_show(&a->balloon,K_BALLOON_PRESET,ms);
}
static void dispatch(KApp *a,KAction e);
/* the tools' list over STEP, DRUM and ROLL (kreisel_seqtool.c): SELECT (or up / down) the row, a knob its
 * values; a tool with values is tried on the track as they turn (heard and seen at once), OCT+ / SAVE / ACCEPT keeps it, OCT- / BACK
 * lets it go.
 * One without values does its work on OCT+ / SAVE / ACCEPT. Either closes the list after. The keys, PLAY and the level still work
 * under it; anything else does nothing there. */
static int tools_page(const KApp *a){return k_tools_page(a);}
int k_asks(const KApp *a){
    if(a->save_ask)return K_ASK_SAVE;
    if(a->tools_open||a->name_open||(a->save_open&&a->save_act))return K_ASK_OCT;
    return K_ASK_NONE;
}
static void tools_let_go(KApp *a){if(a->tools_try>=0)k_seq_tool_revert(a);a->tools_try=-1;}
static void tools_close(KApp *a,int keep){if(!keep)tools_let_go(a);a->tools_open=0;a->tools_try=-1;a->tools_stage=0;}
static void tools_try_it(KApp *a){
    const KTool t=k_tools_item(a,a->tools_sel);
    if(a->tools_try==a->tools_sel)k_seq_tool_revert(a);
    else tools_let_go(a);
    k_seq_tool(a,t,a->tool_x[t],a->tool_y[t]);
    a->tools_try=(int8_t)a->tools_sel;
}
static void tools_turn(KApp *a,int which,int d){
    const KTool t=k_tools_item(a,a->tools_sel);
    if(which&&k_tool_vals(t,k_is_drum(a,a->track))<2)which=0;   /* (one value: every knob turns it) */
    if(k_tool_turn(a,t,which,d))tools_try_it(a);
}
static void tools_do(KApp *a){   /* SAVE / ACCEPT: the row's tool kept (tried already) or done now */
    const KTool t=k_tools_item(a,a->tools_sel);
    if(t==K_TOOL_CLEAR||t==K_TOOL_SC_CLEAR){   /* Dynamite: lit (the pattern as it was meanwhile); it goes off in k_tick */
        tools_let_go(a);
        a->boom=1;a->boom_ms=0;a->boom_tool=(uint8_t)t;a->boom_track=(int8_t)a->track;a->boom_x=a->tool_x[t];a->boom_y=a->tool_y[t];
        tools_close(a,1);
        return;
    }
    if(a->tools_try!=a->tools_sel){tools_let_go(a);k_seq_tool(a,t,a->tool_x[t],a->tool_y[t]);}
    tools_close(a,1);
}
void k_tools_value(KApp *a,int which,int v){   /* (a desktop's slider) */
    const KTool t=k_tools_item(a,a->tools_sel);
    if(!a->tools_open)return;
    if(k_tool_set(a,t,which,v))tools_try_it(a);
}
static int tools_dispatch(KApp *a,KAction e);
/* a page key held + SELECT: that key's page (the one in hand if it is of the family, else SEQ's view or the
 * family's first) and its tools' list; its release then leaves the list open, nothing done: K1..K4 try, OCT+ does, OCT- lets go */
static void held_tools(KApp *a,int g,uint32_t ms){
    const KPage q=group_has(a,g,a->page)?a->page:g==K_GROUP_GLO&&(a->page==K_PAGE_MIXER||a->page==K_PAGE_HOME)?K_PAGE_MIXER:group_page(a,g,0);   /* (the mixer:
                                                 * HOME's, whose held SELECT turns the track: GLO held + SELECT there opens its tools) */
    if(q!=a->page)k_open_page(a,q);
    if(k_tools_page(a)){KAction t={K_ACT_TOOLS,0,0,ms};tools_dispatch(a,t);a->group_held=g;}   /* (still held: its release is a modifier's) */
}
static int tools_dispatch(KApp *a,KAction e){
    const int n=k_tools_count(a);
    if(e.type==K_ACT_TOOLS){
        if(a->tools_open){tools_close(a,0);return 1;}
        if(!tools_page(a)||a->menu_open||a->settings_open||a->save_open||a->save_ask||a->name_open)return 1;
        a->tools_open=1;a->tools_try=-1;a->tools_stage=0;a->tools_sel=(int8_t)clamp(a->tools_sel,0,n-1);a->group_held=-1;
        return 1;
    }
    if(!a->tools_open)return 0;
    if(!tools_page(a)){tools_close(a,1);return 0;}
    if(a->lay&K_LAY_SHEET_CURSOR){   /* a cursor's sheet: the names, OK opens the tool's values; there up /
                                     * down a value, left / right (K L) turn it, OK does it, BACK returns to the names */
        const KTool t=k_tools_item(a,a->tools_sel);
        const int nv=k_tool_vals(t,k_is_drum(a,a->track));
        if(!a->tools_stage)switch(e.type){
            case K_ACT_FOCUS_NEXT:case K_ACT_DOWN:tools_let_go(a);a->tools_sel=(int8_t)wrap(a->tools_sel+1,n);return 1;
            case K_ACT_FOCUS_PREV:case K_ACT_UP:tools_let_go(a);a->tools_sel=(int8_t)wrap(a->tools_sel-1,n);return 1;
            case K_ACT_ACCEPT:case K_ACT_SAVE:
                if(nv){a->tools_stage=1;a->tools_which=0;tools_try_it(a);}else tools_do(a);   /* (its values tried at once: seen on the pattern) */
                return 1;
            case K_ACT_LEFT:case K_ACT_RIGHT:case K_ACT_VALUE_DELTA:case K_ACT_TYPE_DELTA:case K_ACT_PARAM_DELTA:return 1;   /* (no values here) */
            case K_ACT_BACK:tools_close(a,0);return 1;
            default:break;
        }else switch(e.type){
            case K_ACT_FOCUS_NEXT:case K_ACT_DOWN:a->tools_which=(uint8_t)clamp(a->tools_which+1,0,nv-1);return 1;
            case K_ACT_FOCUS_PREV:case K_ACT_UP:a->tools_which=(uint8_t)clamp(a->tools_which-1,0,nv-1);return 1;
            case K_ACT_LEFT:case K_ACT_RIGHT:tools_turn(a,a->tools_which,e.type==K_ACT_RIGHT?1:-1);return 1;
            case K_ACT_VALUE_DELTA:tools_turn(a,a->tools_which,e.a);return 1;
            case K_ACT_ACCEPT:case K_ACT_SAVE:tools_do(a);return 1;
            case K_ACT_BACK:tools_let_go(a);a->tools_stage=0;return 1;   /* (the names again, the pattern as it was) */
            default:break;
        }
    }
    switch(e.type){
        case K_ACT_OCTAVE_DELTA:if(e.a>0)tools_do(a);else if(e.a<0)tools_close(a,0);break;   /* OCT+ does it, OCT- lets it go: as on SAVE and NAME
                                                 * (one pair of keys for yes and no everywhere) */
        case K_ACT_KEY:case K_ACT_PLAY:case K_ACT_MASTER_DELTA:case K_ACT_HOME_DOWN:case K_ACT_HOME_UP:
        case K_ACT_PAGE_GROUP_DOWN:case K_ACT_PAGE_GROUP_UP:return 0;   /* (a page button: another page closes it, SEQ held again too) */
        case K_ACT_FOCUS_NEXT:case K_ACT_DOWN:tools_let_go(a);a->tools_sel=(int8_t)wrap(a->tools_sel+1,n);break;
        case K_ACT_FOCUS_PREV:case K_ACT_UP:tools_let_go(a);a->tools_sel=(int8_t)wrap(a->tools_sel-1,n);break;
        case K_ACT_PARAM_DELTA:tools_turn(a,e.a==1,(e.b+(e.b>0)-(e.b<0))/2);break;   /* K2 the second value, the others the first (a detent 2) */
        case K_ACT_VALUE_DELTA:tools_turn(a,0,e.a);break;
        case K_ACT_TYPE_DELTA:tools_turn(a,1,e.a);break;   /* ALGORITHM: the second */
        case K_ACT_LEFT:case K_ACT_RIGHT:tools_turn(a,1,e.type==K_ACT_RIGHT?1:-1);break;
        case K_ACT_ACCEPT:case K_ACT_SAVE:tools_do(a);break;
        case K_ACT_POINTER:if(e.b>=0&&e.b<n){if(e.b==a->tools_sel)tools_do(a);else{tools_let_go(a);a->tools_sel=(int8_t)e.b;}}else if(e.b<0&&e.a==-1)tools_close(a,0);break;
        case K_ACT_BACK:tools_close(a,0);break;
        default:break;
    }
    return 1;
}
/* an action; then, arrived at a drum track (another track, or this one's engine now DRUM), the hint: its black keys pick the lane
 * (HELP ON; the UI shows it where keys are) */
void k_dispatch(KApp *a,KAction e) {
    const int t0=a->track,d0=k_is_drum(a,t0),c0=a->cur[t0],p0=a->pattern[t0];
    if(a->boom&&(e.type==K_ACT_TOOLS||e.type==K_ACT_ACCEPT||e.type==K_ACT_BACK||e.type==K_ACT_SAVE||(e.type==K_ACT_OCTAVE_DELTA&&e.a)||
                 (e.type==K_ACT_POINTER&&e.a==-1))){a->boom=0;return;}   /* Dynamite lit: pressed again, put out (nothing cleared) */
    a->act_n++;
    dispatch(a,e);
    if(a->page==K_PAGE_ROLL&&(a->lay&K_LAY_ROLL_CELLS)&&!a->rows_view){}   /* (the roll as cells: its cursor is a pitch too; nothing picked or carried) */
    else if(a->track!=t0||a->cur[a->track]!=c0||a->pattern[a->track]!=p0)roll_pick(a,a->track);   /* another step in hand: its note */
    else roll_commit(a,a->track);   /* (ROLL's knobs: the note in hand follows them) */
    if(k_is_drum(a,a->track)&&(a->track!=t0||!d0)&&!a->set_v[K_SET_HELP])k_balloon_show(&a->hint,K_BALLOON_HINT,e.time_ms);
}
static void dispatch(KApp *a,KAction e) {
    KPageGroup group;
    a->clock_ms=e.time_ms;
#if K_WITH_CONNECT
    if(a->learn&&k_learn_action(a,e))return;   /* MIDI learn: the control pointed at, not turned (kreisel_io.c) */
#endif
    if(e.type==K_ACT_UNDO){   /* (a menu, the settings, SAVE, a name or the tools up: nothing) */
        if(!a->menu_open&&!a->settings_open&&!a->save_open&&!a->save_ask&&!a->name_open&&!a->tools_open&&k_seq_tool(a,K_TOOL_UNDO,0,0))
            k_balloon_show(&a->balloon,K_BALLOON_UNDO,e.time_ms);
        return;
    }
    if(e.type==K_ACT_GRAB&&!(a->page==K_PAGE_ROLL&&(a->lay&K_LAY_ROLL_CELLS)&&!a->rows_view&&!k_is_drum(a,a->track)&&!a->menu_open&&!a->settings_open&&
                             !a->save_open&&!a->save_ask&&!a->tools_open&&!a->name_open)){   /* (not the cells' grip: OK as ever, on its press) */
        a->grab=0;
        if(!e.a)return;
        e.type=K_ACT_ACCEPT;e.a=0;
    }
    if(e.type==K_ACT_HOME_DOWN){a->home_held=1;a->home_fired=0;a->home_since=e.time_ms;return;}
    if(e.type==K_ACT_HOME_UP){
        int was=a->home_held&&!a->home_fired;
        a->home_held=0;a->home_fired=0;
        if(!was)return;   /* the hold opened the settings: the release does nothing */
        e.type=K_ACT_BACK;   /* a short press: HOME */
    }
    if(a->home_held && (e.type==K_ACT_REC||e.type==K_ACT_SOUND_DELTA||e.type==K_ACT_OCTAVE_DELTA)){   /* HOME held: REC undoes the last tool
                                                 * (again: redoes), PRESETS turns the octave, OCT does nothing (OCT+ and OCT- are yes and no only) */
        if(a->home_fired==2)a->settings_open=0;   /* (held a while first: the settings it opened give way) */
        a->home_fired=1;
        if(e.type==K_ACT_REC){if(k_seq_tool(a,K_TOOL_UNDO,0,0))k_balloon_show(&a->balloon,K_BALLOON_UNDO,e.time_ms);}
        else if(e.type==K_ACT_SOUND_DELTA&&e.a){a->octave=clamp(a->octave+(e.a>0?1:-1),-3,3);k_balloon_show(&a->balloon,K_BALLOON_OCTAVE,e.time_ms);}
        return;
    }
    if(e.type==K_ACT_SELECT_DELTA){   /* SELECT: with HOME held the track (and HOME's release does nothing), with a page key held its tools,
                                                 * else the cursor, a detent a step */
        int n;
        if(a->group_held>=0 && !a->home_held && !a->menu_open && !a->settings_open && !a->save_open && !a->save_ask && !a->name_open){
            a->group_combo=1;   /* (its release: no page turn) */
            if(!a->tools_open){held_tools(a,a->group_held,e.time_ms);return;}   /* (the first detent opens the list at the row last used) */
            for(n=e.a;n;n+=n>0?-1:1)k_dispatch(a,(KAction){n>0?K_ACT_FOCUS_NEXT:K_ACT_FOCUS_PREV,0,0,e.time_ms});
            return;
        }
        if(a->home_held){if(a->home_fired==2)a->settings_open=0;a->home_fired=1;e.type=K_ACT_TRACK_DELTA;}
        else if((a->page==K_PAGE_MIXER||a->page==K_PAGE_HOME) && !a->save_open && !a->menu_open && !a->settings_open && !a->save_ask){   /* the mixer: the strip; HOME: the
                                                 * track (its macros on the knobs): a cursor, stopping at the first and the last (no wrap) */
            e.a=clamp(a->track+e.a,0,K_TRACKS-1)-a->track;
            if(!e.a)return;
            e.type=K_ACT_TRACK_DELTA;
        }
        else{for(n=e.a;n;n+=n>0?-1:1)k_dispatch(a,(KAction){n>0?K_ACT_FOCUS_NEXT:K_ACT_FOCUS_PREV,0,0,e.time_ms});return;}
    }
    if(tools_dispatch(a,e))return;   /* the tools' list (a long press on a SEQ page) */
    if(k_save_quick(a,e))return;   /* SAVE on a synth page: overwrite the preset in hand? (kreisel_save.c) */
    if(!a->menu_open && !a->settings_open){   /* SAVE: the song slots and the user presets (kreisel_save.c); inside the settings
                                                 * SAVE is theirs (it turns the row, confirms CLEAR ALL) */
        if(a->save_open){if(k_save_dispatch(a,e))return;}
        else if(e.type==K_ACT_SAVE){k_save_open(a);return;}
    }
    if(e.type==K_ACT_OCTAVE_DELTA){   /* on a page OCT+ / OCT- turn the octave (OCT- on a synth page went to the mixer);
                                                 * where something asks (the settings, a menu, SAVE, a name, the tools) and on SONG (its cells) they
                                                 * are yes / do and no / back. HOME held + PRESETS turns the octave too */
        if(!e.a)return;
        if(!a->settings_open&&!a->menu_open&&!a->save_open&&!a->save_ask&&!a->name_open&&!a->tools_open&&a->page!=K_PAGE_SONG){
            a->octave=clamp(a->octave+(e.a>0?1:-1),-3,3);k_balloon_show(&a->balloon,K_BALLOON_OCTAVE,e.time_ms);return;}
        e.b=e.a<0;e.type=e.a>0?K_ACT_ACCEPT:K_ACT_BACK;e.a=0;   /* (e.b: OCT-, a back that does not turn HOME's pages) */
    }
    if(e.type==K_ACT_SETTINGS){if(K_SETTINGS==0)return;a->settings_open=!a->settings_open;a->settings_sel=k_setting_first(a);a->menu_open=0;a->set_ask=0;return;}
    if(a->settings_open && e.type!=K_ACT_KEY && e.type!=K_ACT_MASTER_DELTA && e.type!=K_ACT_PLAY){
        if(a->set_ask && !(a->settings_sel==K_SET_CLEAR && (e.type==K_ACT_PARAM_DELTA||e.type==K_ACT_VALUE_DELTA||e.type==K_ACT_ACCEPT||e.type==K_ACT_SAVE||(e.type==K_ACT_POINTER&&e.b==K_SET_CLEAR))))
            {a->set_ask=0;if(e.type!=K_ACT_BACK)return;}   /* CLEAR ALL armed: anything else says no (and does nothing more) */
        switch(e.type){
            case K_ACT_TYPE_DELTA:{int tb=wrap(k_setting_tab(a->settings_sel)+(e.a>0?1:-1),k_setting_tabs(a)),i;   /* ALGORITHM: the tab, at its first row */
                for(i=0;i<K_SETTINGS;i++)if(k_setting_visible(a,i)&&k_setting_tab(i)==tb){a->settings_sel=i;break;}}break;
            case K_ACT_LEFT:case K_ACT_RIGHT:
                if(a->lay&K_LAY_SHEET_CURSOR){setting_turn(a,a->settings_sel,e.type==K_ACT_RIGHT?1:-1);break;}   /* a cursor's sheet: left / right the value,
                                                 * as a tool's second box; the tabs: ALGORITHM's , . (L R) */
                a->settings_sel=setting_next(a,a->settings_sel,e.type==K_ACT_RIGHT?1:-1);break;
            case K_ACT_FOCUS_NEXT:case K_ACT_DOWN:a->settings_sel=setting_next(a,a->settings_sel,1);break;
            case K_ACT_FOCUS_PREV:case K_ACT_UP:a->settings_sel=setting_next(a,a->settings_sel,-1);break;
            case K_ACT_PARAM_DELTA:setting_turn(a,a->settings_sel,e.b);break;   /* any knob turns the row's value */
            case K_ACT_VALUE_DELTA:setting_turn(a,a->settings_sel,e.a);break;
            case K_ACT_ACCEPT:case K_ACT_SAVE:setting_turn_at(a,a->settings_sel,1,1);break;
            case K_ACT_POINTER:if(k_setting_visible(a,e.b)){if(e.b==a->settings_sel)setting_turn_at(a,e.b,1,1);else a->settings_sel=e.b;}break;
            case K_ACT_BACK:a->settings_open=0;break;
            default:break;
        }
        return;
    }
    if(e.type==K_ACT_PAGE_GROUP_DOWN){
        if((unsigned)e.a<K_GROUP_COUNT && a->group_held<0){a->group_held=e.a;a->held_since_ms=e.time_ms;a->group_combo=0;}
        return;
    }
    if(e.type==K_ACT_PAGE_GROUP_UP){
        if(a->group_held!=e.a)return;
        group=(KPageGroup)e.a;a->group_held=-1;
        if(a->group_combo){a->group_combo=0;return;}   /* GLO was a modifier: stay */
        if((uint32_t)(e.time_ms-a->held_since_ms)>=500u && !a->menu_open)return;   /* held a while and let go: nothing */
        if(a->menu_open && a->menu_is_hold){
            if(a->menu_changed)k_open_page(a,k_menu_page(a,a->menu_selected));
            else {a->menu_open=0;a->menu_is_hold=0;}
        }else if(group==K_GROUP_PATTERN&&(a->page==K_PAGE_ROLL||a->page==K_PAGE_STEP||a->page==K_PAGE_DRUM)){
            /* SEQ again on SEQ: ROLL, STEP, the tracker view, ROLL again (a drum track: DRUM, the tracker, DRUM). The tracker is the
             * last of the turn (a view, not a tool), so STEP is still a press away on a device with no page menu */
            if(a->rows_view){a->rows_view=0;k_open_page(a,group_page(a,group,0));}
            else if(a->page==K_PAGE_ROLL&&!k_is_drum(a,a->track))k_open_page(a,K_PAGE_STEP);
            else a->rows_view=1;
        }
        else if(!flip_page(a,group))k_open_page(a,group_page(a,group,0));
        return;
    }
    if(e.type==K_ACT_PAGE_MENU){
        /* A universal menu supersedes a page-key hold. Ignore its later release. */
        a->group_held=-1;
        if(a->menu_open){a->menu_open=0;a->menu_is_hold=0;}
        else open_menu(a,K_GROUP_ALL,0);
        return;
    }
    if(e.type==K_ACT_MASTER_DELTA){a->master=clamp(a->master+e.a,0,100);k_balloon_show(&a->balloon,K_BALLOON_MASTER,e.time_ms);return;}
    if(e.type==K_ACT_KEY){   /* the bed plays on every page but STEP and DRUM; GLO held + a black key: mute (1st four) / solo (next four) */
        if(e.b && a->group_held==K_GROUP_GLO && e.a>=16 && e.a<24){
            if(e.a<20)k_mute_toggle(a,e.a-16);else k_solo_toggle(a,e.a-20);
            a->group_combo=1;return;
        }
        if(e.b && (a->menu_open||a->group_held>=0))return;   /* no new note under a menu or a held page key; a release always goes through */
        if(a->rows_view&&k_is_drum(a,a->track)&&(a->page==K_PAGE_DRUM||a->page==K_PAGE_ROLL||a->page==K_PAGE_STEP)&&e.a>=0&&e.a<K_LANES){
            /* the tracker on a drum track (its keys were the lane's steps, not a row's): the first eight white keys
             * are the lanes, a press puts that lane's hit on the row in hand (again: takes it away), heard */
            if(e.b){const int t=a->track;k_lane_step_toggle(a,t,e.a,a->cur[t]&15);a->cur_lane[t]=e.a;note_push(a,t,e.a,100);note_push(a,t,e.a,0);}
            return;
        }
        key_event(a,e.a,e.b,e.time_ms);return;
    }
    if(a->menu_open){
        switch(e.type){
            case K_ACT_DOWN:case K_ACT_UP:if(a->menu_cols>1){menu_row(a,e.type==K_ACT_DOWN?1:-1);break;}menu_step(a,e.type==K_ACT_DOWN?1:-1);break;
            case K_ACT_FOCUS_NEXT:case K_ACT_RIGHT:menu_step(a,1);break;
            case K_ACT_FOCUS_PREV:case K_ACT_LEFT:menu_step(a,-1);break;
            case K_ACT_ACCEPT:case K_ACT_SAVE:
                if(menu_config(a)&&a->menu_selected==k_menu_count(a)){a->menu_open=0;a->settings_open=1;a->settings_sel=k_setting_first(a);a->set_ask=0;break;}   /* Config: the settings */
                k_open_page(a,k_menu_page(a,a->menu_selected));break;
            case K_ACT_BACK:
                /* HOME cancels both the menu and the pending held-key navigation. */
                if(a->menu_is_hold)a->group_held=-1;
                a->menu_open=0;a->menu_is_hold=0;break;
            case K_ACT_POINTER:{
                int index=e.b; /* UI supplies a selected menu row when a>=0. */
                if(index>=0 && index<k_menu_count(a))k_open_page(a,k_menu_page(a,index));
                break;
            }
            default:break;
        }
        return;
    }
    if(a->group_held>=0)return;   /* (a page key held: SELECT opens its tools, above; nothing else moves) */
    if(a->rows_view && (a->page==K_PAGE_ROLL||a->page==K_PAGE_DRUM||a->page==K_PAGE_STEP) &&
       (e.type==K_ACT_FOCUS_NEXT||e.type==K_ACT_FOCUS_PREV))   /* the tracker: SELECT the column (the track), as left / right; the rows K1's
                                                 * (SELECT and K1 both moved the row) */
        e.type=e.type==K_ACT_FOCUS_NEXT?K_ACT_RIGHT:K_ACT_LEFT;
    if(a->rows_view && (a->page==K_PAGE_ROLL||a->page==K_PAGE_DRUM||a->page==K_PAGE_STEP) &&
       (e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT||e.type==K_ACT_UP||e.type==K_ACT_DOWN||e.type==K_ACT_ACCEPT)){
        /* the tracker view: up / down the row (the step), left / right the column (the track, on the same row); OK
         * a note there (the note in hand's pitch, length, velocity) or the step emptied; a drum track: the lane in hand's step */
        const int t=a->track,st=a->cur[t],p=a->pattern[t];
        if(e.type==K_ACT_UP||e.type==K_ACT_DOWN)select_step(a,st+(e.type==K_ACT_DOWN?1:-1));
        else if(e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT){
            const int n=clamp(t+(e.type==K_ACT_RIGHT?1:-1),0,K_TRACKS-1);
            a->cur[n]=st;a->track=n;seq_follow(a);select_step(a,st);
        }else if(k_is_drum(a,t))k_lane_step_toggle(a,t,a->cur_lane[t],st);
        else if(k_roll_count(a,t,p,st))k_roll_clear(a,t,p,st);
        else k_roll_add(a,t,p,st,a->rnote[t]<0||a->rnote[t]>127?60:a->rnote[t],a->rlen[t]?a->rlen[t]:1,a->rvel[t]?a->rvel[t]:100);
        return;
    }
    if(a->page==K_PAGE_ROLL && (a->lay&K_LAY_ROLL_CELLS) && !k_is_drum(a,a->track) &&
       (e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT||e.type==K_ACT_UP||e.type==K_ACT_DOWN||e.type==K_ACT_ACCEPT||e.type==K_ACT_VALUE_DELTA||e.type==K_ACT_GRAB)){
        /* the roll as cells: the cursor a step and a pitch (cur, rnote); OK puts a note there or takes it away, the
         * value keys its length (an empty cell: the next note's) */
        const int t=a->track,p=a->pattern[t];
        int sl=k_roll_at(a,t,p,a->cur[t],a->rnote[t]);
        if(e.type==K_ACT_GRAB&&e.a){a->grab=1;a->grab_moved=0;return;}   /* OK down: a grip */
        if(e.type==K_ACT_GRAB){const int moved=a->grab_moved;a->grab=a->grab_moved=0;if(moved)return;e.type=K_ACT_ACCEPT;}   /* let go unmoved: OK */
        if(a->grab&&(e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT||e.type==K_ACT_UP||e.type==K_ACT_DOWN)){
            /* OK held: the note under the cursor edited where the cursor is on it. Its head: left / right its start
             * (the length with it); its tail: its end (the length); its middle: the whole note in time; up / down: its pitch. On an
             * empty cell: notes drawn along the way, as A + a direction */
            const int d=e.type==K_ACT_RIGHT||e.type==K_ACT_UP?1:-1,hz=e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT;
            a->grab_moved=1;
            if(sl<0){   /* draw: here, then the next cell */
                if(!k_roll_count(a,t,p,a->cur[t])||k_roll_at(a,t,p,a->cur[t],a->rnote[t])<0)k_roll_add(a,t,p,a->cur[t],a->rnote[t],1,a->rvel[t]?a->rvel[t]:100);
                if(hz)a->cur[t]=clamp(a->cur[t]+d,0,K_STEPS-1);else a->rnote[t]=clamp(a->rnote[t]+d,0,127);
                if(k_roll_at(a,t,p,a->cur[t],a->rnote[t])<0)k_roll_add(a,t,p,a->cur[t],a->rnote[t],1,a->rvel[t]?a->rvel[t]:100);
            }else{
                const KNote n=a->roll[t][sl];
                const int s0=n.at&15,s1=s0+n.len-1,c=a->cur[t];
                if(!hz){   /* the pitch */
                    const int to=clamp(n.note+d,0,127);
                    k_roll_set(a,t,sl,s0,to,n.len);a->rnote[t]=to;
                }else if(c==s0&&(d<0||n.len>1)&&!(c==s1&&d>0)){   /* the head: the start */
                    if(s0+d>=0&&s0+d<=s1){k_roll_set(a,t,sl,s0+d,n.note,n.len-d);a->cur[t]=s0+d;}
                }else if(c==s1){   /* the tail: the end */
                    if(s1+d<K_STEPS&&s1+d>=s0){k_roll_set(a,t,sl,s0,n.note,n.len+d);a->cur[t]=s1+d;}
                }else if(s0+d>=0&&s1+d<K_STEPS){k_roll_set(a,t,sl,s0+d,n.note,n.len);a->cur[t]=c+d;}   /* the middle: the whole */
            }
            sl=k_roll_at(a,t,p,a->cur[t],a->rnote[t]);a->rsel[t]=sl;
            if(sl>=0){a->rlen[t]=a->roll[t][sl].len;a->rvel[t]=a->roll[t][sl].vel;}
            {int i;for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_STEP&&a->focus[i].index==a->cur[t]){a->focused=i;break;}}
            return;
        }
        switch(e.type){
            case K_ACT_LEFT:case K_ACT_RIGHT:a->cur[t]=clamp(a->cur[t]+(e.type==K_ACT_RIGHT?1:-1),0,K_STEPS-1);break;
            case K_ACT_UP:case K_ACT_DOWN:a->rnote[t]=clamp(a->rnote[t]+(e.type==K_ACT_UP?1:-1),0,127);break;
            case K_ACT_ACCEPT:
                if(sl>=0)k_roll_remove(a,t,sl);
                else{k_roll_add(a,t,p,a->cur[t],a->rnote[t],clamp(a->rlen[t],1,K_STEPS-a->cur[t]),a->rvel[t]?a->rvel[t]:100);k_preview(a,a->rnote[t],1);k_preview(a,a->rnote[t],0);}
                break;
            default:{const int d=e.a>0?1:e.a<0?-1:0;
                if(sl>=0){KNote *n=&a->roll[t][sl];n->len=(uint8_t)clamp(n->len+d,1,K_STEPS-(n->at&15));a->rlen[t]=n->len;}
                else a->rlen[t]=clamp(a->rlen[t]+d,1,K_STEPS);
                break;}
        }
        sl=k_roll_at(a,t,p,a->cur[t],a->rnote[t]);   /* the note under the cursor is the one in hand (its length, its velocity shown) */
        a->rsel[t]=sl;
        if(sl>=0){a->rlen[t]=a->roll[t][sl].len;a->rvel[t]=a->roll[t][sl].vel;}
        {int i;for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_STEP&&a->focus[i].index==a->cur[t]){a->focused=i;break;}}   /* (the cursor: the cell) */
        return;
    }
    if((a->lay&K_LAY_MIX_KEYS) && a->page!=K_PAGE_MIXER && (e.type==K_ACT_UP||e.type==K_ACT_DOWN) && !a->menu_open && !a->settings_open && !a->save_open
       && a->focus_count && a->focused<a->focus_count && a->focus[a->focused].kind==K_NODE_PARAM){   /* a keyboard alone (the Zero): on the
                                                 * knob pages up / down a row, the same column (its right walked the row's end only) */
        const int vis=k_params_visible(a),col=vis>0?a->focus[a->focused].index%vis:0,row=vis>0?a->win/vis:0,rows=page_rows(a,a->page);
        int i;
        if(e.type==K_ACT_UP&&row==0)goto as_before;   /* (up from the first row: as before) */
        if(vis>0&&row+(e.type==K_ACT_DOWN?1:-1)<rows){
            mixer_row(a,e.type==K_ACT_DOWN?1:-1);   /* (the FM-1's SELECT over the rows: stops at the last) */
            for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_PARAM&&a->focus[i].index==a->win+col){a->focused=i;break;}
        }
        return;
    }
    as_before:
    if(a->page==K_PAGE_MIXER && (a->lay&K_LAY_MIX_KEYS)){   /* a keyboard alone (the Zero): left / right the strip (the track),
                                                 * up / down the control in it (Level Pan B1 B2); its value is the value keys' (K L) */
        int i,cur=a->focus[a->focused].kind==K_NODE_PARAM?a->focus[a->focused].index:0;
        if(e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT){int t=clamp(a->track+(e.type==K_ACT_RIGHT?1:-1),0,K_TRACKS-1);if(t!=a->track){a->track=t;seq_follow(a);}return;}
        if(e.type==K_ACT_UP||e.type==K_ACT_DOWN){
            static const int8_t ORDER[4]={2,3,1,0};   /* top to bottom as the strip draws them: B1, B2, Pan, the fader (up is up) */
            int at=0,to;
            while(at<3&&ORDER[at]!=cur)at++;
            to=ORDER[clamp(at+(e.type==K_ACT_DOWN?1:-1),0,3)];
            for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_PARAM&&a->focus[i].index==to){a->focused=i;break;}
            return;
        }
        if(e.type==K_ACT_VALUE_DELTA&&a->focus[a->focused].kind!=K_NODE_PARAM)for(i=0;i<a->focus_count;i++)if(a->focus[i].kind==K_NODE_PARAM){a->focused=i;break;}
    }
    if(a->page==K_PAGE_SONG&&(e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT)&&a->focused<a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE){   /* along a
                                                 * row: the next scene, the window moving at its edges */
        const int f=a->focus[a->focused].index;
        song_focus(a,f/K_SCENES,f%K_SCENES+(e.type==K_ACT_RIGHT?1:-1));
        return;
    }
    if(a->page==K_PAGE_SONG && (e.type==K_ACT_FOCUS_NEXT||e.type==K_ACT_FOCUS_PREV||e.type==K_ACT_PARAM_DELTA)){   /* SONG on the knobs
                                                 * SELECT the scene (stops at the ends), K1..K4 the patterns of tracks 1..4 in it: none A B C D */
        const int f=a->focused<a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE?a->focus[a->focused].index:a->track*K_SCENES,sc=f%K_SCENES;
        if(e.type==K_ACT_PARAM_DELTA){
            const int t=e.a,v=a->song[sc][t>=0&&t<K_TRACKS?t:0],pos=v>=4?0:v+1,np=clamp(pos+(e.b>0?1:e.b<0?-1:0),0,4);
            if(t<0||t>=K_TRACKS||!e.b)return;
            k_song_cell_set(a,sc,t,np==0?4:np-1);
            song_focus(a,t,sc);
        }else song_focus(a,f/K_SCENES,sc+(e.type==K_ACT_FOCUS_NEXT?1:-1));
        return;
    }
    switch(e.type){
        case K_ACT_FOCUS_NEXT:if(a->page==K_PAGE_ROLL)roll_walk(a,1);else if(a->page==K_PAGE_DRUM)select_lane(a,a->cur_lane[a->track]+1);else if(k_page_rowed(a->page))mixer_row(a,1);else if(!slide(a,1))a->focused=wrap(a->focused+1,a->focus_count);break;
        case K_ACT_FOCUS_PREV:if(a->page==K_PAGE_ROLL)roll_walk(a,-1);else if(a->page==K_PAGE_DRUM)select_lane(a,a->cur_lane[a->track]-1);else if(k_page_rowed(a->page))mixer_row(a,-1);else if(!slide(a,-1))a->focused=wrap(a->focused-1,a->focus_count);break;
        case K_ACT_UP:if(!move_grid(a,0,-1))move_spatial(a,0,-1);break;
        case K_ACT_DOWN:if(!move_grid(a,0,1))move_spatial(a,0,1);break;
        case K_ACT_LEFT:if(mixer_across(a,-1))break;if(!slide(a,-1)&&!move_grid(a,-1,0))move_spatial(a,-1,0);break;
        case K_ACT_RIGHT:if(mixer_across(a,1))break;if(!slide(a,1)&&!move_grid(a,1,0))move_spatial(a,1,0);break;
        case K_ACT_ACCEPT:activate(a);break;
        default:break;
    }
    if((e.type==K_ACT_UP||e.type==K_ACT_DOWN||e.type==K_ACT_LEFT||e.type==K_ACT_RIGHT)&&a->page==K_PAGE_DRUM&&a->focused<a->focus_count&&
       a->focus[a->focused].kind==K_NODE_STEP)a->cur[a->track]=(KVal)(a->focus[a->focused].index%K_STEPS);   /* (the cursor's cell: the step in hand, CHANCE's) */
    switch(e.type){
        case K_ACT_BACK:   /* HOME: the mixer; on the mixer, the track's macros; on them, the mixer again. OCT- (e.b): the mixer */
            if(a->page==K_PAGE_MIXER){if(!e.b)k_open_page(a,K_PAGE_HOME);}
            else k_open_page(a,K_PAGE_MIXER);
            break;
        case K_ACT_REC:a->recording=!a->recording;break;
        case K_ACT_PLAY:a->playing=!a->playing;a->count_in=0;
            if(a->playing){a->play_ms=e.time_ms;a->last_idx=-1;a->clk.pulses=a->clk.pend=0;a->clk.seen=a->clk.cont=0;
                if(a->recording&&a->set_v[K_SET_COUNT]&&a->clock_src<=0){   /* armed, Count-in: a bar of clicks first, the steps after it */
                    a->count_in=1;a->play_ms=e.time_ms+240000u/(uint32_t)(a->bpm>0?a->bpm:120);a->step=-1;a->beat=0;}
                /* on SONG: the song, from the scene in hand (its column), a bar a scene, to its end; elsewhere the patterns in hand loop */
                a->song_play=a->page==K_PAGE_SONG;
                a->song_scene=a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE?a->focus[a->focused].index%K_SCENES:0;}
            else{a->beat=-1;a->step=-1;a->song_play=0;}
            break;
        case K_ACT_TRACK_DELTA:a->track=wrap(a->track+e.a,K_TRACKS);seq_follow(a);
            if(a->page==K_PAGE_SONG&&a->focused<a->focus_count&&a->focus[a->focused].kind==K_NODE_SCENE)   /* SONG: the cursor onto the track's row,
                                                 * the same scene (the FM-1's way to a row: HOME + SELECT) */
                song_focus(a,a->track,a->focus[a->focused].index%K_SCENES);
            break;
        case K_ACT_PARAM_DELTA:
            if(e.a>=0 && e.a<k_params_visible(a)) {   /* e.a is the knob's place in the window */
                int i,p=a->win+e.a;
                add_param(a,a->page,p,e.b);
                a->touched|=(uint64_t)1<<p;a->touch_ms[p]=e.time_ms;
                for(i=0;i<a->focus_count;i++)   /* focus follows the knob last turned */
                    if(a->focus[i].kind==K_NODE_PARAM && a->focus[i].index==p){a->focused=i;break;}
            }
            break;
        case K_ACT_SOUND_DELTA:   /* PRESETS: the user presets, one a detent (kreisel_save.c); only where the preset's name is on the screen
                                 * the synth's pages and HOME's macros, or (e.b) from where a screen shows it (a desktop's header) */
            if(e.b||a->page==K_PAGE_SOUND||a->page==K_PAGE_EDIT||a->page==K_PAGE_ENV1||a->page==K_PAGE_ENV2||a->page==K_PAGE_LFO||a->page==K_PAGE_HOME||
               a->page==K_PAGE_MIXER)   /* (the mixer: its strips name the presets) */
                k_save_preset_step(a,e.a);
            break;
        case K_ACT_TYPE_DELTA:   /* ALGORITHM: the engine on its pages, the effect in hand on FX, the pattern on SEQ; nothing elsewhere */
            if(a->page==K_PAGE_SOUND||a->page==K_PAGE_EDIT||a->page==K_PAGE_ENV1||a->page==K_PAGE_ENV2||a->page==K_PAGE_LFO)engine_step(a,e.a,e.time_ms);
            else if(a->page==K_PAGE_ROLL||a->page==K_PAGE_STEP||a->page==K_PAGE_DRUM){   /* SEQ: the pattern, A .. D (stops at the ends) */
                if(k_pattern_set(a,a->track,a->pattern[a->track]+(e.a>0?1:-1)))k_balloon_show(&a->balloon,K_BALLOON_PATTERN,e.time_ms);}
            else if(a->page==K_PAGE_FX){int slot=0;k_fx_item(a,a->win,&slot,0);   /* the effect of the row's slot (stops at the ends) */
                k_fx_type_step(a,slot,e.a,0);a->touched|=(uint64_t)1<<(slot*4);a->touch_ms[slot*4]=e.time_ms;}
            break;
        case K_ACT_STEP_TOGGLE:
            if((a->page==K_PAGE_STEP||a->page==K_PAGE_ROLL||a->page==K_PAGE_DRUM) && e.a>=0 && e.a<K_STEPS){
                int i,t=a->track;
                if(a->page==K_PAGE_DRUM)a->lane_on[t][a->cur_lane[t]]^=(uint16_t)(1u<<e.a);   /* the lane in hand */
                else step_toggle(a,t,e.a);
                for(i=0;i<a->focus_count;i++)
                    if(a->focus[i].kind==K_NODE_STEP && a->focus[i].index==(a->page==K_PAGE_DRUM?a->cur_lane[t]*K_STEPS+e.a:e.a)){a->focused=i;break;}
            }
            break;
        case K_ACT_KEY:key_event(a,e.a,e.b,e.time_ms);break;
        case K_ACT_LANE_SELECT:if(k_is_drum(a,a->track) && e.a>=0 && e.a<K_LANES)select_lane(a,e.a);break;
        case K_ACT_VALUE_DELTA:value_delta(a,e.a,e.time_ms);break;
        case K_ACT_POINTER:{int n=k_hit_focus(a,e.a,e.b);
            if(n>=0){a->focused=n;activate(a);
                if(a->focus[n].kind==K_NODE_PARAM){int k=a->focus[n].index;a->touched|=(uint64_t)1<<k;a->touch_ms[k]=e.time_ms;}}   /* a touched card opens */
            break;}
        default:break;
    }
}
