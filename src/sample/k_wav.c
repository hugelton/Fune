/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAV reading (k_wav.h). The chunks are walked once, remembering fmt, data and smpl; the samples are converted afterwards,
 * so the chunk order does not matter. Sizes are compared as remaining lengths, never as sums that could wrap. */
#include "k_wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FMT_PCM 1u
#define FMT_FLOAT 3u
#define FMT_EXT 0xFFFEu

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int tag(const uint8_t *p, const char *t) { return memcmp(p, t, 4) == 0; }

/* the tail of the KSDATAFORMAT_SUBTYPE GUIDs (PCM and IEEE float differ only in their first two bytes) */
static const uint8_t GUID_TAIL[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

static int16_t from24(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8;   /* sign-extended */
    v = (v + 128) >> 8;                                                                               /* rounded to 16 bits */
    return (int16_t)(v > 32767 ? 32767 : v);
}
static int16_t from_float(const uint8_t *p)
{
    uint32_t u = rd32(p);
    float f;
    memcpy(&f, &u, 4);
    if (!(f == f)) return 0;           /* NaN */
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    f *= 32767.0f;
    return (int16_t)(f < 0 ? f - 0.5f : f + 0.5f);
}

int k_wav_parse(const uint8_t *buf, size_t n, KWav *out, size_t max_bytes)
{
    const uint8_t *fmt = NULL, *data = NULL, *smpl = NULL;
    uint32_t fmt_n = 0, data_n = 0, smpl_n = 0, riff_n;
    size_t pos = 12, end;
    unsigned code, ch, bits, align;
    uint32_t rate, nfr, i;
    int16_t *fr;

    memset(out, 0, sizeof *out);
    if (!buf || n < 12 || !tag(buf, "RIFF") || !tag(buf + 8, "WAVE")) return K_WAV_ERR_NOT_WAVE;
    riff_n = rd32(buf + 4);
    if (riff_n < 4) return K_WAV_ERR_NOT_WAVE;
    end = (size_t)riff_n <= n - 8 ? (size_t)riff_n + 8 : n;   /* a RIFF size past the file is read up to the file's end */
    while (end - pos >= 8) {
        const uint8_t *h = buf + pos;
        uint32_t sz = rd32(h + 4);
        pos += 8;
        if (sz > end - pos) {
            if (tag(h, "data")) return K_WAV_ERR_TRUNCATED;
            break;                     /* a cut-off trailing chunk other than data is dropped */
        }
        if (tag(h, "fmt ") && !fmt) { fmt = buf + pos; fmt_n = sz; }
        else if (tag(h, "data") && !data) { data = buf + pos; data_n = sz; }
        else if (tag(h, "smpl") && !smpl) { smpl = buf + pos; smpl_n = sz; }
        pos += sz;
        if ((sz & 1u) && pos < end) pos++;   /* chunks are padded to an even size */
    }
    if (!fmt || !data || fmt_n < 16) return K_WAV_ERR_TRUNCATED;

    code = rd16(fmt);
    ch = rd16(fmt + 2);
    rate = rd32(fmt + 4);
    align = rd16(fmt + 12);
    bits = rd16(fmt + 14);
    if (code == FMT_EXT) {
        if (fmt_n < 40 || rd16(fmt + 16) < 22) return K_WAV_ERR_FORMAT;
        if (memcmp(fmt + 26, GUID_TAIL, sizeof GUID_TAIL) != 0) return K_WAV_ERR_FORMAT;
        code = rd16(fmt + 24);
        if (rd16(fmt + 18) && rd16(fmt + 18) != bits) return K_WAV_ERR_FORMAT;   /* valid bits other than the container's */
    }
    if (ch != 1 && ch != 2) return K_WAV_ERR_CHANNELS;
    if (!((code == FMT_PCM && (bits == 16 || bits == 24)) || (code == FMT_FLOAT && bits == 32))) return K_WAV_ERR_FORMAT;
    if (rate == 0 || align != ch * (bits / 8)) return K_WAV_ERR_FORMAT;

    nfr = data_n / align;              /* a partial last frame is dropped */
    if (nfr == 0) return K_WAV_ERR_TRUNCATED;
    if ((size_t)nfr > (SIZE_MAX / 2) / ch) return K_WAV_ERR_TOO_BIG;
    if (max_bytes && (size_t)nfr * ch * 2 > max_bytes) return K_WAV_ERR_TOO_BIG;
    fr = malloc((size_t)nfr * ch * 2);
    if (!fr) return K_WAV_ERR_NOMEM;
    for (i = 0; i < nfr * ch; i++) {
        if (bits == 16) fr[i] = (int16_t)rd16(data + (size_t)i * 2);
        else if (bits == 24) fr[i] = from24(data + (size_t)i * 3);
        else fr[i] = from_float(data + (size_t)i * 4);
    }
    out->frames = fr;
    out->nframes = nfr;
    out->rate = rate;
    out->channels = (int)ch;
    /* smpl: 36 bytes of header (the loop count at 28), then 24 bytes a loop: id, type, start, end (inclusive), fraction, count */
    if (smpl && smpl_n >= 36 + 24 && rd32(smpl + 28) >= 1) {
        uint32_t s = rd32(smpl + 36 + 8), e = rd32(smpl + 36 + 12);
        if (s <= e && e < nfr) { out->has_loop = 1; out->loop_start = s; out->loop_end = e + 1; }
    }
    return K_WAV_OK;
}

int k_wav_read_file(const char *path, uint8_t **buf, size_t *n, size_t max_bytes)
{
    FILE *f;
    long len;
    uint8_t *b;

    *buf = NULL;
    *n = 0;
    f = fopen(path, "rb");
    if (!f) return K_WAV_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return K_WAV_ERR_IO; }
    if (max_bytes && (unsigned long)len > max_bytes) { fclose(f); return K_WAV_ERR_TOO_BIG; }
    b = malloc(len ? (size_t)len : 1);
    if (!b) { fclose(f); return K_WAV_ERR_NOMEM; }
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); fclose(f); return K_WAV_ERR_IO; }
    fclose(f);
    *buf = b;
    *n = (size_t)len;
    return K_WAV_OK;
}

int k_wav_load(const char *path, KWav *out, size_t max_bytes)
{
    uint8_t *b;
    size_t n, fmax = 0;
    int r;

    memset(out, 0, sizeof *out);
    if (max_bytes) fmax = max_bytes <= (SIZE_MAX - 65536) / 2 ? max_bytes * 2 + 65536 : SIZE_MAX;   /* float halves; chunks */
    r = k_wav_read_file(path, &b, &n, fmax);
    if (r != K_WAV_OK) return r;
    r = k_wav_parse(b, n, out, max_bytes);
    free(b);
    return r;
}

void k_wav_free(KWav *w)
{
    if (!w) return;
    free(w->frames);
    memset(w, 0, sizeof *w);
}

const char *k_wav_error(int err)
{
    switch (err) {
    case K_WAV_OK: return "ok";
    case K_WAV_ERR_IO: return "cannot read the file";
    case K_WAV_ERR_NOT_WAVE: return "not a WAV file";
    case K_WAV_ERR_TRUNCATED: return "truncated";
    case K_WAV_ERR_FORMAT: return "unsupported format";
    case K_WAV_ERR_CHANNELS: return "not mono or stereo";
    case K_WAV_ERR_NOMEM: return "out of memory";
    case K_WAV_ERR_TOO_BIG: return "too big";
    default: return "error";
    }
}
