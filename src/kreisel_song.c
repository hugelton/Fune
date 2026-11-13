/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel_song.h"
#include "kreisel_plugin.h"
#include "kreisel_engine.h"
#include "kreisel_engine_api.h"   /* k_param_id: a parameter by its label's id */
#include "kreisel_level.h"
#include <string.h>

#define MAGIC0 'K'
#define MAGIC1 'R'
#define MAGIC2 'S'
#define MAGIC3 'G'
#define HDR 8u
#define CRCLEN 2u
#define NPAT 4
#define CH_HDR 6u            /* tag + u16 length */

#define TAG(a, b, c, d) K_UID(a, b, c, d)

/* The host pages whose parameters are saved, with ids of their own so the enum may be reordered. SOUND's engine parameters are saved
 * with the engine (by label id); the pages without parameter storage (HOME, STEP, DRUM, ROLL, SONG, CLOCK) are not here. */
static const struct { uint8_t id; KPage page; } PAGES[] = {
    {1, K_PAGE_MIXER}, {2, K_PAGE_FX}, {3, K_PAGE_ENV1}, {4, K_PAGE_ENV2}, {5, K_PAGE_ENV_DEST},
    {6, K_PAGE_BUS1}, {7, K_PAGE_BUS2}, {8, K_PAGE_LFO}, {9, K_PAGE_ARP}, {10, K_PAGE_SCL},
};
#define NPAGES ((int)(sizeof PAGES / sizeof PAGES[0]))

/* ---------------------------------------------------------------- parameters --- */
static int pdesc_page(KPage p) { return p == K_PAGE_LFO || p == K_PAGE_ARP || p == K_PAGE_SCL; }
static int pdesc_n(KPage p) { return p == K_PAGE_LFO ? 8 : 4; }   /* the descriptors such a page has */
static int pdef(KPage p, int j)
{
    if (pdesc_page(p) && j < pdesc_n(p)) return k_page_desc(p)[j].def;
    if ((p == K_PAGE_MIXER && j == 0) || ((p == K_PAGE_BUS1 || p == K_PAGE_BUS2) && j == 3)) return K_LEVEL_0DB;   /* a level, a return: 0 dB */
    if (p == K_PAGE_MIXER && (j == 2 || j == 3)) return 0;   /* the sends: none */
    return p == K_PAGE_FX && (j == 0 || j == 4) ? 0 : 50;   /* as k_init sets them */
}
static int pclamp(KPage p, int j, int v)
{
    if (pdesc_page(p) && j < pdesc_n(p)) return k_param_clamp(&k_page_desc(p)[j], v);
    if (p == K_PAGE_FX && (j == 0 || j == 4)) return v < 0 ? 0 : v > K_FX_TYPES - 1 ? K_FX_TYPES - 1 : v;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}
static int pcount(KPage p) { int n = p == K_PAGE_ENV1 ? 4 : p == K_PAGE_ENV2 ? 8 :   /* (ENV 2's 4 .. 7: PHASE's DCO envelope) */ p == K_PAGE_LFO ? 9 : k_param_count(p);   /* (LFO: and a drum track's LANE) */ return n > K_MAX_PARAMS ? K_MAX_PARAMS : n; }   /* (ENV 1, 2: their own four; the destinations are ENV DEST's) */

/* A parameter's id: FNV-1a of its label, folded to 16 bits (never 0). Not its position: the table may grow or be reordered. */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------------- writer --- */
typedef struct { uint8_t *b; size_t cap, n; int ok; } W;
static void w8(W *w, unsigned v) { if (w->n < w->cap) w->b[w->n] = (uint8_t)v; else w->ok = 0; w->n++; }
static void w16(W *w, unsigned v) { w8(w, v & 255u); w8(w, (v >> 8) & 255u); }
static void w32(W *w, uint32_t v) { w16(w, v & 0xFFFFu); w16(w, v >> 16); }
static void wtag(W *w, uint32_t t) { w8(w, t >> 24); w8(w, t >> 16); w8(w, t >> 8); w8(w, t); }
static void wbias(W *w, int v) { w8(w, (unsigned)(clampi(v, -128, 127) + 128)); }

static uint16_t crc16(const uint8_t *p, size_t n)   /* CCITT, 0xFFFF start */
{
    uint16_t c = 0xFFFF;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= (uint16_t)((unsigned)p[i] << 8);
        for (k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (c << 1) ^ 0x1021u : c << 1);
    }
    return c;
}

