/* Kreisel: the sequencer's tools (a page's quick menu): clear, fill, random, Euclid, shift,
 * reverse, transpose, copy and paste, humanize, and a one-step undo. They work on the track in hand: a synth track's pattern in hand
 * (its notes, KApp.roll), a drum track's lane in hand (its steps, lane_on). How a page opens them is the UI's (not decided yet).
 * Every tool but COPY and UNDO first keeps the track as it was (undo_*): UNDO puts that back, and again UNDO takes it back again.
 * The other pages' tools (SONG, SOUND / EDIT, MIXER / TRACKS, FX) keep what they change in undo_roll's bytes (undo_kind says what). */
#include "kreisel.h"
#include "kreisel_engine.h"
#include "kreisel_level.h"
#include <stdio.h>
#include <string.h>

/* the scales (SCL's SCL, the order of pagedefs.c's N_SCALE): bit i, the degree i semitones over the root */
const uint16_t k_scale_masks[16] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x295, 0x4A9, 0x9AD, 0x5AB, 0xAD5, 0x56B, 0xAAD, 0x4E9, 0x555, 0x6DB, 0xB6D};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t rnd(KApp *a) {   /* xorshift32 */
    uint32_t x = a->tool_rng ? a->tool_rng : 0x2545F491u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return a->tool_rng = x;
}
static int in_scale(const KApp *a, int t, int note) {
    const int root = a->params[t][K_PAGE_SCL][0], scl = a->params[t][K_PAGE_SCL][1] & 15;
    return (k_scale_masks[scl] >> ((note - root + 1200) % 12)) & 1;
}
/* the Euclidean rhythm: k onsets spread as evenly as can be over 16 steps (the first on step 0), turned `rot` steps later */
unsigned k_euclid(int k, int rot) {
    unsigned b = 0;
    int i;
    k = clampi(k, 0, K_STEPS);
    for (i = 0; i < K_STEPS; i++) if ((i * k) % K_STEPS < k) b |= 1u << i;
    rot = ((rot % K_STEPS) + K_STEPS) % K_STEPS;
    return rot ? ((b << rot) | (b >> (K_STEPS - rot))) & 0xFFFFu : b;
}
static void keep(KApp *a, int t) {
    a->undo_t = (int8_t)t; a->undo_kind = 1;
    memcpy(a->undo_roll, a->roll[t], sizeof a->undo_roll);
    memcpy(a->undo_lanes, a->lane_on[t], sizeof a->undo_lanes); a->undo_lpat = (uint8_t)(a->pattern[t] & 3);
    a->undo_chance = a->chance[t][a->undo_lpat];
}
/* the step in hand: the one STEP and ROLL edit; a drum track's, its last step switched (a key, a tap) or the DRUM cursor's cell */
static int step_in_hand(const KApp *a, int t) { return clampi(a->cur[t], 0, K_STEPS - 1); }
static uint32_t rot_chance(uint32_t w, int n) { n = ((n % 16) + 16) % 16; return n ? (w << 2 * n) | (w >> (32 - 2 * n)) : w; }   /* (as rot16, 2 bits a step) */
static void rebits(KApp *a, int t) {   /* steps[t][*] after the notes moved (k_roll_add keeps it, a tool that moves them in place does not) */
    int p, i;
    for (p = 0; p < 4; p++) a->steps[t][p] = 0;
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len) a->steps[t][(a->roll[t][i].at >> 4) & 3] |= (uint16_t)(1u << (a->roll[t][i].at & 15));
    a->rsel[t] = -1;
}
static void clear_pat(KApp *a, int t, int p) {
    int i;
    for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && (a->roll[t][i].at >> 4) == p) a->roll[t][i].len = 0;
}
static void put_bits(KApp *a, int t, int p, unsigned bits, int len) {   /* a note a step as bits says: the note in hand's pitch and velocity */
    int s;
    for (s = 0; s < K_STEPS; s++) if (bits >> s & 1u) k_roll_add(a, t, p, s, a->rnote[t], len, a->rvel[t]);
}
static uint16_t rev16(uint16_t b) { uint16_t r = 0; int i; for (i = 0; i < 16; i++) if (b >> i & 1) r |= (uint16_t)(1u << (15 - i)); return r; }
static uint16_t rot16(uint16_t b, int n) { n = ((n % 16) + 16) % 16; return n ? (uint16_t)((b << n) | (b >> (16 - n))) : b; }

/* ---- the other pages' tools: what they change, kept in undo_roll's bytes (and a sound or the inserts in clip's) ---- */
typedef struct { int16_t eng, lane, n; int16_t v[K_ENGINE_PARAMS]; } SndSnap;
typedef struct { int16_t v[K_TRACKS][4]; uint8_t mute, solo; } MixSnap;
typedef struct { int16_t v[K_FX_ITEMS]; } FxSnap;
typedef char snaps_fit[(sizeof(SndSnap) <= sizeof((KApp *)0)->undo_roll && sizeof(MixSnap) <= sizeof((KApp *)0)->undo_roll &&
                        sizeof(FxSnap) <= sizeof((KApp *)0)->undo_roll && K_SCENES <= sizeof((KApp *)0)->clip &&
                        sizeof(SndSnap) <= sizeof((KApp *)0)->clip && sizeof(FxSnap) <= sizeof((KApp *)0)->clip) ? 1 : -1];
