/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The recorder (k_rec.h). The capture thread writes the frames, then publishes their count; the main thread reads the count,
 * then the frames before it. The state, the count and the peak are atomics; nothing else is shared while a recording runs. */
#include "k_rec.h"
#include "k_wav.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int16_t *buf;
static uint32_t cap, rate;
static int16_t pre[K_REC_PREROLL];
static uint32_t pre_n;                   /* (the capture thread's) frames written into pre, all told */
static atomic_int state;
static atomic_uint count, peak;
static atomic_int live;

int k_rec_init(uint32_t r, uint32_t max_frames)
{
    if (buf && cap == max_frames) { rate = r; return 0; }
    k_rec_shutdown();
    buf = max_frames ? malloc((size_t)max_frames * sizeof *buf) : 0;
    if (max_frames && !buf) return -1;
    cap = max_frames;
    rate = r;
    return 0;
}

void k_rec_shutdown(void)
{
    atomic_store(&state, K_REC_IDLE);
    free(buf);
    buf = 0;
    cap = 0;
    atomic_store(&count, 0);
}

uint32_t k_rec_rate(void) { return rate; }
uint32_t k_rec_max(void) { return cap; }

static void level(int32_t a)
{
    unsigned p = atomic_load_explicit(&peak, memory_order_relaxed);
    if ((unsigned)a > p) atomic_store_explicit(&peak, (unsigned)a, memory_order_relaxed);
}

/* one frame, mono, from the capture thread */
static void take(int16_t s)
{
    const int st = atomic_load_explicit(&state, memory_order_acquire);
    uint32_t n;
    level(s < 0 ? -(int32_t)s : s);
    if (st == K_REC_WAIT) {
        if ((s < 0 ? -(int32_t)s : s) <= K_REC_THRESHOLD) { pre[pre_n++ % K_REC_PREROLL] = s; return; }
        {   /* the sound came: what came before it first, oldest first */
            const uint32_t k = pre_n < K_REC_PREROLL ? pre_n : K_REC_PREROLL, at = pre_n - k;
            uint32_t i;
            for (i = 0; i < k && i < cap; i++) buf[i] = pre[(at + i) % K_REC_PREROLL];
            atomic_store_explicit(&count, i, memory_order_release);
            atomic_store_explicit(&state, K_REC_ON, memory_order_release);
        }
    } else if (st != K_REC_ON) return;
    n = atomic_load_explicit(&count, memory_order_relaxed);
    if (n >= cap) { atomic_store_explicit(&state, K_REC_IDLE, memory_order_release); return; }   /* full: it stops */
    buf[n] = s;
    atomic_store_explicit(&count, n + 1, memory_order_release);
}

static int16_t clip16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

void k_rec_input(const float *in, uint32_t n, int channels)
{
    uint32_t i;
    int c;
    if (!in || channels < 1) return;
    for (i = 0; i < n; i++) {
        float s = 0;
        for (c = 0; c < channels; c++) s += in[i * (uint32_t)channels + (uint32_t)c];
        s = s / (float)channels * 32767.0f;
        take(clip16((int32_t)(s < 0 ? s - 0.5f : s + 0.5f)));
    }
}

void k_rec_input16(const int16_t *in, uint32_t n, int channels)
{
    uint32_t i;
    int c;
    if (!in || channels < 1) return;
    for (i = 0; i < n; i++) {
        int32_t s = 0;
        for (c = 0; c < channels; c++) s += in[i * (uint32_t)channels + (uint32_t)c];
        take(clip16(s / channels));
    }
}

int k_rec_start(int wait)
{
    if (!buf || !cap) return -1;
    atomic_store(&state, K_REC_IDLE);
    pre_n = 0;
    atomic_store(&count, 0);
    atomic_store_explicit(&state, wait ? K_REC_WAIT : K_REC_ON, memory_order_release);
    return 0;
}

void k_rec_stop(void) { atomic_store(&state, K_REC_IDLE); }
int k_rec_state(void) { return atomic_load_explicit(&state, memory_order_acquire); }
uint32_t k_rec_frames(void) { return atomic_load_explicit(&count, memory_order_acquire); }
const int16_t *k_rec_data(void) { return buf; }

int k_rec_level(void)
{
    const unsigned p = atomic_load_explicit(&peak, memory_order_relaxed);
    atomic_store_explicit(&peak, p * 3u / 4u, memory_order_relaxed);   /* (falls back as it is read: a meter's release) */
    return (int)(p > 32767u ? 32767u : p);
}

int k_rec_save(const char *path)
{
    const uint32_t n = k_rec_frames();
    if (!n) return K_WAV_ERR_TRUNCATED;
    return k_wav_write(path, buf, n, 1, rate);
}

void k_rec_set_live(int on) { atomic_store(&live, on != 0); }
int k_rec_live(void) { return atomic_load(&live); }

int k_rec_normalize(int max_db)
{
    static const uint16_t DB_Q8[25] = {256, 287, 322, 362, 406, 456, 511, 573, 643, 722, 810, 909, 1020, 1144, 1284, 1440,
                                       1616, 1813, 2035, 2283, 2561, 2874, 3224, 3618, 4059};   /* 10^(dB/20), Q8, 0 .. 24 dB */
    const uint32_t n = k_rec_frames();
    int32_t pk = 0, g;
    uint32_t i;
    int db = 0;
    if (!buf || !n || k_rec_state() != K_REC_IDLE) return 0;
    if (max_db > 24) max_db = 24;
    for (i = 0; i < n; i++) { const int32_t a = buf[i] < 0 ? -(int32_t)buf[i] : buf[i]; if (a > pk) pk = a; }
    if (!pk) return 0;
    while (db < max_db && (pk * (int32_t)DB_Q8[db + 1] >> 8) <= 29204) db++;   /* (the peak under -1 dBFS) */
    if (!db) return 0;
    g = DB_Q8[db];
    for (i = 0; i < n; i++) buf[i] = clip16((buf[i] * g) >> 8);
    return db;
}

uint32_t k_rec_trim(int threshold)
{
    const uint32_t n = k_rec_frames();
    uint32_t i, cut;
    if (!buf || !n || k_rec_state() != K_REC_IDLE) return 0;
    for (i = 0; i < n && (buf[i] < 0 ? -(int32_t)buf[i] : buf[i]) <= threshold; i++) {}
    if (i == n) return 0;   /* (nothing over it: kept as it is) */
    cut = i > K_REC_PREROLL ? i - K_REC_PREROLL : 0;
    if (!cut) return 0;
    memmove(buf, buf + cut, (size_t)(n - cut) * sizeof *buf);
    atomic_store(&count, n - cut);
    return cut;
}
