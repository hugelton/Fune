/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Kreisel's SAMPLER (shown "Sampler"): one recorded sound across the keys. Linux and macOS only (K_WITH_SAMPLES): the FM-1 has
 * no room for samples, and this file is not in its build. The sound is the track's sample (src/sample/k_smp.h), handed to the
 * audio by k_audio_sync; a song keeps its path, never its frames. It plays from START to END, forward or reversed, once or in a
 * loop (forward, or back and forth from LPST to END), at the key's pitch over ROOT (KEY off: always at ROOT's); then the SVF
 * of RETRO (its three taps), the host's ENV 1 (the amplitude, and to the filter with ENV), LEVEL and VEL as the other engines.
 * Its parameters, on P_E0 ..:
 *   SOUND  0 STRT, 1 END, 2 LOOP (OFF FWD PING), 3 LPST (of START .. END) | 4 ROOT, 5 TUNE (cents), 6 DIR (FWD REV), 7 KEY
 *   EDIT   8 CUT, 9 RES, 10 TYPE (LP BP HP), 11 ENV (ENV 1 -> the filter, the host's), 12 LEVEL, 13 VEL
 * The voice: its place in the sample as frames << 16 (s[0] the frame, ph[0] the fraction), s[1] s[2] the filter, s[3] the way
 * it goes (+1 / -1), s[4] 1 once it has run out. Integer only, as every engine (the position's step: once a block). */
typedef struct { const int16_t *f; uint32_t n, rate; int ch; } ksmp_bind_t;
static ksmp_bind_t ksmp_b[NPART];      /* each part's sample (k_audio_sync sets it, under the platform's lock) */

typedef struct { int64_t s, e, l; int loop, rev; } ksmp_span_t;
static int ksmp_span(const track_t *t, const ksmp_bind_t *b, ksmp_span_t *r)   /* where it plays, in frames */
{
    const int16_t *p = t->p;
    const int64_t n = b->n;
    if (!b->f || n < 2) return 0;
    r->s = n * clamp(p[P_E0], 0, 127) / 128;
    r->e = n * (clamp(p[P_E1], 0, 127) + 1) / 128;
    if (r->e <= r->s + 1) r->e = r->s + 2 <= n ? r->s + 2 : n, r->s = r->e - 2;
    r->l = r->s + (r->e - r->s) * clamp(p[P_E3], 0, 127) / 128;
    if (r->l >= r->e - 1) r->l = r->e - 2;
    r->loop = clamp(p[P_E2], 0, 2);
    r->rev = p[P_E6] != 0;
    return 1;
}

static void ksmp_note_on(track_t *t, voice_t *v)
{
    ksmp_span_t r;
    const ksmp_bind_t *b = &ksmp_b[t - trk];
    int64_t at;
    v->s[1] = v->s[2] = 0;
    v->s[4] = !ksmp_span(t, b, &r);
    if (v->s[4]) return;
    at = r.rev ? r.e - 1 : r.s;
    v->s[0] = (int32_t)at;
    v->ph[0] = 0;
    v->s[3] = r.rev ? -1 : 1;
}

static inline int32_t ksmp_at(const ksmp_bind_t *b, int64_t i)   /* frame i (mono: a stereo file's two halves) */
{
    if (i < 0) i = 0;
    if (i >= (int64_t)b->n) i = (int64_t)b->n - 1;
    return b->ch == 2 ? ((int32_t)b->f[2 * i] + b->f[2 * i + 1]) >> 1 : b->f[i];
}

static void ksmp_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const ksmp_bind_t *b = &ksmp_b[t - trk];
    ksmp_span_t r;
    const int32_t root16 = clamp(p[P_E4], 0, 127) * 16, tune = clamp(p[P_E5], -99, 99), res = clamp(p[P_E9], 0, 127);
    const uint32_t ft = (uint32_t)clamp(p[P_E10], 0, 2);
    const int32_t fk = 8192 - res * 7600 / 127, lev = clamp(p[P_E12], 0, 127) * 258, vs = clamp(p[P_E13], 0, 127);
    const int32_t cut = (clamp(p[P_E8], 0, 127) << 8) + m->cutoff;
    const int32_t velc = v->vel ? (int32_t)((((127 - vs) * 127 + vs * (int32_t)v->vel) << 12) / (127 * (int32_t)v->vel)) : 4096;
    int64_t P, step, E16, L16, S16;
    int32_t ic1 = v->s[1], ic2 = v->s[2], dir = v->s[3] < 0 ? -1 : 1;
    uint32_t i, inc, inc0;
    tsvf_t flt;
    if (v->s[4] || !ksmp_span(t, b, &r)) { v->s[4] = 1; return; }
    {   /* the step a sample, frames << 16: the note over ROOT (KEY off: ROOT's own, the host's bends and glides kept), its rate */
        const int32_t p16 = p[P_E7] ? m->pitch16 : m->pitch16 - (int32_t)v->note * 16 + root16;
        inc = cents_inc(p16, tune, 0);
        if (m->fine) inc += (uint32_t)((int32_t)(inc >> 12) * m->fine);
        inc0 = pitch_inc((uint32_t)root16);
        step = (int64_t)((((uint64_t)inc << 16) / (inc0 ? inc0 : 1u)) * b->rate / FS);
        if (step > (32 << 16)) step = 32 << 16;   /* (five octaves over the file's own: enough) */
    }
    tsvf_coef(&flt, cut, res);
    P = ((int64_t)v->s[0] << 16) | (int64_t)(v->ph[0] & 0xFFFFu);
    S16 = r.s << 16; E16 = r.e << 16; L16 = (r.loop ? r.l : r.s) << 16;
    for (i = 0; i < n; i++) {
        const int64_t i0 = P >> 16;
        const int32_t fr = (int32_t)(P & 0xFFFF), a0 = ksmp_at(b, i0), a1 = ksmp_at(b, i0 + 1);
        int32_t s = a0 + (int32_t)(((int64_t)(a1 - a0) * fr) >> 16);
        s = kanalog_tap(&flt, fk, mulq15(s, lev), &ic1, &ic2, ft);
        if (velc != 4096) s = k_mulsh(s, velc, 12);
        out[i] += voice_amp(s, m, i);
        P += dir * step;
        if (dir > 0 && P >= E16) {   /* the end: round again, back, or out */
            if (r.loop == 1) { while (P >= E16) P -= E16 - L16; }
            else if (r.loop == 2) { P = 2 * E16 - P - 1; dir = -1; if (P < L16) P = L16; }
            else { v->s[4] = 1; break; }
        } else if (dir < 0 && P < L16) {   /* the start (reversed, or a loop's way back) */
            if (r.loop == 2) { P = 2 * L16 - P; dir = 1; if (P >= E16) P = E16 - 1; }
            else if (r.loop == 1) { while (P < L16) P += E16 - L16; }
            else { v->s[4] = 1; break; }
        }
        if (P < S16 && dir < 0 && !r.loop) { v->s[4] = 1; break; }
    }
    v->s[0] = (int32_t)(P >> 16);
    v->ph[0] = (uint32_t)(P & 0xFFFF);
    v->s[1] = ic1; v->s[2] = ic2; v->s[3] = dir;
}

static const engine_t ENG_KSAMPLER = {
    .name = "Sampler",
    .page_title = {"PLAY", "PITCH"},
    .note_on = ksmp_note_on,
    .render = ksmp_render,
    .sampled = 1,
};