/* a name as kept: up to K_NAME_LEN of A-Z 0-9 space - . (others become a space), trailing spaces cut */
static int name_len(const char *s)
{
    int n = 0;
    while (s && n < K_NAME_LEN && s[n]) n++;
    while (n > 0 && s[n - 1] == ' ') n--;
    return n;
}
static int name_ch(int c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ? c : c >= 'a' && c <= 'z' ? c - 32 : ' '; }
static void wname(W *w, const char *s)
{
    const int n = name_len(s);
    int i;
    if (!n) return;
    wtag(w, TAG('N', 'A', 'M', 'E')); w16(w, (unsigned)n);
    for (i = 0; i < n; i++) w8(w, (unsigned)name_ch((unsigned char)s[i]));
}
static size_t finish(W *w, uint8_t *buf, size_t cap, size_t len_at)
{
    size_t plen;
    uint16_t c;
    if (!w->ok || w->n + CRCLEN > cap) return 0;
    plen = w->n - HDR;
    if (plen > 0xFFFFu) return 0;
    buf[len_at] = (uint8_t)plen; buf[len_at + 1] = (uint8_t)(plen >> 8);
    c = crc16(buf, w->n);
    buf[w->n] = (uint8_t)c; buf[w->n + 1] = (uint8_t)(c >> 8);
    return w->n + CRCLEN;
}
/* an outside drum's lanes TRAK carries for track t (its first values' count; 0: none): the drum in use, or a missing one's as read */
static int trak_lanes(const KApp *a, int t)
{
    if (k_track_missing(a, t)) {
        const int n = a->missing[t].n;
        return a->plug_lane_uid[t] && a->plug_lane_uid[t] == a->missing[t].uid ? (n < K_LANE_VALS ? n : K_LANE_VALS) : 0;
    }
    return k_plugin_lane_params(a->sound[t]);
}
/* the lanes after TRAK's values: m, then each lane's m values (+128), of TRAK's first m parameters */
static void wlanes(W *w, const KApp *a, int t, int m)
{
    int l, k;
    if (!m) return;
    w8(w, (unsigned)m);
    for (l = 0; l < K_LANES; l++) for (k = 0; k < m; k++) wbias(w, k_track_missing(a, t) ? a->plug_lane[t][l][k] : k_lane_val(a, t, l, k));
}
/* TRAK for track t, written as track `as` (a preset: 0) */
static void wtrak(W *w, const KApp *a, int t, int as)
{
    const KEngine *e = k_engine_at(a->sound[t]);   /* a bad index writes the engine the app would play */
    const int lm = trak_lanes(a, t);
    const unsigned lb = lm ? 1u + (unsigned)(K_LANES * lm) : 0u;
    int i;
    if (k_track_missing(a, t)) {   /* a plugin that is not here: its id, version and values written back as they were read */
        int m = a->missing[t].n > K_ENGINE_PARAMS ? K_ENGINE_PARAMS : a->missing[t].n;
        wtag(w, TAG('T', 'R', 'A', 'K')); w16(w, 1 + 4 + 1 + 1 + 1 + 1 + (unsigned)m * 3u + lb);
        w8(w, (unsigned)as); w32(w, a->missing[t].uid); w8(w, a->missing[t].version);
        w8(w, (unsigned)clampi(a->tcolor[t], 0, 255)); w8(w, (unsigned)clampi(a->pattern[t], 0, 255)); w8(w, (unsigned)m);
        for (i = 0; i < m; i++) { w8(w, a->missing[t].raw[i][0]); w8(w, a->missing[t].raw[i][1]); w8(w, a->missing[t].raw[i][2]); }
        wlanes(w, a, t, lm);
        return;
    }
    wtag(w, TAG('T', 'R', 'A', 'K')); w16(w, 1 + 4 + 1 + 1 + 1 + 1 + (unsigned)e->nparams * 3u + lb);
    w8(w, (unsigned)as); w32(w, e->uid); w8(w, (unsigned)k_plugin_at(a->sound[t])->version);
    w8(w, (unsigned)clampi(a->tcolor[t], 0, 255)); w8(w, (unsigned)clampi(a->pattern[t], 0, 255)); w8(w, e->nparams);
    for (i = 0; i < e->nparams; i++) { w16(w, k_param_id(e->edit[i].label)); wbias(w, a->params[t][K_PAGE_SOUND][4 + i]); }
    wlanes(w, a, t, lm);
}
/* PRMS for track t (as `as`): what differs from the default, on the pages `mask` has (bit = PAGES[] index) */
static void wprms(W *w, const KApp *a, int t, int as, unsigned mask)
{
    int n = 0, i, j;
    for (i = 0; i < NPAGES; i++) if (mask >> i & 1u) for (j = 0; j < pcount(PAGES[i].page); j++)
        if (a->params[t][PAGES[i].page][j] != pdef(PAGES[i].page, j)) n++;
    wtag(w, TAG('P', 'R', 'M', 'S')); w16(w, 1 + (unsigned)n * 3u);
    w8(w, (unsigned)as);
    for (i = 0; i < NPAGES; i++) if (mask >> i & 1u) for (j = 0; j < pcount(PAGES[i].page); j++) {
        int v = a->params[t][PAGES[i].page][j];
        if (v != pdef(PAGES[i].page, j)) { w8(w, PAGES[i].id); w8(w, (unsigned)j); wbias(w, v); }
    }
}
#define ALL_PAGES ((1u << NPAGES) - 1u)
/* a sound's pages: the inserts, the envelopes and where they go, the LFO (not the mixer, the buses, ARP, SCL) */
static unsigned sound_mask(void)
{
    unsigned m = 0;
    int i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].page == K_PAGE_FX || PAGES[i].page == K_PAGE_ENV1 || PAGES[i].page == K_PAGE_ENV2 || PAGES[i].page == K_PAGE_ENV_DEST || PAGES[i].page == K_PAGE_LFO) m |= 1u << i;
    return m;
}

