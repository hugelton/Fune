/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * The Sampler's file browser (k_browse.h). */
#include "k_browse.h"
#include "k_wav.h"
#include <stdio.h>
#include <string.h>

static char names[K_BROWSE_MAX][K_SMP_NAME];
static uint32_t lens[K_BROWSE_MAX];
static char was[K_SMP_NAME];
static int count, sel = -1, track = -1;

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

int k_wav_info(const char *path, uint32_t *frames, uint32_t *rate, int *channels)
{
    uint8_t h[12], c[8], f[40];
    FILE *fp = fopen(path, "rb");
    uint32_t fr = 0, ch = 0, bits = 0, fmt = 0, data = 0;
    int have_fmt = 0;
    if (!fp) return K_WAV_ERR_IO;
    if (fread(h, 1, 12, fp) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(fp); return K_WAV_ERR_NOT_WAVE; }
    for (;;) {   /* the chunks up to "data": its size is all that is read of it */
        uint32_t n;
        if (fread(c, 1, 8, fp) != 8) { fclose(fp); return K_WAV_ERR_TRUNCATED; }
        n = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            const uint32_t k = n < sizeof f ? n : (uint32_t)sizeof f;
            if (n < 16 || fread(f, 1, k, fp) != k) { fclose(fp); return K_WAV_ERR_TRUNCATED; }
            fmt = rd16(f); ch = rd16(f + 2); fr = rd32(f + 4); bits = rd16(f + 14);
            if (fmt == 0xFFFE && k >= 26) fmt = rd16(f + 24);   /* EXTENSIBLE: the sub-format's first two bytes */
            have_fmt = 1;
            n -= k;
        } else if (!memcmp(c, "data", 4)) {
            data = n;
            break;
        }
        if (fseek(fp, (long)(n + (n & 1u)), SEEK_CUR) != 0) { fclose(fp); return K_WAV_ERR_TRUNCATED; }
    }
    fclose(fp);
    if (!have_fmt) return K_WAV_ERR_TRUNCATED;
    if (!((fmt == 1 && (bits == 16 || bits == 24)) || (fmt == 3 && bits == 32))) return K_WAV_ERR_FORMAT;
    if (ch < 1 || ch > 2) return K_WAV_ERR_CHANNELS;
    if (frames) *frames = data / (ch * (bits / 8));
    if (rate) *rate = fr;
    if (channels) *channels = (int)ch;
    return K_WAV_OK;
}

int k_browse_open(int t)
{
    int i;
    char full[1100];
    track = t;
    snprintf(was, sizeof was, "%s", k_smp_path(t));
    count = k_smp_list(names, K_BROWSE_MAX);
    sel = count ? 0 : -1;
    for (i = 0; i < count; i++) {
        uint32_t fr = 0, r = 0;
        lens[i] = k_smp_full(names[i], full, sizeof full) && k_wav_info(full, &fr, &r, 0) == K_WAV_OK && r ? (uint32_t)((uint64_t)fr * 1000u / r) : 0;
        if (!strcmp(names[i], was)) sel = i;
    }
    return count;
}

void k_browse_close(int keep)
{
    if (track < 0) return;
    if (!keep && strcmp(k_smp_path(track), was) != 0) k_smp_assign(track, was);
    track = -1;
    sel = -1;
}

int k_browse_is_open(void) { return track >= 0; }
int k_browse_track(void) { return track; }
int k_browse_count(void) { return track >= 0 ? count : 0; }
int k_browse_sel(void) { return track >= 0 ? sel : -1; }
const char *k_browse_name(int i) { return track >= 0 && i >= 0 && i < count ? names[i] : 0; }
uint32_t k_browse_ms(int i) { return track >= 0 && i >= 0 && i < count ? lens[i] : 0; }

int k_browse_pick(int i)
{
    if (track < 0 || i < 0 || i >= count) return -1;
    sel = i;
    return strcmp(k_smp_path(track), names[i]) ? k_smp_assign(track, names[i]) : 0;
}

int k_browse_move(int d)
{
    int i;
    if (track < 0 || !count) return -1;
    i = sel + d;
    return k_browse_pick(i < 0 ? 0 : i >= count ? count - 1 : i);
}
