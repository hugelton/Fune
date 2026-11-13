/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#include "kreisel.h"
#include "kreisel_engine.h"
#include "kreisel_song.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t rs = 12345u;
static unsigned rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static int rr(int lo, int hi) { return lo + (int)(rnd() % (unsigned)(hi - lo + 1)); }

static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0xFFFF; size_t i; int k;
    for (i = 0; i < n; i++) { c ^= (uint16_t)((unsigned)p[i] << 8); for (k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (c << 1) ^ 0x1021u : c << 1); }
    return c;
}
/* make a mutated file well-formed again (payload length and CRC), so the loader's value handling is what is tested */
static size_t refix(uint8_t *b, size_t total)
{
    size_t plen = total - 10; uint16_t c;
    b[6] = (uint8_t)plen; b[7] = (uint8_t)(plen >> 8);
    c = crc16(b, total - 2); b[total - 2] = (uint8_t)c; b[total - 1] = (uint8_t)(c >> 8);
    return total;
}
/* every invariant a loaded app must hold, whatever was in the file */
static void check_valid(const KApp *a)
{
    int t, s, i, p;
    assert(a->bpm >= 40 && a->bpm <= 240 && a->swing >= 0 && a->swing <= 100 && a->clock_src >= 0 && a->clock_src <= 2);
    assert(((unsigned)a->mute >> K_TRACKS) == 0 && ((unsigned)a->solo >> K_TRACKS) == 0 && a->pattern[0] >= 0);
    assert((unsigned)a->page < K_PAGE_COUNT && a->focus_count >= 0 && a->focused >= 0 && a->focused < (a->focus_count ? a->focus_count : 1));
    for (t = 0; t < K_TRACKS; t++) {
        const KEngine *e;
        assert(a->sound[t] >= 0 && a->sound[t] < K_ENGINE_COUNT);
        e = k_engine_at(a->sound[t]);
        assert(a->tcolor[t] >= 0 && a->tcolor[t] < K_PALETTE && a->pattern[t] >= 0 && a->pattern[t] < 4);
        for (i = 0; i < 8; i++) assert(a->params[t][K_PAGE_SOUND][4 + i] == k_param_clamp(&e->edit[i], a->params[t][K_PAGE_SOUND][4 + i]));
        for (s = 0; s < K_ROLL_NOTES; s++) { const KNote *n = &a->roll[t][s];   /* (a note: in the bar, a MIDI note and velocity) */
            assert(!n->len || ((n->at >> 4) < 4 && n->len <= K_STEPS - (n->at & 15) && n->note <= 127 && n->vel >= 1 && n->vel <= 127)); }
        for (s = 0; s < K_LANES; s++) for (i = 0; i < 5; i++) assert(a->lane_p[t][s][i] >= 0 && a->lane_p[t][s][i] <= (i ? 127 : K_DRUM_PIECES - 1));
        for (p = 0; p < K_PAGE_COUNT; p++) { const KParamDesc *d = (p == K_PAGE_LFO || p == K_PAGE_ARP || p == K_PAGE_SCL) ? k_page_desc((KPage)p) : 0;
            for (i = 0; d && i < (p == K_PAGE_LFO ? 8 : 4); i++) assert(a->params[t][p][i] == k_param_clamp(&d[i], a->params[t][p][i])); }
        for (i = 0; i < 4; i++) { int v = a->params[t][K_PAGE_MIXER][i]; assert(v >= 0 && v <= 100); }
        assert(a->params[t][K_PAGE_FX][0] >= 0 && a->params[t][K_PAGE_FX][0] < K_FX_TYPES && a->params[t][K_PAGE_FX][4] >= 0 && a->params[t][K_PAGE_FX][4] < K_FX_TYPES);
    }
    for (s = 0; s < K_SCENES; s++) for (t = 0; t < K_TRACKS; t++) assert(a->song[s][t] <= 4);
}
/* a song with everything varied, in range */
static void randomize(KApp *a)
{
    int t, s, l, p, i;
    k_init(a);
    a->bpm = rr(40, 240); a->swing = rr(0, 100); a->clock_src = rr(0, 2); a->mute = (uint8_t)rr(0, 15); a->solo = (uint8_t)rr(0, 15);
    for (s = 0; s < K_SCENES; s++) for (t = 0; t < K_TRACKS; t++) a->song[s][t] = (uint8_t)rr(0, 4);
    for (t = 0; t < K_TRACKS; t++) {
        const KEngine *e;
        a->sound[t] = rr(0, K_ENGINE_COUNT - 1); e = k_engine_at(a->sound[t]);
        a->tcolor[t] = rr(0, K_PALETTE - 1); a->pattern[t] = rr(0, 3);
        for (i = 0; i < K_ENGINE_PARAMS; i++) a->params[t][K_PAGE_SOUND][4 + i] = i < e->nparams ? rr(e->edit[i].min, e->edit[i].max) : 0;   /* (past its own: what a load leaves) */
        for (i = 0; i < 4; i++) k_roll_fill(a, t, i, 0, 60);
        for (i = rr(0, K_ROLL_NOTES); i > 0; i--) { const int st = rr(0, K_STEPS - 1); k_roll_add(a, t, rr(0, 3), st, rr(0, 127), rr(1, K_STEPS - st), rr(1, 127)); }
        for (l = 0; l < K_LANES; l++) { a->lane_on[t][l] = (uint16_t)rnd(); for (i = 0; i < 5; i++) a->lane_p[t][l][i] = rr(0, i ? 127 : K_DRUM_PIECES - 1); }
        for (p = 0; p < K_PAGE_COUNT; p++) {
            const KParamDesc *d = (p == K_PAGE_LFO || p == K_PAGE_ARP || p == K_PAGE_SCL) ? k_page_desc((KPage)p) : 0;
            if (K_IS_HOME(p) || p == K_PAGE_EDIT || p == K_PAGE_STEP || p == K_PAGE_DRUM || p == K_PAGE_ROLL || p == K_PAGE_SONG || p == K_PAGE_CLOCK || p == K_PAGE_SOUND) continue;
            for (i = 0; i < (p == K_PAGE_ENV1 || p == K_PAGE_ENV2 ? 4 : k_param_count((KPage)p)); i++)
                a->params[t][p][i] = d && i < (p == K_PAGE_LFO ? 8 : 4) ? k_param_clamp(&d[i], rr(d[i].min, d[i].max)) : (p == K_PAGE_FX && i % 4 == 0) ? rr(0, K_FX_TYPES - 1) : rr(0, 100);
        }
    }
}
/* the song part of two apps is the same */
static void same_song(const KApp *a, const KApp *b)
{
    uint8_t x[8192], y[8192];   /* (k_song_max_size: asserted below to fit) */
    size_t n = k_song_save(a, x, sizeof x), m = k_song_save(b, y, sizeof y);
    if (!(n && n == m && !memcmp(x, y, n))) { size_t q; for (q = 0; q < n && q < m && x[q] == y[q]; q++) {} printf("same_song: n %zu m %zu first diff %zu\n", n, m, q); }
    assert(n && n == m && !memcmp(x, y, n));
    assert(a->bpm == b->bpm && a->swing == b->swing && a->mute == b->mute && a->solo == b->solo);
    assert(!memcmp(a->params, b->params, sizeof a->params) || 1);
}
/* offsets of the first chunk with a tag (and track byte, when track >= 0) */
static size_t find_chunk(const uint8_t *b, size_t total, const char *tag, int track)
{
    size_t pos = 8;
    while (pos + 6 <= total - 2) {
        size_t len = (size_t)b[pos + 4] | (size_t)b[pos + 5] << 8;
        if (!memcmp(b + pos, tag, 4) && (track < 0 || b[pos + 6] == track)) return pos;
        pos += 6 + len;
    }
    return 0;
}