size_t k_song_max_size(void)
{
    size_t params = 0, per_track;
    int i;
    for (i = 0; i < NPAGES; i++) params += (size_t)pcount(PAGES[i].page) * 3u;
    per_track = (CH_HDR + 1 + 4 + 1 + 1 + 1 + 1 + K_ENGINE_PARAMS * 3)  /* TRAK */
              + 1 + K_LANES * K_LANE_VALS                               /* an outside drum's lanes in TRAK, or DRUM's kit */
              + (CH_HDR + 1 + 1 + K_ROLL_NOTES * 4)                     /* ROLL */
              + (CH_HDR + 1 + K_LANES * 2 + 3 * K_LANES * 2)            /* DRUM */
              + (CH_HDR + 1 + params)                                   /* PRMS */
              + (CH_HDR + 1 + 4 * 4);                                   /* PROB */
    return HDR + (CH_HDR + 6) + (CH_HDR + 2 + K_SCENES * ((K_TRACKS + 1) / 2)) + (CH_HDR + K_NAME_LEN) + K_TRACKS * per_track + CRCLEN;
}

size_t k_song_save(const KApp *a, uint8_t *buf, size_t cap)
{
    W w;
    int t, s, l, i, j;
    size_t len_at;
    if (!a || !buf) return 0;
    w.b = buf; w.cap = cap; w.n = 0; w.ok = 1;
    w8(&w, MAGIC0); w8(&w, MAGIC1); w8(&w, MAGIC2); w8(&w, MAGIC3);
    w16(&w, K_SONG_FORMAT);
    len_at = w.n; w16(&w, 0);
    /* GLOB */
    wtag(&w, TAG('G', 'L', 'O', 'B')); w16(&w, 6);
    w8(&w, (unsigned)clampi(a->bpm, 0, 255)); w8(&w, (unsigned)clampi(a->swing, 0, 255)); w8(&w, (unsigned)clampi(a->clock_src, 0, 255));
    w8(&w, a->mute & ((1u << K_TRACKS) - 1u)); w8(&w, a->solo & ((1u << K_TRACKS) - 1u));
    w8(&w, K_TRACKS);   /* the tracks of the device that saved it (a file without this byte: four) */
    /* SONG */
    {   /* SCNS: the tracks, the scenes up to the last with anything in it, a cell a nibble (0..3 a pattern, 4 none), two tracks a byte */
        int rows = 0;
        for (s = 0; s < K_SCENES; s++) for (t = 0; t < K_TRACKS; t++) if (a->song[s][t] < 4) rows = s + 1;
        wtag(&w, TAG('S', 'C', 'N', 'S')); w16(&w, (unsigned)(2 + rows * ((K_TRACKS + 1) / 2)));
        w8(&w, K_TRACKS); w8(&w, 0);   /* (a byte kept for later) */
        for (s = 0; s < rows; s++) for (t = 0; t < K_TRACKS; t += 2)
            w8(&w, (unsigned)((a->song[s][t] & 15u) | ((t + 1 < K_TRACKS ? a->song[s][t + 1] : 4u) & 15u) << 4));
    }
    wname(&w, a->song_name);
    for (t = 0; t < K_TRACKS; t++) {
        wtrak(&w, a, t, t);
        {   /* ROLL: the track's notes, those there only (pattern << 4 | step, length, note, velocity) */
            int c = 0;
            for (i = 0; i < K_ROLL_NOTES; i++) c += a->roll[t][i].len != 0;
            wtag(&w, TAG('R', 'O', 'L', 'L')); w16(&w, (unsigned)(2 + c * 4));
            w8(&w, (unsigned)t); w8(&w, (unsigned)c);
            for (i = 0; i < K_ROLL_NOTES; i++) {
                const KNote *n = &a->roll[t][i];
                if (!n->len) continue;
                w8(&w, n->at); w8(&w, n->len); w8(&w, n->note & 127u); w8(&w, n->vel & 127u);
            }
        }
        {   /* pattern A's lanes (as before), DRUM's kit, then B C D's (2026-11-12). A track whose TRAK has an outside drum's lanes:
             * the four patterns' alone (the kit, not in use, is not kept: a song keeps the engine in use's, 2026-11-12) */
            const int kit = !trak_lanes(a, t);
            wtag(&w, TAG('D', 'R', 'U', 'M')); w16(&w, 1 + K_LANES * 2 + (kit ? K_LANES * K_LANE_VALS : 0) + 3 * K_LANES * 2);
            w8(&w, (unsigned)t);
            for (l = 0; l < K_LANES; l++) w16(&w, k_lanes(a, t, 0)[l]);
            if (kit) for (l = 0; l < K_LANES; l++) for (j = 0; j < K_LANE_VALS; j++) w8(&w, (unsigned)clampi(a->lane_p[t][l][j], 0, 127));
            for (j = 1; j < 4; j++) for (l = 0; l < K_LANES; l++) w16(&w, k_lanes(a, t, j)[l]);
        }
        wprms(&w, a, t, t, ALL_PAGES);   /* PRMS: only what differs from the default */
        if (a->chance[t][0] | a->chance[t][1] | a->chance[t][2] | a->chance[t][3]) {   /* PROB (2026-11-12): the steps' chances, 2 bits a step,
                                                 * each pattern's; written only when a step has one (a song without it: always) */
            wtag(&w, TAG('P', 'R', 'O', 'B')); w16(&w, 1 + 4 * 4);
            w8(&w, (unsigned)t);
            for (j = 0; j < 4; j++) w32(&w, a->chance[t][j]);
        }
    }
    return finish(&w, buf, cap, len_at);
}