static KVal *snd_val(KApp *a, int t, int lane, int i) {   /* the sound's i-th value of track t (a drum track: of that lane) */
    const int ot = a->track, ol = a->cur_lane[t];
    KVal *p;
    a->track = t; a->cur_lane[t] = lane;
    p = k_param(a, K_PAGE_SOUND, i);
    a->track = ot; a->cur_lane[t] = ol;
    return p;
}
static void snd_snap(KApp *a, int t, int lane, SndSnap *s) {
    int i;
    s->eng = (int16_t)a->sound[t]; s->lane = (int16_t)lane; s->n = (int16_t)k_engine_at(a->sound[t])->nparams;
    for (i = 0; i < s->n && i < K_ENGINE_PARAMS; i++) s->v[i] = (int16_t)*snd_val(a, t, lane, i);
}
static int snd_put(KApp *a, int t, int lane, const SndSnap *s) {   /* (onto the same engine only) */
    const KEngine *e = k_engine_at(a->sound[t]);
    int i;
    if (s->eng != a->sound[t] || k_track_missing(a, t)) return 0;
    for (i = 0; i < s->n && i < e->nparams; i++) *snd_val(a, t, lane, i) = k_param_clamp(&e->edit[i], s->v[i]);
    a->preset_dirty[t] = 1;
    return 1;
}
static void mix_snap(const KApp *a, MixSnap *m) {
    int t, k;
    for (t = 0; t < K_TRACKS; t++) for (k = 0; k < 4; k++) m->v[t][k] = (int16_t)a->params[t][K_PAGE_MIXER][k];
    m->mute = a->mute; m->solo = a->solo;
}
static void mix_put(KApp *a, const MixSnap *m) {
    int t, k;
    for (t = 0; t < K_TRACKS; t++) for (k = 0; k < 4; k++) a->params[t][K_PAGE_MIXER][k] = m->v[t][k];
    a->mute = m->mute; a->solo = m->solo;
}
static void fx_snap(const KApp *a, int t, FxSnap *f) { int k; for (k = 0; k < K_FX_ITEMS; k++) f->v[k] = (int16_t)a->params[t][K_PAGE_FX][k]; }
static void fx_put(KApp *a, int t, const FxSnap *f) { int k; for (k = 0; k < K_FX_ITEMS; k++) a->params[t][K_PAGE_FX][k] = f->v[k]; }
/* what a kind of tool changes, kept (into undo_roll's bytes) before it does (a sound: the lane in hand's, or keep_lane's) */
static int keep_lane = -1;
static void keep_kind(KApp *a, int kind, int t) {
    void *u = a->undo_roll;
    a->undo_kind = (uint8_t)kind; a->undo_t = (int8_t)t;
    if (kind == 2) memcpy(a->undo_song, a->song, sizeof a->song);
    else if (kind == 3) { SndSnap s; snd_snap(a, t, keep_lane >= 0 ? keep_lane : a->cur_lane[t], &s); memcpy(u, &s, sizeof s); }
    else if (kind == 4) { MixSnap m; mix_snap(a, &m); memcpy(u, &m, sizeof m); }
    else if (kind == 5) { FxSnap f; fx_snap(a, t, &f); memcpy(u, &f, sizeof f); }
}
/* the kept state back; swap: what was there kept in its place (UNDO, then UNDO again redoes) */
static int put_back(KApp *a, int swap) {
    void *u = a->undo_roll;
    const int t = a->undo_t;
    if (t < 0 || t >= K_TRACKS) return 0;
    switch (a->undo_kind) {
    case 1: {
        KNote r[K_ROLL_NOTES]; uint16_t ln[K_LANES];
        uint16_t *lp = k_lanes_mut(a, t, a->undo_lpat);   /* (the lanes of the pattern they were kept from) */
        memcpy(r, a->roll[t], sizeof r); memcpy(ln, lp, sizeof ln);
        const uint32_t ch = a->chance[t][a->undo_lpat & 3];
        memcpy(a->roll[t], a->undo_roll, sizeof r); memcpy(lp, a->undo_lanes, sizeof ln);
        a->chance[t][a->undo_lpat & 3] = a->undo_chance;
        if (swap) { memcpy(a->undo_roll, r, sizeof r); memcpy(a->undo_lanes, ln, sizeof ln); a->undo_chance = ch; }
        rebits(a, t);
    } return 1;
    case 2: { int s, k; for (s = 0; s < K_SCENES; s++) for (k = 0; k < K_TRACKS; k++) {   /* (swapped a cell at a time: no second song on the stack) */
        const uint8_t now = a->song[s][k]; a->song[s][k] = a->undo_song[s][k]; if (swap) a->undo_song[s][k] = now; } } return 1;
    case 3: { SndSnap was, now; memcpy(&was, u, sizeof was); snd_snap(a, t, was.lane, &now);
        if (was.eng != a->sound[t]) k_track_engine(a, t, was.eng);   /* (a sound duplicated onto another engine: that one back) */
        if (!snd_put(a, t, was.lane, &was)) return 0; if (swap) memcpy(u, &now, sizeof now); } return 1;
    case 4: { MixSnap was, now; memcpy(&was, u, sizeof was); mix_snap(a, &now); mix_put(a, &was); if (swap) memcpy(u, &now, sizeof now); } return 1;
    case 5: { FxSnap was, now; memcpy(&was, u, sizeof was); fx_snap(a, t, &now); fx_put(a, t, &was); if (swap) memcpy(u, &now, sizeof now); } return 1;
    default: return 0;
    }
}
static int scene_in_hand(const KApp *a) {   /* SONG: the column of the cell in hand (else the scene playing) */
    if (a->page == K_PAGE_SONG && a->focus_count && a->focus[a->focused].kind == K_NODE_SCENE) return a->focus[a->focused].index % K_SCENES;
    return clampi(a->song_scene, 0, K_SCENES - 1);
}
static int page_tool(KApp *a, KTool tool, int x) {   /* SONG, SOUND, MIXER, FX's: 1 done, 0 nothing to do; -1 not one of them */
    const int t = a->track;
    int i, k;
    switch (tool) {
    case K_TOOL_SC_DUP: {   /* the scene in hand onto scene x: what each track plays there */
        const int s = scene_in_hand(a), d = clampi(x, 0, K_SCENES - 1);
        if (d == s) return 0;
        keep_kind(a, 2, t);
        memcpy(a->song[d], a->song[s], sizeof a->song[d]);
        return 1;
    }
    case K_TOOL_SC_INSERT: case K_TOOL_SC_DELETE: case K_TOOL_SC_CLEAR: {
        const int s = scene_in_hand(a);
        keep_kind(a, 2, t);
        if (tool == K_TOOL_SC_CLEAR) { for (i = 0; i < K_SCENES; i++) if (x || i == s) for (k = 0; k < K_TRACKS; k++) a->song[i][k] = 4; return 1; }
        if (tool == K_TOOL_SC_DELETE) {
            for (i = s; i < K_SCENES - 1; i++) memcpy(a->song[i], a->song[i + 1], sizeof a->song[i]);
            memset(a->song[K_SCENES - 1], 4, sizeof a->song[0]);
            return 1;
        }
        for (i = K_SCENES - 1; i > s; i--) memcpy(a->song[i], a->song[i - 1], sizeof a->song[i]);   /* (INSERT: the later a scene on) */
        if (tool == K_TOOL_SC_INSERT) memset(a->song[s], 4, sizeof a->song[s]);
        return 1;
    }
    case K_TOOL_SN_INIT: case K_TOOL_SN_RANDOM: {
        const KEngine *e = k_engine_at(a->sound[t]);
        const int l = a->cur_lane[t], drum = e->uid == K_UID('D', 'R', 'U', 'M'), amt = clampi(x, 0, 100);
        if (k_track_missing(a, t)) return 0;
        keep_kind(a, 3, t);
        for (i = drum; i < e->nparams; i++) {   /* (a drum lane's PIECE, its first, stays) */
            const KParamDesc *d = &e->edit[i];
            KVal *v = snd_val(a, t, l, i); int r;
            if (tool == K_TOOL_SN_INIT) { *v = d->def; continue; }
            r = d->min + (int)(rnd(a) % (unsigned)(d->max - d->min + 1));
            *v = k_param_clamp(d, *v + (r - *v) * amt / 100);
        }
        a->preset_dirty[t] = 1;
        return 1;
    }
    case K_TOOL_SN_COPY: {
        SndSnap s;
        if (k_track_missing(a, t)) return 0;
        snd_snap(a, t, a->cur_lane[t], &s); memcpy(a->clip, &s, sizeof s); a->clip_kind = K_CLIP_SOUND;
        return 1;
    }
    case K_TOOL_SN_PASTE: {
        SndSnap s;
        if (a->clip_kind != K_CLIP_SOUND) return 0;
        memcpy(&s, a->clip, sizeof s);
        if (s.eng != a->sound[t] || k_track_missing(a, t)) return 0;   /* (another engine: its values mean other things) */
        keep_kind(a, 3, t);
        return snd_put(a, t, a->cur_lane[t], &s);
    }
    case K_TOOL_SN_DUP: {   /* the sound onto track x (its engine taken there too); a drum track: the lane in hand's onto lane x */
        SndSnap snd;
        const int drum = k_is_drum(a, t), d = clampi(x, 0, drum ? K_LANES - 1 : K_TRACKS - 1);
        if (k_track_missing(a, t) || (drum ? d == a->cur_lane[t] : d == t) || (!drum && k_track_missing(a, d))) return 0;
        snd_snap(a, t, a->cur_lane[t], &snd);
        if (drum) { keep_lane = d; keep_kind(a, 3, t); keep_lane = -1; snd.lane = (int16_t)d; return snd_put(a, t, d, &snd); }
        keep_kind(a, 3, d);
        if (a->sound[d] != a->sound[t]) k_track_engine(a, d, a->sound[t]);
        return snd_put(a, d, a->cur_lane[d], &snd);
    }
    case K_TOOL_FX_DUP: {   /* the inserts onto track x */
        FxSnap f;
        const int d = clampi(x, 0, K_TRACKS - 1);
        if (d == t) return 0;
        fx_snap(a, t, &f); keep_kind(a, 5, d); fx_put(a, d, &f); a->preset_dirty[d] = 1;
        return 1;
    }
    case K_TOOL_MX_LEVELS: case K_TOOL_MX_PANS: case K_TOOL_MX_SENDS: case K_TOOL_MX_UNMUTE:
        keep_kind(a, 4, t);
        for (k = 0; k < K_TRACKS; k++) {
            KVal *m = a->params[k][K_PAGE_MIXER];
            if (tool == K_TOOL_MX_LEVELS) m[0] = K_LEVEL_0DB;
            else if (tool == K_TOOL_MX_PANS) m[1] = 50;
            else if (tool == K_TOOL_MX_SENDS) m[2] = m[3] = 0;
        }
        if (tool == K_TOOL_MX_UNMUTE) a->mute = a->solo = 0;
        return 1;
    case K_TOOL_FX_SWAP: {
        KVal *f = a->params[t][K_PAGE_FX];
        keep_kind(a, 5, t);
        for (i = 0; i < 4; i++) { k = f[i]; f[i] = f[4 + i]; f[4 + i] = k; }
        for (i = 0; i < K_FX_EXTRA_PER_SLOT; i++) { k = f[8 + i]; f[8 + i] = f[8 + K_FX_EXTRA_PER_SLOT + i]; f[8 + K_FX_EXTRA_PER_SLOT + i] = k; }
        return 1;
    }
    case K_TOOL_FX_INIT: {   /* x 0 slot 1, 1 slot 2, 2 both: empty, their values at the middle */
        KVal *f = a->params[t][K_PAGE_FX]; int s;
        keep_kind(a, 5, t);
        for (s = 0; s < K_FX_SLOTS; s++) {
            if (x != 2 && x != s) continue;
            f[s * 4] = 0; for (i = 1; i < 4; i++) f[s * 4 + i] = 50;
            for (i = 0; i < K_FX_EXTRA_PER_SLOT; i++) f[8 + s * K_FX_EXTRA_PER_SLOT + i] = 50;
        }
        return 1;
    }
    case K_TOOL_FX_COPY: { FxSnap f; fx_snap(a, t, &f); memcpy(a->clip, &f, sizeof f); a->clip_kind = K_CLIP_INSERTS; return 1; }
    case K_TOOL_FX_PASTE: {
        FxSnap f;
        if (a->clip_kind != K_CLIP_INSERTS) return 0;
        memcpy(&f, a->clip, sizeof f);
        keep_kind(a, 5, t); fx_put(a, t, &f);
        return 1;
    }
    default: return -1;
    }
}

