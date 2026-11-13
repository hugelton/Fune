/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The WAV reader and the sample store (src/sample): files built in memory, parsed values, refusals, and the store over a file. */
#include "sample/k_wav.h"
#include "sample/k_sample.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t B[4096];
static size_t bn;

static void put(const void *p, size_t n) { assert(bn + n <= sizeof B); memcpy(B + bn, p, n); bn += n; }
static void p8(unsigned v) { uint8_t b = (uint8_t)v; put(&b, 1); }
static void p16(unsigned v) { p8(v & 0xFF); p8(v >> 8 & 0xFF); }
static void p32(uint32_t v) { p16(v & 0xFFFF); p16(v >> 16); }
static void p24(int32_t v) { p8((unsigned)v & 0xFF); p8((unsigned)v >> 8 & 0xFF); p8((unsigned)v >> 16 & 0xFF); }
static void pf(float f) { uint32_t u; memcpy(&u, &f, 4); p32(u); }
static void begin(void) { bn = 0; put("RIFF", 4); p32(0); put("WAVE", 4); }
static void finish(void) { uint32_t s = (uint32_t)(bn - 8); B[4] = s & 0xFF; B[5] = s >> 8 & 0xFF; B[6] = s >> 16 & 0xFF; B[7] = s >> 24; }
static void fmt(unsigned code, unsigned ch, uint32_t rate, unsigned bits)
{
    put("fmt ", 4); p32(16); p16(code); p16(ch); p32(rate); p32(rate * ch * bits / 8); p16(ch * bits / 8); p16(bits);
}
static void fmt_ext(unsigned sub, unsigned ch, uint32_t rate, unsigned bits)
{
    static const uint8_t tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    put("fmt ", 4); p32(40); p16(0xFFFE); p16(ch); p32(rate); p32(rate * ch * bits / 8); p16(ch * bits / 8); p16(bits);
    p16(22); p16(bits); p32(ch == 2 ? 3 : 4); p16(sub); put(tail, 14);
}
static void data_hdr(uint32_t n) { put("data", 4); p32(n); }
static int parse(KWav *w) { return k_wav_parse(B, bn, w, 0); }