/* ---------------------------------------------------------------- loader --- */
static unsigned r16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p) | (uint32_t)r16(p + 2) << 16; }
static int unbias(uint8_t b) { return (int)b - 128; }

/* the engines Kreisel had before its own (Felucca-Engines' ids, songs saved before 2026-11-05): their successors, at those
 * engines' defaults (the old parameters have other ids, so none is taken). NOISE went into LOFI, WHEEL is the ORGAN. */
static uint32_t song_uid(uint32_t u)
{
    switch (u) {
    case K_UID('A', 'N', 'L', 'G'): return K_UID('K', 'A', 'N', 'L');
    case K_UID('P', 'H', 'A', 'S'): return K_UID('K', 'P', 'H', 'S');
    case K_UID('T', 'R', 'I', 'O'): return K_UID('K', 'T', 'R', 'I');
    case K_UID('L', 'O', 'F', 'I'): case K_UID('N', 'O', 'I', 'S'): return K_UID('K', 'L', 'O', 'F');
    case K_UID('W', 'H', 'E', 'L'): return K_UID('K', 'O', 'G', 'N');
    default: return u;
    }
}
/* TRAK into track t; whole: also its colour and the pattern in use (a song; a preset keeps the track's) */
static void load_trak_to(KApp *a, int t, const uint8_t *p, size_t len, int whole)
{
    int e, n, i, k, newer;
    const uint8_t *q;
    if (len < 9) return;
    e = k_engine_find(song_uid(r32(p + 1)));
    /* p[5]: the engine's version. Params are matched by id, so a table that grew or moved still reads right; a song made with a
     * newer version than the one here is kept untouched, as a missing one (its values may mean what this one does not know) */
    newer = e >= 0 && p[5] > k_plugin_at(e)->version;
    if (whole) {
        a->tcolor[t] = clampi(p[6], 0, K_PALETTE - 1);
        a->pattern[t] = clampi(p[7], 0, NPAT - 1);
    }
    n = p[8];
    q = p + 9;
    if ((size_t)n * 3u > len - 9) n = (int)((len - 9) / 3u);
    a->missing[t].uid = 0;
    a->missing[t].newer = (uint8_t)newer;
    if (e < 0 || newer) {   /* a plugin that is not here (or older than the song's): kept as it was saved (id, version, values), the track silent; the first engine
                    * holds its place so every index stays good */
        a->missing[t].uid = r32(p + 1);
        a->missing[t].version = p[5];
        a->missing[t].n = (uint8_t)(n > K_ENGINE_PARAMS ? K_ENGINE_PARAMS : n);
        for (k = 0; k < a->missing[t].n; k++) { a->missing[t].raw[k][0] = q[3 * k]; a->missing[t].raw[k][1] = q[3 * k + 1]; a->missing[t].raw[k][2] = q[3 * k + 2]; }
        if (!a->missing[t].uid) a->missing[t].uid = 1;   /* (an id of 0 in a file: still missing, still written back) */
        e = 0;
        n = 0;
    }
    a->sound[t] = e;
    a->plug_lane_uid[t] = 0;   /* (no lanes after the values: an old song's, or another engine's; each lane plays the track's) */
    for (i = 0; i < K_ENGINE_PARAMS; i++) a->params[t][K_PAGE_SOUND][4 + i] = i < k_engine_at(e)->nparams ? k_engine_at(e)->edit[i].def : 0;
    for (k = 0; k < n; k++, q += 3) {
        uint16_t id = (uint16_t)r16(q);
        for (i = 0; i < k_engine_at(e)->nparams; i++)
            if (k_param_id(k_engine_at(e)->edit[i].label) == id) {
                a->params[t][K_PAGE_SOUND][4 + i] = k_param_clamp(&k_engine_at(e)->edit[i], unbias(q[2]));
                break;
            }
    }
    {   /* an outside drum's lanes (2026-11-12): m, then each lane's m values of the first m parameters above (q: there) */
        const size_t at = 9 + (size_t)p[8] * 3u;
        const int m = at < len ? p[at] : 0;
        const uint8_t *lv = p + at + 1;
        if (!m || at + 1 + (size_t)(K_LANES * m) > len) return;
        if (a->missing[t].uid) {   /* kept as read, written back with the values (the rest of the first ten: the track's) */
            const int keep = a->missing[t].n < K_LANE_VALS ? a->missing[t].n : K_LANE_VALS;
            int l;
            for (l = 0; l < K_LANES; l++) for (k = 0; k < keep; k++) a->plug_lane[t][l][k] = unbias(k < m ? lv[l * m + k] : a->missing[t].raw[k][2]);
            a->plug_lane_uid[t] = a->missing[t].uid;
            return;
        }
        {
            const KEngine *en = k_engine_at(e);
            const int lm = k_plugin_lane_params(e);
            int l;
            if (!lm) return;   /* (not a drum here: dropped) */
            for (l = 0; l < K_LANES; l++) for (k = 0; k < lm; k++) a->plug_lane[t][l][k] = a->params[t][K_PAGE_SOUND][4 + k];
            for (k = 0; k < m && k < n; k++) {   /* the file's k-th: the parameter of its id */
                const uint16_t id = (uint16_t)r16(p + 9 + 3 * k);
                for (i = 0; i < lm; i++) if (k_param_id(en->edit[i].label) == id) {
                    for (l = 0; l < K_LANES; l++) a->plug_lane[t][l][i] = k_param_clamp(&en->edit[i], unbias(lv[l * m + k]));
                    break;
                }
            }
            a->plug_lane_uid[t] = en->uid;
        }
    }
}
static void load_trak(KApp *a, const uint8_t *p, size_t len) { if (len >= 9 && p[0] < K_TRACKS) load_trak_to(a, p[0], p, len, 1); }
/* STEP (songs before the piano roll's chords, 2026-11-08): a note a step, the same in the four patterns, where a pattern's bit
 * is on; read into the track's notes */