int k_seq_tool(KApp *a, KTool tool, int x, int y) {
    const int t = a->track, p = a->pattern[t] & 3, drum = k_is_drum(a, t), l = a->cur_lane[t];
    int i, n = 0;
    if (t < 0 || t >= K_TRACKS || (unsigned)tool >= K_TOOL_COUNT) return 0;
    if (tool == K_TOOL_UNDO) return put_back(a, 1);   /* the last tool's change, whatever page it was on (and the undone state kept: UNDO again redoes) */
    if (tool == K_TOOL_VIEW) { a->rows_view = (uint8_t)!a->rows_view; return 1; }   /* (a view: nothing kept for undo) */
    { const int done = page_tool(a, tool, x); if (done >= 0) return done; }
    if (tool == K_TOOL_COPY) {   /* the pattern's notes (their steps), or the lane's steps */
        if (drum) { a->clip_kind = K_CLIP_LANE; a->clip_lane = a->lane_on[t][l]; return 1; }
        for (i = 0; i < K_ROLL_NOTES; i++) if (a->roll[t][i].len && (a->roll[t][i].at >> 4) == p) { a->clip[n] = a->roll[t][i]; a->clip[n++].at &= 15; }
        a->clip_n = (uint8_t)n; a->clip_kind = K_CLIP_NOTES;
        return 1;
    }
    if (tool == K_TOOL_PASTE && a->clip_kind != (drum ? K_CLIP_LANE : K_CLIP_NOTES)) return 0;   /* (nothing of this kind copied) */
    if (tool == K_TOOL_CHANCE) {   /* x %: the step in hand's chance (y 1: every step's); its notes or hits all together */
        const int c = (100 - clampi(x, 25, 100) + 12) / 25, s0 = step_in_hand(a, t);
        uint32_t w = a->chance[t][p];
        for (i = 0; i < K_STEPS; i++) if (y || i == s0) w = (w & ~(3u << 2 * i)) | (uint32_t)c << 2 * i;
        keep(a, t);   /* (kept even when nothing changes: a tool tried and let go puts back what it kept) */
        if (w == a->chance[t][p]) return 0;
        a->chance[t][p] = w;
        return 1;
    }
    if (tool == K_TOOL_DUP) {   /* the pattern onto pattern x, or the lane's steps onto lane x */
        const int d = clampi(x, 0, drum ? K_LANES - 1 : 3);
        if (drum ? d == l : d == p) return 0;
        keep(a, t);
        if (drum) { a->lane_on[t][d] = a->lane_on[t][l]; return 1; }
        clear_pat(a, t, d);
        a->chance[t][d] = a->chance[t][p];   /* (the steps' chances with the notes) */
        for (i = 0; i < K_ROLL_NOTES; i++)
            if (a->roll[t][i].len && (a->roll[t][i].at >> 4) == p && (a->roll[t][i].at & 15) < K_STEPS) {
                const KNote k = a->roll[t][i];
                k_roll_add(a, t, d, k.at & 15, k.note, k.len, k.vel);
            }
        rebits(a, t);
        return 1;
    }
    if (drum && (tool == K_TOOL_TRANSPOSE)) return 0;
    keep(a, t);
    if (drum) {
        uint16_t *b = &a->lane_on[t][l];
        switch (tool) {
        case K_TOOL_CLEAR: if (x) { memset(a->lane_on[t], 0, sizeof a->lane_on[t]); a->chance[t][p] = 0; } else *b = 0; break;   /* x 1: every lane (and the steps' chances) */
        case K_TOOL_FILL: { const int e = clampi(x, 1, 16); uint16_t f = 0; for (i = 0; i < K_STEPS; i += e) f |= (uint16_t)(1u << i); *b = f; } break;
        case K_TOOL_RANDOM: { uint16_t f = 0; for (i = 0; i < K_STEPS; i++) if ((int)(rnd(a) % 100) < clampi(x, 0, 100)) f |= (uint16_t)(1u << i); *b = f; } break;
        case K_TOOL_EUCLID: *b = (uint16_t)k_euclid(x, y); break;
        case K_TOOL_SHIFT: *b = rot16(*b, x); break;
        case K_TOOL_REVERSE: *b = rev16(*b); break;
        case K_TOOL_PASTE: *b = a->clip_lane; break;
        case K_TOOL_HUMANIZE: break;   /* (a lane's steps have no velocity) */
        default: return 0;
        }
        return 1;
    }
    switch (tool) {
    case K_TOOL_CLEAR: clear_pat(a, t, p); a->chance[t][p] = 0; break;
    case K_TOOL_FILL: { const int e = clampi(x, 1, 16); clear_pat(a, t, p); for (i = 0; i < K_STEPS; i += e) n |= 1 << i; put_bits(a, t, p, (unsigned)n, clampi(a->rlen[t], 1, e)); } break;
    case K_TOOL_EUCLID: clear_pat(a, t, p); put_bits(a, t, p, k_euclid(x, y), 1); break;
    case K_TOOL_RANDOM: {   /* x: how many steps sound (0..100 %); y: how far the pitch wanders from the note in hand, in the scale's notes */
        const int dens = clampi(x, 0, 100), wide = clampi(y, 0, 12), base = a->rnote[t];
        int s;
        clear_pat(a, t, p);
        for (s = 0; s < K_STEPS; s++) {
            int note = base, k, d;
            if ((int)(rnd(a) % 100) >= dens) continue;
            d = wide ? (int)(rnd(a) % (unsigned)(2 * wide + 1)) - wide : 0;   /* -wide..wide degrees of the scale */
            for (k = 0; d && k < 128; k++) { note += d > 0 ? 1 : -1; if (note < 0 || note > 127) { note = clampi(note, 0, 127); break; } if (in_scale(a, t, note)) d += d > 0 ? -1 : 1; }
            k_roll_add(a, t, p, s, note, 1, clampi(a->rvel[t] - 16 + (int)(rnd(a) % 33), 1, 127));
        }
    } break;
    case K_TOOL_SHIFT: case K_TOOL_REVERSE: case K_TOOL_TRANSPOSE: case K_TOOL_HUMANIZE:
        if (tool == K_TOOL_SHIFT) a->chance[t][p] = rot_chance(a->chance[t][p], x);   /* (the chances go round with the notes) */
        for (i = 0; i < K_ROLL_NOTES; i++) {
            KNote *k = &a->roll[t][i];
            int s;
            if (!k->len || (k->at >> 4) != p) continue;
            s = k->at & 15;
            if (tool == K_TOOL_SHIFT) { s = (((s + x) % K_STEPS) + K_STEPS) % K_STEPS; k->len = (uint8_t)clampi(k->len, 1, K_STEPS - s); }   /* round: what leaves one end comes in at the other */
            else if (tool == K_TOOL_REVERSE) s = clampi(K_STEPS - s - k->len, 0, K_STEPS - 1);   /* its end where its start was */
            else if (tool == K_TOOL_TRANSPOSE) k->note = (uint8_t)clampi(k->note + x, 0, 127);
            else k->vel = (uint8_t)clampi(k->vel + (int)(rnd(a) % (unsigned)(2 * clampi(x, 0, 64) + 1)) - clampi(x, 0, 64), 1, 127);   /* x: up to so much either way */
            k->at = (uint8_t)(p << 4 | s);
        }
        for (i = 0; i < K_ROLL_NOTES; i++) {   /* two notes landed on one step and pitch: the later slot goes */
            int j;
            for (j = 0; a->roll[t][i].len && j < i; j++) if (a->roll[t][j].len && a->roll[t][j].at == a->roll[t][i].at && a->roll[t][j].note == a->roll[t][i].note) { a->roll[t][i].len = 0; break; }
        }
        break;
    case K_TOOL_PASTE:
        clear_pat(a, t, p);
        for (i = 0; i < a->clip_n; i++) k_roll_add(a, t, p, a->clip[i].at & 15, a->clip[i].note, a->clip[i].len, a->clip[i].vel);
        break;
    default: return 0;
    }
    rebits(a, t);
    return 1;
}