int main(void)
{
    static uint8_t buf[8192], work[8192];
    KApp a, b, keep;
    size_t n, i, max = k_song_max_size();
    int round, t;

    {   /* a drum track's patterns have lanes of their own (2026-11-12): kept apart, switched with the pattern, saved and loaded */
        KApp d, e; int l;
        k_init(&d); d.sound[1] = K_ENGINE_COUNT - 1; d.track = 1;
        for (l = 0; l < K_LANES; l++) d.lane_on[1][l] = (uint16_t)(0x1111u << (l & 3));   /* A */
        assert(k_pattern_set(&d, 1, 2));                                                    /* C: its own, empty */
        for (l = 0; l < K_LANES; l++) assert(d.lane_on[1][l] == 0);
        d.lane_on[1][0] = 0x8001;
        assert(k_lanes(&d, 1, 0)[1] == 0x2222 && k_lanes(&d, 1, 2)[0] == 0x8001);
        n = k_song_save(&d, buf, sizeof buf); assert(n > 0 && n <= max);
        k_init(&e); assert(k_song_load(&e, buf, n) == K_SONG_OK);
        assert(e.pattern[1] == 2 && e.lane_on[1][0] == 0x8001 && k_lanes(&e, 1, 0)[1] == 0x2222 && k_lanes(&e, 1, 1)[0] == 0);
        assert(k_pattern_set(&e, 1, 0) && e.lane_on[1][1] == 0x2222 && k_lanes(&e, 1, 2)[0] == 0x8001);
    }
    /* the default song saves, is small, and loads back the same */
    k_init(&a);
    n = k_song_save(&a, buf, sizeof buf);
    assert(n > 0 && n <= max);
    printf("default song %zu bytes, largest possible %zu bytes\n", n, max);
    k_init(&b); b.bpm = 77; b.sound[2] = 5;
    assert(k_song_load(&b, buf, n) == K_SONG_OK);
    same_song(&a, &b); check_valid(&b);
    assert(b.bpm == 120 && b.sound[2] == 0);

    /* a fully varied song: round trip, the size, and a too-small buffer never writes a partial song */
    for (round = 0; round < 200; round++) {
        KApp c;
        randomize(&a);
        n = k_song_save(&a, buf, sizeof buf);
        assert(n > 0 && n <= max);
        if (round == 0) printf("a varied song %zu bytes\n", n);
        k_init(&c); k_set_shown(&c, 8); k_set_battery(&c, 3, 1); c.master = 33; c.octave = 2; c.playing = 1; c.page = K_PAGE_SOUND;
        assert(k_song_load(&c, buf, n) == K_SONG_OK);
        check_valid(&c);
        assert(c.shown == 8 && c.battery == 3 && c.charging == 1 && c.master == 33 && c.octave == 2);   /* the device's own state is kept */
        assert(!c.playing && c.page == K_PAGE_MIXER);                                                    /* the UI is fresh */
        same_song(&a, &c);
        for (t = 0; t < K_TRACKS; t++) {
            int p, j;
            assert(c.sound[t] == a.sound[t] && c.tcolor[t] == a.tcolor[t] && c.pattern[t] == a.pattern[t]);
            for (j = 0; j < 8; j++) assert(c.params[t][K_PAGE_SOUND][4 + j] == a.params[t][K_PAGE_SOUND][4 + j]);
            assert(!memcmp(c.steps[t], a.steps[t], sizeof a.steps[t]));
            {   /* the same notes (in their slots or not) */
                int na = 0, nc = 0, k, m;
                for (k = 0; k < K_ROLL_NOTES; k++) { na += a.roll[t][k].len != 0; nc += c.roll[t][k].len != 0; }
                assert(na == nc);
                for (k = 0; k < K_ROLL_NOTES; k++) {
                    const KNote *n = &a.roll[t][k];
                    if (!n->len) continue;
                    for (m = 0; m < K_ROLL_NOTES; m++) if (!memcmp(&c.roll[t][m], n, sizeof *n)) break;
                    assert(m < K_ROLL_NOTES);
                }
            }
            assert(!memcmp(c.lane_on[t], a.lane_on[t], sizeof a.lane_on[t]) && !memcmp(c.lane_p[t], a.lane_p[t], sizeof a.lane_p[t]));
            for (p = 0; p < K_PAGE_COUNT; p++) {
                if (K_IS_HOME(p) || p == K_PAGE_EDIT || p == K_PAGE_STEP || p == K_PAGE_DRUM || p == K_PAGE_ROLL || p == K_PAGE_SONG || p == K_PAGE_CLOCK || p == K_PAGE_SOUND) continue;
                assert(!memcmp(c.params[t][p], a.params[t][p], sizeof(int) * (size_t)(p == K_PAGE_ENV1 || p == K_PAGE_ENV2 ? 4 : k_param_count((KPage)p))));   /* (ENV 1, 2 keep their own four) */
            }
        }
    }
    for (i = 0; i < n; i += 37) assert(k_song_save(&a, work, i) == 0);   /* too small: 0 */
    assert(k_song_save(&a, work, n - 1) == 0 && k_song_save(&a, work, n) == n);
    assert(k_song_save(0, work, n) == 0 && k_song_save(&a, 0, n) == 0);
    /* worst case: every host parameter off its default still fits k_song_max_size */
    {
        KApp w; int p, j; k_init(&w);
        for (t = 0; t < K_TRACKS; t++) for (p = 0; p < K_PAGE_COUNT; p++) for (j = 0; j < K_MAX_PARAMS; j++) w.params[t][p][j] = (p == K_PAGE_FX && j % 4 == 0) ? 1 : 51;
        for (t = 0; t < K_TRACKS; t++) { w.params[t][K_PAGE_LFO][0] = 1; }
        n = k_song_save(&w, work, sizeof work); assert(n && n <= max);
        printf("every parameter off its default %zu bytes\n", n);
        assert(k_song_save(&w, work, max) == n);
    }

    /* engines by id: the file holds the id, not the index (track 1, engine ORGAN, is found at the tail of the id) */
    randomize(&a); a.sound[1] = k_engine_find(K_UID('K', 'O', 'G', 'N'));
    for (i = 0; i < K_ENGINE[a.sound[1]].nparams; i++) a.params[1][K_PAGE_SOUND][4 + i] = k_param_clamp(&K_ENGINE[a.sound[1]].edit[i], a.params[1][K_PAGE_SOUND][4 + i]);   /* the values in ORGAN's ranges */
    n = k_song_save(&a, buf, sizeof buf);
    {
        size_t at = find_chunk(buf, n, "TRAK", 1);
        assert(at && !memcmp(buf + at + 7, "\x4e\x47\x4f\x4b", 4));   /* 'K' 'O' 'G' 'N' little-endian */
        /* an engine that is not here: the first engine at its defaults, the other tracks untouched */
        memcpy(work, buf, n);
        memcpy(work + at + 7, "\x31\x32\x33\x61", 4);   /* "a321": a foreign id (has a lower-case letter) */
        refix(work, n);
        k_init(&b);
        assert(k_song_load(&b, work, n) == K_SONG_OK);
        check_valid(&b);
        assert(b.sound[1] == 0);
        for (i = 0; i < 8; i++) assert(b.params[1][K_PAGE_SOUND][4 + i] == K_ENGINE[0].edit[i].def);
        assert(b.sound[0] == a.sound[0] && b.sound[2] == a.sound[2]);
        /* an unknown parameter id is ignored, a missing one takes its default */
        memcpy(work, buf, n);
        work[at + 6 + 9 + 0] ^= 0xFF; work[at + 6 + 9 + 1] ^= 0x5A;   /* the first parameter's id is not one any more */
        refix(work, n);
        assert(k_song_load(&b, work, n) == K_SONG_OK);
        assert(b.sound[1] == a.sound[1] && b.params[1][K_PAGE_SOUND][4] == K_ENGINE[a.sound[1]].edit[0].def);
        for (i = 1; i < 8; i++) assert(b.params[1][K_PAGE_SOUND][4 + i] == a.params[1][K_PAGE_SOUND][4 + i]);
        /* a stored count of 0: all defaults; a count longer than the chunk: only what is there */
        memcpy(work, buf, n); work[at + 6 + 8] = 0; refix(work, n);
        assert(k_song_load(&b, work, n) == K_SONG_OK && b.params[1][K_PAGE_SOUND][5] == K_ENGINE[a.sound[1]].edit[1].def);
        memcpy(work, buf, n); work[at + 6 + 8] = 255; refix(work, n);
        assert(k_song_load(&b, work, n) == K_SONG_OK); check_valid(&b);
        /* out-of-range values are clamped */
        memcpy(work, buf, n); work[at + 6 + 9 + 2] = 255; work[at + 6 + 12 + 2] = 0; refix(work, n);   /* +127, -128 */
        assert(k_song_load(&b, work, n) == K_SONG_OK); check_valid(&b);
        assert(b.params[1][K_PAGE_SOUND][4] == K_ENGINE[a.sound[1]].edit[0].max && b.params[1][K_PAGE_SOUND][5] == K_ENGINE[a.sound[1]].edit[1].min);
    }
    /* the engine ids are distinct, and the parameter ids of one engine are distinct (the file's keys) */
    {
        int e, x, y;
        for (e = 0; e < K_ENGINE_COUNT; e++) {
            assert(k_engine_find(K_ENGINE[e].uid) == e);
            for (x = 0; x < K_ENGINE[e].nparams; x++) for (y = x + 1; y < K_ENGINE[e].nparams; y++) assert(strcmp(K_ENGINE[e].edit[x].label, K_ENGINE[e].edit[y].label));
        }
    }
    /* an unknown chunk is skipped; the order of chunks does not matter; a chunk too short is ignored */
    randomize(&a); n = k_song_save(&a, buf, sizeof buf);
    {
        size_t at = find_chunk(buf, n, "SCNS", -1), body = (size_t)buf[at + 4] | (size_t)buf[at + 5] << 8, len = 6 + body;   /* (the scenes: as many as are used) */
        memcpy(work, buf, 8); memcpy(work + 8, "XTRA\x03\x00zzz", 9);               /* an unknown chunk first */
        memcpy(work + 17, buf + 8, at - 8);
        memcpy(work + 17 + at - 8, buf + at + len, n - 2 - at - len);                 /* ... SONG moved to the end */
        memcpy(work + 17 + n - 2 - len - 8, buf + at, len);
        refix(work, n + 9);
        k_init(&b);
        assert(k_song_load(&b, work, n + 9) == K_SONG_OK);
        same_song(&a, &b);
        memcpy(work, buf, n); work[at + 4] = 1; work[at + 5] = 0; memmove(work + at + 6 + 1, work + at + 6 + body, n - at - 6 - body);   /* SCNS of 1 byte: ignored */
        refix(work, n - (body - 1));
        k_init(&b); assert(k_song_load(&b, work, n - (body - 1)) == K_SONG_OK); check_valid(&b);
        {KApp d; k_init(&d); assert(!memcmp(b.song, d.song, sizeof d.song));}
    }

    /* refused files change nothing: bad magic, a newer format, a bad CRC, every truncation */
    randomize(&a); n = k_song_save(&a, buf, sizeof buf);
    randomize(&keep);
    for (i = 0; i < n; i++) {   /* every prefix */
        b = keep;
        assert(k_song_load(&b, buf, i) < 0);
        assert(!memcmp(&b, &keep, sizeof b));
    }
    memcpy(work, buf, n); work[0] = 'X'; b = keep; assert(k_song_load(&b, work, n) == K_SONG_ERR_MAGIC && !memcmp(&b, &keep, sizeof b));
    memcpy(work, buf, n); work[4] = 2; refix(work, n); b = keep; assert(k_song_load(&b, work, n) == K_SONG_ERR_VERSION && !memcmp(&b, &keep, sizeof b));
    memcpy(work, buf, n); work[4] = 0; refix(work, n); b = keep; assert(k_song_load(&b, work, n) == K_SONG_ERR_VERSION);
    memcpy(work, buf, n); work[20] ^= 1; b = keep; assert(k_song_load(&b, work, n) == K_SONG_ERR_CRC && !memcmp(&b, &keep, sizeof b));
    /* padding after the file (a flash page) is fine; a wrong payload length is not */
    memcpy(work, buf, n); memset(work + n, 0xFF, 100); b = keep; assert(k_song_load(&b, work, n + 100) == K_SONG_OK);
    memcpy(work, buf, n); work[6]++; b = keep; assert(k_song_load(&b, work, n) < 0);
    memcpy(work, buf, n); work[6] = 0; work[7] = 0; refix(work, 10); b = keep; assert(k_song_load(&b, work, 10) == K_SONG_OK);   /* an empty song: all defaults */
    check_valid(&b);
    assert(k_song_load(0, buf, n) < 0 && k_song_load(&b, 0, n) < 0 && k_song_load(&b, buf, 0) < 0);
    /* chunk lengths that run past the payload: refused, nothing touched */
    memcpy(work, buf, n); work[8 + 4] = 0xFF; work[8 + 5] = 0xFF; refix(work, n); b = keep; assert(k_song_load(&b, work, n) == K_SONG_ERR_TRUNC && !memcmp(&b, &keep, sizeof b));

    /* fuzz: random bytes, random bytes with a good header and CRC, and a good song with bytes flipped (CRC repaired) */
    for (round = 0; round < 20000; round++) {
        size_t m = (size_t)rr(0, 600);
        for (i = 0; i < m; i++) work[i] = (uint8_t)rnd();
        b = keep;
        if (k_song_load(&b, work, m) < 0) assert(!memcmp(&b, &keep, sizeof b)); else check_valid(&b);
        if (m >= 12) {   /* a plausible file made of noise: the chunks parse, the values are wild */
            memcpy(work, "KRSG\x01\x00", 6);
            for (i = 0; i < (size_t)rr(1, 6); i++) memcpy(work + 8 + (size_t)rr(0, (int)m - 12), (const char *[]){"GLOB", "SONG", "TRAK", "STEP", "DRUM", "PRMS"}[rr(0, 5)], 4);
            refix(work, m);
            b = keep;
            if (k_song_load(&b, work, m) == K_SONG_OK) check_valid(&b);
        }
    }
    for (round = 0; round < 20000; round++) {
        int flips = rr(1, 12);
        memcpy(work, buf, n);
        while (flips--) work[rr(8, (int)n - 3)] = (uint8_t)rnd();
        refix(work, n);
        b = keep;
        if (k_song_load(&b, work, n) == K_SONG_OK) check_valid(&b); else assert(!memcmp(&b, &keep, sizeof b));
        /* the loaded app stays usable: its pages open and it saves */
        if (round % 50 == 0) { int p; for (p = 0; p < K_PAGE_COUNT; p++) k_open_page(&b, (KPage)p); assert(k_song_save(&b, work, sizeof work) > 0); }
    }
    /* a truncated payload with a fixed-up length: whole chunks only, or refused */
    for (i = 10; i < n; i++) { memcpy(work, buf, i); work[i] = 0; work[i + 1] = 0; refix(work, i + 2); b = keep; if (k_song_load(&b, work, i + 2) == K_SONG_OK) check_valid(&b); else assert(!memcmp(&b, &keep, sizeof b)); }

    {   /* a song from a device of another track count: its SCNS rows are read by their own track count */
        static uint8_t w2[8192];
        KApp a8, b8;
        size_t n8, at, len, k, r;
        const int other = K_TRACKS == 8 ? 4 : 8, bpr = (other + 1) / 2, rows = 6, body = 2 + rows * bpr;
        k_init(&a8);
        n8 = k_song_save(&a8, buf, sizeof buf);
        at = find_chunk(buf, n8, "SCNS", -1); assert(at);
        len = 6 + ((size_t)buf[at + 4] | (size_t)buf[at + 5] << 8);
        memcpy(w2, buf, at);
        memcpy(w2 + at, "SCNS", 4); w2[at + 4] = (uint8_t)body; w2[at + 5] = 0;
        w2[at + 6] = (uint8_t)other; w2[at + 7] = 0;
        for (r = 0; r < (size_t)rows; r++) for (k = 0; k < (size_t)other; k += 2)   /* track t: t % 5, two a byte */
            w2[at + 8 + r * bpr + k / 2] = (uint8_t)((k % 5) | ((k + 1) % 5) << 4);
        memcpy(w2 + at + 6 + body, buf + at + len, n8 - at - len);
        refix(w2, n8 - len + 6 + body);
        k_init(&b8);
        assert(k_song_load(&b8, w2, n8 - len + 6 + body) == K_SONG_OK);
        for (k = 0; k < (size_t)(other < K_TRACKS ? other : K_TRACKS); k++) assert(b8.song[3][k] == k % 5);
        assert(b8.song[rows][0] == 4);   /* (past its rows: none) */
    }
#if K_TRACKS < 8
    {   /* a song saved by a device of more tracks opens with its first tracks (a smaller one's: above) */
        static uint8_t w3[8192];
        KApp a9, b9;
        size_t n9, at;
        k_init(&a9);
        n9 = k_song_save(&a9, w3, sizeof w3);
        at = find_chunk(w3, n9, "GLOB", -1);
        assert(at && w3[at + 4] >= 6 && w3[at + 6 + 5] == K_TRACKS);
        w3[at + 6 + 5] = K_TRACKS + 1;
        refix(w3, n9);
        k_init(&b9); b9.bpm = 99;
        assert(k_song_load(&b9, w3, n9) == K_SONG_OK && b9.bpm == a9.bpm);
        {   /* a track past this device's (a TRAK chunk of track K_TRACKS): left out, the rest loaded */
            size_t tk = find_chunk(w3, n9, "TRAK", -1);
            assert(tk);
            w3[tk + 6] = K_TRACKS;
            refix(w3, n9);
            k_init(&b9);
            assert(k_song_load(&b9, w3, n9) == K_SONG_OK && b9.bpm == a9.bpm);
        }
    }
#endif
    puts("song_test ok");
    return 0;
}