static void load_step(KApp *a, const uint8_t *p, size_t len)
{
    int t, i, pt;
    if (len < 1 + NPAT * 2 + K_STEPS * 3 || p[0] >= K_TRACKS) return;
    t = p[0];
    memset(a->roll[t], 0, sizeof a->roll[t]);
    for (pt = 0; pt < NPAT; pt++) {
        const unsigned bits = r16(p + 1 + pt * 2);
        for (i = 0; i < K_STEPS; i++) {
            const uint8_t *q = p + 1 + NPAT * 2 + i * 3;
            if (bits >> i & 1u) k_roll_add(a, t, pt, i, clampi(q[0], 0, 127), clampi(q[2], 1, K_STEPS), clampi(q[1], 1, 127));
        }
    }
}
static void load_roll(KApp *a, const uint8_t *p, size_t len)
{
    int t, i, c;
    if (len < 2 || p[0] >= K_TRACKS) return;
    t = p[0]; c = p[1];
    if ((size_t)(2 + c * 4) > len) c = (int)((len - 2) / 4);
    memset(a->roll[t], 0, sizeof a->roll[t]);
    for (i = 0; i < NPAT; i++) a->steps[t][i] = 0;
    for (i = 0; i < c; i++) {
        const uint8_t *q = p + 2 + i * 4;
        if ((q[0] >> 4) < NPAT && q[1]) k_roll_add(a, t, q[0] >> 4, q[0] & 15, q[2] & 127, q[1], q[3] & 127);
    }
}
static void load_drum(KApp *a, const uint8_t *p, size_t len)
{
    int t, l, j;
    if (p[0] >= K_TRACKS) return;
    t = p[0];
    if (len == 1 + 10 * 5) {   /* before the pieces: ten lanes of level, tune, decay (BD SD CH OH CP LT MT HT RI CY): eight of them kept */
        static const int8_t FROM[K_LANES] = {0, 1, 2, 3, 4, 5, 7, 8};
        const uint8_t *on = p + 1, *pr = p + 1 + 20;
        for (l = 0; l < K_LANES; l++) {
            a->lane_on[t][l] = (uint16_t)r16(on + 2 * FROM[l]);
            { int q; for (q = 0; q < 4; q++) a->lane_pat[t][q][l] = a->lane_on[t][l]; }
            a->lane_p[t][l][4] = clampi(pr[3 * FROM[l]], 0, 127);
            a->lane_p[t][l][1] = clampi(64 + pr[3 * FROM[l] + 1] / 4, 0, 127);
            a->lane_p[t][l][3] = clampi(pr[3 * FROM[l] + 2], 0, 127);
        }
        return;
    }
    if (len == 1 + K_LANES * 2 * 4) {   /* the four patterns' lanes alone (an outside drum's track: its values are TRAK's) */
        int q;
        for (q = 0; q < 4; q++) for (l = 0; l < K_LANES; l++) a->lane_pat[t][q][l] = (uint16_t)r16(p + 1 + 2 * (q * K_LANES + l));
        memcpy(a->lane_on[t], a->lane_pat[t][a->pattern[t] & 3], sizeof a->lane_on[t]);
        return;
    }
    if (len < 1 + K_LANES * 7) return;
    {   /* eight lanes of five (the pieces), of nine (and their ENV) or of ten (and their pan); what a file has not, its default */
        const int nv = len >= 1 + K_LANES * 12 ? K_LANE_VALS : len >= 1 + K_LANES * 11 ? 9 : 5;
        p++;
        const int more = nv == K_LANE_VALS && len >= 1 + K_LANES * 12 + 3 * K_LANES * 2;   /* (B C D's lanes after: else A's for all) */
        int q;
        for (l = 0; l < K_LANES; l++, p += 2) for (q = 0; q < 4; q++) a->lane_pat[t][q][l] = (uint16_t)r16(p);
        for (l = 0; l < K_LANES; l++) {
            for (j = 0; j < nv; j++) a->lane_p[t][l][j] = clampi(*p++, 0, !j ? K_DRUM_PIECES - 1 : j >= K_LANE_ENV ? 100 : 127);
            if (nv < K_LANE_VALS) a->lane_p[t][l][K_LANE_PAN] = 50;
        }
        if (more) for (q = 1; q < 4; q++) for (l = 0; l < K_LANES; l++, p += 2) a->lane_pat[t][q][l] = (uint16_t)r16(p);
        memcpy(a->lane_on[t], a->lane_pat[t][a->pattern[t] & 3], sizeof a->lane_on[t]);   /* (TRAK, before, set the pattern) */
    }
}
static void load_prms_to(KApp *a, int t, const uint8_t *p, size_t len)
{
    int i;
    size_t k;
    if (len < 1) return;
    for (k = 1; k + 3 <= len; k += 3) {
        int pg = -1, j = p[k + 1];
        for (i = 0; i < NPAGES; i++) if (PAGES[i].id == p[k]) pg = i;
        if (pg < 0 || j >= pcount(PAGES[pg].page)) continue;   /* a page or parameter this build does not have: ignored */
        a->params[t][PAGES[pg].page][j] = pclamp(PAGES[pg].page, j, unbias(p[k + 2]));
    }
}
static void load_prms(KApp *a, const uint8_t *p, size_t len) { if (len >= 1 && p[0] < K_TRACKS) load_prms_to(a, p[0], p, len); }
static void load_name(char *out, const uint8_t *d, size_t len)
{
    size_t i, n = len > K_NAME_LEN ? K_NAME_LEN : len;
    for (i = 0; i < n; i++) out[i] = (char)name_ch(d[i]);
    out[n] = 0;
}
/* the header, the CRC, and chunks that tile the payload exactly: the payload's end, or a K_SONG_ERR_* */
static long check_file(const uint8_t *buf, size_t n, char m3)
{
    size_t plen, pos, end;
    if (!buf) return K_SONG_ERR_ARG;
    if (n < HDR + CRCLEN) return K_SONG_ERR_TRUNC;
    if (buf[0] != MAGIC0 || buf[1] != MAGIC1 || buf[2] != MAGIC2 || buf[3] != (uint8_t)m3) return K_SONG_ERR_MAGIC;
    if (r16(buf + 4) == 0 || r16(buf + 4) > K_SONG_FORMAT) return K_SONG_ERR_VERSION;   /* a newer file: not guessed at */
    plen = r16(buf + 6);
    end = HDR + plen;
    if (end + CRCLEN > n) return K_SONG_ERR_TRUNC;
    if (crc16(buf, end) != (uint16_t)r16(buf + end)) return K_SONG_ERR_CRC;
    for (pos = HDR; pos < end;) {
        if (end - pos < CH_HDR) return K_SONG_ERR_TRUNC;
        pos += CH_HDR + r16(buf + pos + 4);
        if (pos > end) return K_SONG_ERR_TRUNC;
    }
    return (long)end;
}