void k_seq_tool_revert(KApp *a) { put_back(a, 0); }

/* ---- the tools' list (a page's quick menu): rows, names, values ---- */
typedef char tool_vals_fit[(sizeof ((KApp *)0)->tool_x / sizeof ((KApp *)0)->tool_x[0] == K_TOOL_COUNT) ? 1 : -1];   /* (KApp.tool_x holds one a tool) */
typedef struct { const char *name, *xn, *yn; int16_t x0, x1, xd, y0, y1, yd; } ToolDesc;   /* (16 bits: a scene to 255) */
static const ToolDesc TD[K_TOOL_COUNT] = {   /* name, its values: label, range, default (no label: none) */
    [K_TOOL_CLEAR] = {"Dynamite", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_FILL] = {"Fill", "EVERY", 0, 1, 16, 4, 0, 0, 0},
    [K_TOOL_RANDOM] = {"Random", "DENS", "RANGE", 0, 100, 50, 0, 12, 3},
    [K_TOOL_EUCLID] = {"Euclid", "HITS", "ROT", 0, 16, 5, 0, 15, 0},
    [K_TOOL_SHIFT] = {"Shift", "STEPS", 0, -15, 15, 1, 0, 0, 0},
    [K_TOOL_REVERSE] = {"Reverse", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_TRANSPOSE] = {"Transpose", "SEMI", 0, -24, 24, 1, 0, 0, 0},
    [K_TOOL_COPY] = {"Copy", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_PASTE] = {"Paste", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_DUP] = {"Duplicate", "TO", 0, 0, 7, 1, 0, 0, 0},
    [K_TOOL_HUMANIZE] = {"Humanize", "AMT", 0, 0, 64, 16, 0, 0, 0},
    [K_TOOL_UNDO] = {"Undo", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SC_INSERT] = {"Insert", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SC_DUP] = {"Duplicate", "TO", 0, 0, K_SCENES - 1, 1, 0, 0, 0},
    [K_TOOL_SC_DELETE] = {"Delete", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SC_CLEAR] = {"Dynamite", "SCENES", 0, 0, 1, 0, 0, 0, 0},
    [K_TOOL_SN_INIT] = {"Init", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SN_RANDOM] = {"Random", "AMT", 0, 5, 100, 30, 0, 0, 0},
    [K_TOOL_SN_COPY] = {"Copy", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SN_PASTE] = {"Paste", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_SN_DUP] = {"Duplicate", "TO", 0, 0, 7, 1, 0, 0, 0},
    [K_TOOL_MX_LEVELS] = {"Levels 0dB", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_MX_PANS] = {"Pans center", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_MX_SENDS] = {"Sends off", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_MX_UNMUTE] = {"Unmute", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_FX_SWAP] = {"Swap", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_FX_INIT] = {"Init", "SLOT", 0, 0, 2, 2, 0, 0, 0},
    [K_TOOL_FX_COPY] = {"Copy", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_FX_PASTE] = {"Paste", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_FX_DUP] = {"Duplicate", "TO", 0, 0, K_TRACKS - 1, 1, 0, 0, 0},
    [K_TOOL_VIEW] = {"Tracker", 0, 0, 0, 0, 0, 0, 0, 0},
    [K_TOOL_CHANCE] = {"Chance", "PCT", "STEPS", 25, 100, 50, 0, 1, 0},
};
static const KTool SYNTH_ROWS[] = {K_TOOL_FILL, K_TOOL_EUCLID, K_TOOL_RANDOM, K_TOOL_SHIFT, K_TOOL_TRANSPOSE, K_TOOL_HUMANIZE,
                                   K_TOOL_REVERSE, K_TOOL_DUP, K_TOOL_CLEAR, K_TOOL_UNDO};   /* (a step's chance: STEP's card; the tracker: SEQ's page key) */