static void test_pcm16_mono(void)
{
    KWav w;
    begin(); fmt(1, 1, 44100, 16);
    put("junk", 4); p32(3); p8(1); p8(2); p8(3); p8(0);   /* odd-sized unknown chunk, padded */
    data_hdr(8); p16(0); p16(1000); p16((unsigned)-1000 & 0xFFFF); p16(32767); finish();
    assert(parse(&w) == K_WAV_OK);
    assert(w.channels == 1 && w.rate == 44100 && w.nframes == 4 && !w.has_loop);
    assert(w.frames[0] == 0 && w.frames[1] == 1000 && w.frames[2] == -1000 && w.frames[3] == 32767);
    k_wav_free(&w);
    assert(w.frames == NULL);
}
static void test_pcm24_stereo_smpl(void)
{
    KWav w;
    int i;
    begin(); fmt(1, 2, 48000, 24);
    data_hdr(4 * 6); p24(0x7FFFFF); p24(-0x800000); p24(0x123456); p24(-0x123456); p24(0x80); p24(0x7F); p24(0); p24(-1);
    put("smpl", 4); p32(36 + 24); for (i = 0; i < 7; i++) p32(0); p32(1); p32(0);
    p32(0); p32(0); p32(1); p32(2); p32(0); p32(0);   /* loop: id, type, start 1, end 2 (inclusive), fraction, count */
    finish();
    assert(parse(&w) == K_WAV_OK);
    assert(w.channels == 2 && w.rate == 48000 && w.nframes == 4);
    assert(w.frames[0] == 32767 && w.frames[1] == -32768);
    assert(w.frames[2] == 0x1234 && w.frames[3] == -0x1234);   /* 0x56 rounds down */
    assert(w.frames[4] == 1 && w.frames[5] == 0 && w.frames[6] == 0 && w.frames[7] == 0);
    assert(w.has_loop && w.loop_start == 1 && w.loop_end == 3);
    k_wav_free(&w);
}
static void test_float_and_ext(void)
{
    KWav w;
    begin(); fmt(3, 1, 22050, 32); data_hdr(20); pf(0.0f); pf(0.5f); pf(-1.0f); pf(2.0f); pf(-7.0f); finish();
    assert(parse(&w) == K_WAV_OK);
    assert(w.nframes == 5 && w.rate == 22050);
    assert(w.frames[0] == 0 && w.frames[1] == 16384 && w.frames[2] == -32767 && w.frames[3] == 32767 && w.frames[4] == -32767);
    k_wav_free(&w);
    begin(); fmt_ext(1, 2, 96000, 16); data_hdr(8); p16(5); p16(6); p16(7); p16(8); finish();
    assert(parse(&w) == K_WAV_OK);
    assert(w.channels == 2 && w.nframes == 2 && w.rate == 96000 && w.frames[3] == 8);
    k_wav_free(&w);
    begin(); fmt_ext(3, 1, 44100, 32); data_hdr(4); pf(-0.5f); finish();
    assert(parse(&w) == K_WAV_OK && w.frames[0] == -16384);
    k_wav_free(&w);
}
static void test_refusals(void)
{
    KWav w;
    size_t k;
    begin(); fmt(1, 1, 44100, 16); data_hdr(8); p16(1); p16(2); p16(3); p16(4); finish();
    for (k = 0; k < bn; k++) {          /* every cut of a good file: refused or fewer frames, never an overrun */
        int r = k_wav_parse(B, k, &w, 0);
        assert(r != K_WAV_OK || w.nframes < 4);
        k_wav_free(&w);
    }
    assert(k_wav_parse(B, 8, &w, 0) == K_WAV_ERR_NOT_WAVE && w.frames == NULL);
    assert(k_wav_parse(B, bn, &w, 4) == K_WAV_ERR_TOO_BIG);
    begin(); fmt(1, 1, 44100, 16); data_hdr(1000); p16(1); finish();
    assert(parse(&w) == K_WAV_ERR_TRUNCATED);
    begin(); fmt(1, 1, 44100, 16); data_hdr(0xFFFFFFFFu); p16(1); finish();
    assert(parse(&w) == K_WAV_ERR_TRUNCATED);
    begin(); fmt(1, 3, 44100, 16); data_hdr(6); p16(1); p16(2); p16(3); finish();
    assert(parse(&w) == K_WAV_ERR_CHANNELS);
    begin(); fmt(1, 1, 44100, 8); data_hdr(2); p8(1); p8(2); finish();
    assert(parse(&w) == K_WAV_ERR_FORMAT);
    begin(); fmt(2, 1, 44100, 16); data_hdr(2); p16(1); finish();   /* ADPCM */
    assert(parse(&w) == K_WAV_ERR_FORMAT);
    begin(); fmt(1, 1, 44100, 16); finish();                       /* no data */
    assert(parse(&w) == K_WAV_ERR_TRUNCATED);
    begin(); data_hdr(2); p16(1); finish();                          /* no fmt */
    assert(parse(&w) == K_WAV_ERR_TRUNCATED);
    memcpy(B, "RIFX", 4);
    assert(parse(&w) == K_WAV_ERR_NOT_WAVE);
}
static void test_store(void)
{
    char dir[] = "kreisel_sample_XXXXXX", path[256];
    const KSample *s;
    FILE *f;
    int a, b;

    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/kick.wav", dir);
    begin(); fmt(1, 2, 44100, 16); data_hdr(400); { int i; for (i = 0; i < 200; i++) p16((unsigned)i); } finish();
    f = fopen(path, "wb");
    assert(f && fwrite(B, 1, bn, f) == bn);
    fclose(f);

    assert(k_sample_load("/nonexistent/x.wav") == -1 && k_sample_last_error() == K_WAV_ERR_IO);
    a = k_sample_load(path);
    b = k_sample_load(path);
    assert(a >= 0 && a == b && k_sample_find(path) == a);
    s = k_sample_get(a);
    assert(s && s->refs == 2 && strcmp(s->name, "kick.wav") == 0 && strcmp(s->path, path) == 0);
    assert(s->channels == 2 && s->nframes == 100 && s->rate == 44100 && s->frames[199] == 199);
    assert(k_sample_mem_used() == 400);
    k_sample_free(a);
    assert(k_sample_get(a) && k_sample_get(a)->refs == 1);
    k_sample_free(a);
    assert(!k_sample_get(a) && k_sample_find(path) == -1 && k_sample_mem_used() == 0);
    assert(!k_sample_get(-1) && !k_sample_get(K_SAMPLE_MAX));

    k_sample_set_mem_max(399);          /* the cap refuses a file one byte over it */
    assert(k_sample_load(path) == -1 && k_sample_last_error() == K_WAV_ERR_TOO_BIG && k_sample_mem_used() == 0);
    k_sample_set_mem_max(400);
    a = k_sample_load(path);
    assert(a >= 0 && k_sample_mem_used() == 400);
    k_sample_free_all();
    assert(k_sample_mem_used() == 0 && !k_sample_get(a));
    k_sample_set_mem_max((size_t)-1);
    assert(k_sample_mem_max() == K_SAMPLE_MEM_MAX);

    remove(path);
    rmdir(dir);
}

int main(void)
{
    test_pcm16_mono();
    test_pcm24_stereo_smpl();
    test_float_and_ext();
    test_refusals();
    test_store();
    printf("sample_test: ok\n");
    return 0;
}