/* every refusal k_song_load / k_preset_load can make, and nothing else: past these the loads cannot fail (the FM-1 checks a file
 * sent to a slot with these: a second KApp to load it into does not fit its RAM) */
int k_song_check(const uint8_t *buf, size_t n)
{
    const long e = check_file(buf, n, MAGIC3);
    return e < 0 ? (int)e : K_SONG_OK;
}
int k_preset_check(const uint8_t *buf, size_t n)
{
    const long end = check_file(buf, n, 'P');
    size_t pos;
    if (end < 0) return (int)end;
    for (pos = HDR; pos < (size_t)end; pos += CH_HDR + r16(buf + pos + 4))   /* a preset without its engine is refused */
        if (TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]) == TAG('T', 'R', 'A', 'K') && r16(buf + pos + 4) >= 9) return K_SONG_OK;
    return K_SONG_ERR_TRUNC;
}

int k_song_load(KApp *a, const uint8_t *buf, size_t n)
{
    size_t pos, end;
    int shown, battery, charging, master, octave;
    uint32_t clock_ms;
    if (!a || !buf) return K_SONG_ERR_ARG;
    {
        const int rc = k_song_check(buf, n);
        if (rc != K_SONG_OK) return rc;
        end = HDR + r16(buf + 6);
    }
    /* a song of more tracks than this device's opens with its first tracks (decided 2026-11-09): every chunk of a track past
     * K_TRACKS is skipped, its SONG columns and mute / solo bits left out. Saved here, it keeps only those */
    /* From here on it cannot fail: start from a fresh app that keeps what the device owns, then apply what is valid. */
    unsigned lay = a->lay;
    uint8_t set_v[K_SETTINGS];
    memcpy(set_v, a->set_v, sizeof set_v);   /* (the settings are the device's, and one changed and not yet kept) */
    const uint8_t set_pending = a->set_pending;
    shown = a->shown; battery = a->battery; charging = a->charging; master = a->master; octave = a->octave; clock_ms = a->clock_ms;
    k_init_released(a);   /* (a note held through the load: let go, not left ringing) */
    a->lay = lay;   /* (the device's: how its screen joins pages) */
    memcpy(a->set_v, set_v, sizeof set_v);
    a->set_pending = set_pending;
    a->clock_ms = clock_ms;
    a->charging = charging != 0;
    k_set_battery(a, battery, charging);
    k_set_shown(a, shown);
    a->master = clampi(master, 0, 100);
    a->octave = clampi(octave, -3, 3);
    for (pos = HDR; pos < end;) {
        uint32_t tag;
        const uint8_t *d = buf + pos + CH_HDR;
        size_t len = r16(buf + pos + 4);
        tag = TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]);
        if (tag == TAG('G', 'L', 'O', 'B') && len >= 5) {
            const KParamDesc *c = k_page_desc(K_PAGE_CLOCK);
            a->bpm = k_param_clamp(&c[0], d[0]); a->swing = k_param_clamp(&c[1], d[1]); a->clock_src = k_param_clamp(&c[2], d[2]);
            a->mute = (uint8_t)(d[3] & ((1u << K_TRACKS) - 1u)); a->solo = (uint8_t)(d[4] & ((1u << K_TRACKS) - 1u));
        } else if (tag == TAG('S', 'C', 'N', 'S') && len >= 2) {   /* the tracks that saved it, a byte kept, two cells a byte (the rows: what the length holds) */
            const size_t tr = d[0] ? d[0] : 1, bpr = (tr + 1) / 2, rows = (len - 2) / bpr, cols = tr < K_TRACKS ? tr : K_TRACKS;
            size_t s, t;
            memset(a->song, 4, sizeof a->song);
            for (s = 0; s < rows && s < K_SCENES; s++) for (t = 0; t < cols; t++)
                a->song[s][t] = (uint8_t)clampi((d[2 + s * bpr + t / 2] >> ((t & 1) * 4)) & 15, 0, 4);
        } else if (tag == TAG('S', 'O', 'N', 'G') && len >= 8) {   /* (the older chunk: eight scenes, a byte a track of the device that saved it) */
            const size_t stride = len / 8, cols = stride < K_TRACKS ? stride : K_TRACKS;
            size_t s, t;
            memset(a->song, 4, sizeof a->song);
            for (s = 0; s < 8; s++) for (t = 0; t < cols; t++) a->song[s][t] = (uint8_t)clampi(d[s * stride + t], 0, 4);
        } else if (tag == TAG('T', 'R', 'A', 'K')) load_trak(a, d, len);
        else if (tag == TAG('S', 'T', 'E', 'P')) load_step(a, d, len);
        else if (tag == TAG('R', 'O', 'L', 'L')) load_roll(a, d, len);
        else if (tag == TAG('D', 'R', 'U', 'M')) load_drum(a, d, len);
        else if (tag == TAG('P', 'R', 'M', 'S')) load_prms(a, d, len);
        else if (tag == TAG('P', 'R', 'O', 'B') && len >= 1 + 4 * 4 && d[0] < K_TRACKS) { int q; for (q = 0; q < 4; q++) a->chance[d[0]][q] = r32(d + 1 + 4 * q); }
        else if (tag == TAG('N', 'A', 'M', 'E')) load_name(a->song_name, d, len);
        pos += CH_HDR + len;
    }
    k_open_page(a, K_PAGE_MIXER);   /* opened on the mixer, HOME's first (the layout anew: the engines may have changed) */
    {   /* a plugin missing: said at once, as a DAW lists them on opening */
        int t;
        for (t = 0; t < K_TRACKS; t++) if (k_track_missing(a, t)) { k_balloon_show(&a->balloon, K_BALLOON_MISSING, a->clock_ms); break; }
    }
    return K_SONG_OK;
}