static const KTool DRUM_ROWS[] = {K_TOOL_FILL, K_TOOL_EUCLID, K_TOOL_RANDOM, K_TOOL_SHIFT, K_TOOL_REVERSE, K_TOOL_DUP, K_TOOL_CLEAR,
                                  K_TOOL_UNDO};   /* (copy and paste: a desktop's, k_edit; on a panel Duplicate takes the lane where it goes) */
static const KTool SONG_ROWS[] = {K_TOOL_SC_INSERT, K_TOOL_SC_DUP, K_TOOL_SC_DELETE, K_TOOL_SC_CLEAR, K_TOOL_UNDO};
static const KTool SOUND_ROWS[] = {K_TOOL_SN_RANDOM, K_TOOL_SN_INIT, K_TOOL_SN_DUP, K_TOOL_UNDO};
static const KTool MIX_ROWS[] = {K_TOOL_MX_UNMUTE, K_TOOL_MX_LEVELS, K_TOOL_MX_PANS, K_TOOL_MX_SENDS, K_TOOL_UNDO};
static const KTool FX_ROWS[] = {K_TOOL_FX_SWAP, K_TOOL_FX_INIT, K_TOOL_FX_DUP, K_TOOL_UNDO};
#define NROWS(r) ((int)(sizeof r / sizeof r[0]))
static const KTool *rows_of(const KApp *a, int *n) {   /* the page in hand's tools (none: 0) */
    switch (a->page) {
    case K_PAGE_STEP: case K_PAGE_DRUM: case K_PAGE_ROLL:
        if (k_is_drum(a, a->track)) { *n = NROWS(DRUM_ROWS); return DRUM_ROWS; }
        *n = NROWS(SYNTH_ROWS); return SYNTH_ROWS;
    case K_PAGE_SONG: *n = NROWS(SONG_ROWS); return SONG_ROWS;
    case K_PAGE_SOUND: case K_PAGE_EDIT: *n = NROWS(SOUND_ROWS); return SOUND_ROWS;
    case K_PAGE_MIXER: case K_PAGE_TRACKS: *n = NROWS(MIX_ROWS); return MIX_ROWS;
    case K_PAGE_FX: *n = NROWS(FX_ROWS); return FX_ROWS;
    default: *n = 0; return 0;
    }
}
int k_tools_page(const KApp *a) { int n; return rows_of(a, &n) != 0; }
int k_tools_count(const KApp *a) { int n; rows_of(a, &n); return n; }
KTool k_tools_item(const KApp *a, int row) {
    int n;
    const KTool *r = rows_of(a, &n);
    return r && n > 0 ? r[clampi(row, 0, n - 1)] : K_TOOL_UNDO;
}
const char *k_tool_name(KTool t) { return (unsigned)t < K_TOOL_COUNT ? TD[t].name : ""; }
const char *k_tool_val_name(KTool t, int which, int drum) {
    if ((unsigned)t >= K_TOOL_COUNT) return 0;
    if (t == K_TOOL_CLEAR && drum && !which) return "LANES";   /* a drum track: this lane, or every lane */
    if (t == K_TOOL_RANDOM && drum && which) return 0;         /* (a lane has no pitch) */
    return which ? TD[t].yn : TD[t].xn;
}
int k_tool_vals(KTool t, int drum) { return !!k_tool_val_name(t, 0, drum) + !!k_tool_val_name(t, 1, drum); }
void k_tool_val_text(const KApp *a, KTool t, int which, char *out, int n) {
    const int v = which ? a->tool_y[t] : a->tool_x[t];
    if (n < 1) return;
    out[0] = 0;
    if (!k_tool_val_name(t, which, k_is_drum(a, a->track))) return;
    if (t == K_TOOL_CLEAR || t == K_TOOL_SC_CLEAR) snprintf(out, (size_t)n, "%s", v ? "All" : "One");
    else if (t == K_TOOL_CHANCE) { if (which) { if (v) snprintf(out, (size_t)n, "All"); else snprintf(out, (size_t)n, "%d", step_in_hand(a, a->track) + 1); } else snprintf(out, (size_t)n, "%d%%", v); }   /* (the step in hand, numbered) */
    else if ((t == K_TOOL_RANDOM && !which) || t == K_TOOL_SN_RANDOM) snprintf(out, (size_t)n, "%d%%", v);
    else if (t == K_TOOL_FX_INIT) snprintf(out, (size_t)n, "%s", v == 2 ? "Both" : v ? "2" : "1");
    else if (t == K_TOOL_DUP && !k_is_drum(a, a->track)) snprintf(out, (size_t)n, "%c", 'A' + (v & 3));   /* (where it goes: a pattern's letter, */
    else if (t == K_TOOL_DUP || t == K_TOOL_SC_DUP || t == K_TOOL_SN_DUP || t == K_TOOL_FX_DUP) snprintf(out, (size_t)n, "%d", v + 1);   /* a lane, scene, track) */
    else if (t == K_TOOL_SHIFT || t == K_TOOL_TRANSPOSE) snprintf(out, (size_t)n, v > 0 ? "+%d" : "%d", v);
    else if (t == K_TOOL_HUMANIZE) snprintf(out, (size_t)n, "%c%d", v ? '~' : ' ', v);
    else snprintf(out, (size_t)n, "%d", v);
}
/* the tools' values at their defaults, the list closed */
void k_tools_init(KApp *a) {
    int i;
    for (i = 0; i < K_TOOL_COUNT; i++) { a->tool_x[i] = TD[i].xd; a->tool_y[i] = TD[i].yd; }
    a->tools_open = 0; a->tools_sel = 0; a->tools_try = -1;
}
/* a value of tool t turned by d (CLEAR on a drum track: 0 one lane, 1 every lane): 1 when it moved */
int k_tool_turn(KApp *a, KTool t, int which, int d) {
    const int drum = k_is_drum(a, a->track);
    int16_t *v = which ? &a->tool_y[t] : &a->tool_x[t];
    int lo = which ? TD[t].y0 : TD[t].x0, hi = which ? TD[t].y1 : TD[t].x1, w;
    if (!k_tool_val_name(t, which, drum) || !d) return 0;
    if (t == K_TOOL_CLEAR) { lo = 0; hi = 1; }
    if (t == K_TOOL_DUP || t == K_TOOL_SN_DUP) hi = drum ? K_LANES - 1 : t == K_TOOL_DUP ? 3 : K_TRACKS - 1;   /* (where it goes: a lane, a pattern, a track) */
    if ((t == K_TOOL_RANDOM && !which) || t == K_TOOL_SN_RANDOM) d *= 5;   /* (density, amount in 5 % steps) */
    if (t == K_TOOL_CHANCE && !which) d *= 25;   /* (a chance: 25 % steps) */
    w = clampi(*v + d, lo, hi);
    if (w == *v) return 0;
    *v = (int8_t)w;
    return 1;
}
/* a value's range (a slider's ends); 0 when the tool has no such value */
int k_tool_range(const KApp *a, KTool t, int which, int *lo, int *hi) {
    if ((unsigned)t >= K_TOOL_COUNT || !k_tool_val_name(t, which, k_is_drum(a, a->track))) return 0;
    *lo = which ? TD[t].y0 : TD[t].x0; *hi = which ? TD[t].y1 : TD[t].x1;
    if (t == K_TOOL_CLEAR) { *lo = 0; *hi = 1; }
    if (t == K_TOOL_DUP || t == K_TOOL_SN_DUP) *hi = k_is_drum(a, a->track) ? K_LANES - 1 : t == K_TOOL_DUP ? 3 : K_TRACKS - 1;
    return 1;
}
/* a value set outright (a slider), within its range (density and amount in their 5 % steps); 1 when it moved */
int k_tool_set(KApp *a, KTool t, int which, int v) {
    int lo, hi;
    int16_t *p;
    if (!k_tool_range(a, t, which, &lo, &hi)) return 0;
    if ((t == K_TOOL_RANDOM && !which) || t == K_TOOL_SN_RANDOM) v = (v + 2) / 5 * 5;
    if (t == K_TOOL_CHANCE && !which) v = (v + 12) / 25 * 25;
    v = clampi(v, lo, hi);
    p = which ? &a->tool_y[t] : &a->tool_x[t];
    if (*p == v) return 0;
    *p = (int16_t)v;
    return 1;
}
/* SONG's blocks (a desktop's selection): a scene's column (kind 1: its cells for every track) or a track's row (kind 2: its cells in
 * every scene), copied, pasted over (kept first: UNDO), or emptied; 1 when done */
int k_song_block(KApp *a, int op, int kind, int idx) {
    int i, n = kind == 1 ? K_TRACKS : K_SCENES;
    if ((kind != 1 && kind != 2) || idx < 0 || idx >= (kind == 1 ? K_SCENES : K_TRACKS)) return 0;
    uint8_t *clip = (uint8_t *)a->clip;   /* (the one clipboard's bytes) */
    if (op == 0) { for (i = 0; i < n; i++) clip[i] = kind == 1 ? a->song[idx][i] : a->song[i][idx]; a->clip_kind = kind == 1 ? K_CLIP_SCENE : K_CLIP_ROW; return 1; }
    if (op == 1 && a->clip_kind != (kind == 1 ? K_CLIP_SCENE : K_CLIP_ROW)) return 0;   /* (a column pastes over a column, a row over a row) */
    keep_kind(a, 2, a->track);
    for (i = 0; i < n; i++) {
        uint8_t *c = kind == 1 ? &a->song[idx][i] : &a->song[i][idx];
        *c = op == 1 ? clip[i] : 4;   /* (cleared: nothing) */
    }
    return 1;
}

/* ---------------------------------------------------------------- the selection and the clipboard (C3) --- */
void k_select(KApp *a, int kind, int idx)
{
    if (kind == K_SEL_LANE && idx >= 0 && idx < K_LANES && k_is_drum(a, a->track)) a->cur_lane[a->track] = idx;
    a->sel_kind = (uint8_t)(kind == K_SEL_SCENE || kind == K_SEL_ROW || kind == K_SEL_LANE ? kind : K_SEL_NONE);
    a->sel_idx = (uint8_t)(idx < 0 ? 0 : idx);
}
int k_selected(const KApp *a, int kind)
{
    if (a->sel_kind != kind) return -1;
    if ((kind == K_SEL_SCENE || kind == K_SEL_ROW) && a->page != K_PAGE_SONG) return -1;
    if (kind == K_SEL_LANE && (a->page != K_PAGE_DRUM || !k_is_drum(a, a->track))) return -1;
    return a->sel_idx;
}
int k_value_copy(KApp *a, KPage page, int i)   /* (into the one clipboard: a pattern or a sound copied before is let go) */
{
    const int16_t v = (int16_t)k_param_get(a, page, i);
    memcpy(a->clip, &v, sizeof v);
    a->clip_kind = K_CLIP_VALUE;
    return 1;
}
int k_value_paste(KApp *a, KPage page, int i)
{
    int16_t v;
    if (a->clip_kind != K_CLIP_VALUE) return 0;
    memcpy(&v, a->clip, sizeof v);
    return k_param_set(a, page, i, v);
}
int k_edit(KApp *a, int op)
{
    int i;
    if (op < 0 || op > 2) return 0;
    if ((i = k_selected(a, K_SEL_SCENE)) >= 0) return k_song_block(a, op, 1, i);
    if ((i = k_selected(a, K_SEL_ROW)) >= 0) return k_song_block(a, op, 2, i);
    if (k_selected(a, K_SEL_LANE) >= 0) return k_seq_tool(a, op == 0 ? K_TOOL_COPY : op == 1 ? K_TOOL_PASTE : K_TOOL_CLEAR, 0, 0);
    if (a->focus_count && a->focus[a->focused].kind == K_NODE_PARAM) {
        i = a->focus[a->focused].index;
        return op == 0 ? k_value_copy(a, a->page, i) : op == 1 ? k_value_paste(a, a->page, i) : k_param_reset(a, a->page, i);
    }
    return 0;
}