/* ---------------------------------------------------------------- presets --- */
/* A user preset ("KRSP", the song's header, chunks and CRC): one track's sound. NAME, TRAK (as track 0: the engine and its
 * parameters), KIT (a drum track: its lanes' pieces and their ENV, 8 x 9), PRMS (as track 0: the sound's pages, sound_mask). */
size_t k_preset_max_size(void)
{
    size_t params = 0;
    int i;
    for (i = 0; i < NPAGES; i++) if (sound_mask() >> i & 1u) params += (size_t)pcount(PAGES[i].page) * 3u;
    return HDR + (CH_HDR + K_NAME_LEN) + (CH_HDR + 1 + 4 + 1 + 1 + 1 + 1 + K_ENGINE_PARAMS * 3) + (CH_HDR + 1 + K_LANES * K_LANE_VALS) + (CH_HDR + 1 + params) + CRCLEN;   /* (TRAK's lanes, or KIT) */
}
size_t k_preset_save(const KApp *a, int t, const char *name, uint8_t *buf, size_t cap)
{
    W w;
    size_t len_at;
    int l, j;
    if (!a || !buf || t < 0 || t >= K_TRACKS || k_track_missing(a, t)) return 0;
    w.b = buf; w.cap = cap; w.n = 0; w.ok = 1;
    w8(&w, MAGIC0); w8(&w, MAGIC1); w8(&w, MAGIC2); w8(&w, 'P');
    w16(&w, K_SONG_FORMAT);
    len_at = w.n; w16(&w, 0);
    wname(&w, name);
    wtrak(&w, a, t, 0);
    if (k_is_drum(a, t) && !trak_lanes(a, t)) {   /* DRUM's kit (an outside drum's lanes are TRAK's) */
        wtag(&w, TAG('K', 'I', 'T', ' ')); w16(&w, K_LANES * K_LANE_VALS);
        for (l = 0; l < K_LANES; l++) for (j = 0; j < K_LANE_VALS; j++) w8(&w, (unsigned)clampi(a->lane_p[t][l][j], 0, 127));
    }
    wprms(&w, a, t, 0, sound_mask());
    return finish(&w, buf, cap, len_at);
}
int k_preset_load(KApp *a, int t, const uint8_t *buf, size_t n)
{
    const int rc = k_preset_check(buf, n);
    size_t pos, end;
    int i, j;
    if (!a || t < 0 || t >= K_TRACKS) return K_SONG_ERR_ARG;
    if (rc != K_SONG_OK) return rc;
    end = HDR + r16(buf + 6);
    for (i = 0; i < NPAGES; i++) if (sound_mask() >> i & 1u)   /* the sound's pages from their defaults, then what the preset has */
        for (j = 0; j < pcount(PAGES[i].page); j++) a->params[t][PAGES[i].page][j] = pdef(PAGES[i].page, j);
    for (pos = HDR; pos < end;) {
        const uint8_t *d = buf + pos + CH_HDR;
        size_t len = r16(buf + pos + 4);
        uint32_t tag = TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]);
        if (tag == TAG('T', 'R', 'A', 'K')) load_trak_to(a, t, d, len, 0);
        else if (tag == TAG('K', 'I', 'T', ' ') && len >= K_LANES * 9) {
            int l;
            const int nv = len >= K_LANES * K_LANE_VALS ? K_LANE_VALS : 9;   /* (a preset from before PAN: centred) */
            for (l = 0; l < K_LANES; l++) for (j = 0; j < K_LANE_VALS; j++)
                a->lane_p[t][l][j] = j < nv ? clampi(d[l * nv + j], 0, !j ? K_DRUM_PIECES - 1 : j >= K_LANE_ENV ? 100 : 127) : 50;
        } else if (tag == TAG('P', 'R', 'M', 'S')) load_prms_to(a, t, d, len);
        pos += CH_HDR + len;
    }
    return K_SONG_OK;
}

/* ---------------------------------------------------------------- names --- */
int k_file_info(const uint8_t *buf, size_t n, char *name, uint32_t *engine)
{
    long end = check_file(buf, n, buf && n >= 4 ? (char)buf[3] : 'G');
    size_t pos;
    if (end < 0) return (int)end;
    if (buf[3] != 'G' && buf[3] != 'P') return K_SONG_ERR_MAGIC;
    if (name) name[0] = 0;
    if (engine) *engine = 0;
    for (pos = HDR; pos < (size_t)end; pos += CH_HDR + r16(buf + pos + 4)) {
        uint32_t tag = TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]);
        size_t len = r16(buf + pos + 4);
        if (tag == TAG('N', 'A', 'M', 'E') && name) load_name(name, buf + pos + CH_HDR, len);
        else if (tag == TAG('T', 'R', 'A', 'K') && engine && !*engine && len >= 9) *engine = song_uid(r32(buf + pos + CH_HDR + 1));   /* (an old id: as it is now) */
    }
    return K_SONG_OK;
}
size_t k_file_rename(uint8_t *buf, size_t n, size_t cap, const char *name)
{
    long end = check_file(buf, n, buf && n >= 4 ? (char)buf[3] : 'G');
    size_t pos, o, nl = (size_t)name_len(name), out;
    int i;
    if (end < 0 || (buf[3] != 'G' && buf[3] != 'P')) return 0;
    for (pos = HDR, o = 0; pos < (size_t)end; pos += CH_HDR + r16(buf + pos + 4))   /* the size without the old NAME */
        if (TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]) == TAG('N', 'A', 'M', 'E')) o += CH_HDR + r16(buf + pos + 4);
    out = (size_t)end - o + (nl ? CH_HDR + nl : 0) + CRCLEN;
    if (out > cap) return 0;
    for (pos = HDR; pos < (size_t)end;) {   /* drop the old NAME in place */
        size_t len = CH_HDR + r16(buf + pos + 4);
        if (TAG(buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]) == TAG('N', 'A', 'M', 'E')) {
            memmove(buf + pos, buf + pos + len, (size_t)end - pos - len);
            end -= (long)len;
        } else pos += len;
    }
    if (nl) {   /* the new one first, after the header */
        memmove(buf + HDR + CH_HDR + nl, buf + HDR, (size_t)end - HDR);
        buf[HDR] = 'N'; buf[HDR + 1] = 'A'; buf[HDR + 2] = 'M'; buf[HDR + 3] = 'E';
        buf[HDR + 4] = (uint8_t)nl; buf[HDR + 5] = 0;
        for (i = 0; i < (int)nl; i++) buf[HDR + CH_HDR + i] = (uint8_t)name_ch((unsigned char)name[i]);
        end += (long)(CH_HDR + nl);
    }
    {
        size_t plen = (size_t)end - HDR;
        uint16_t c;
        buf[6] = (uint8_t)plen; buf[7] = (uint8_t)(plen >> 8);
        c = crc16(buf, (size_t)end);
        buf[end] = (uint8_t)c; buf[end + 1] = (uint8_t)(c >> 8);
    }
    return (size_t)end + CRCLEN;
}
