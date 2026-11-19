/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The Sampler's file browser (src/sample/k_browse.h): the folder's files in name order with their lengths from the headers, the
 * track's file in hand on opening, moving gives the track each file, closing keeps it or puts the old one back. */
#include "sample/k_browse.h"
#include "sample/k_wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int16_t f[96000];

int main(void)
{
    char dir[] = "/tmp/kreisel_browse_XXXXXX", p[300];
    uint32_t fr, rate;
    int ch;
    static const char *const N[] = {"c-long.wav", "a-short.wav", "b-stereo.wav", "notes.txt", "z-broken.wav"};
    assert(mkdtemp(dir));
    snprintf(p, sizeof p, "%s/%s", dir, N[0]); assert(k_wav_write(p, f, 96000, 1, 48000) == K_WAV_OK);   /* 2 s */
    snprintf(p, sizeof p, "%s/%s", dir, N[1]); assert(k_wav_write(p, f, 4410, 1, 44100) == K_WAV_OK);    /* 100 ms */
    snprintf(p, sizeof p, "%s/%s", dir, N[2]); assert(k_wav_write(p, f, 22050, 2, 44100) == K_WAV_OK);   /* 500 ms, stereo */
    snprintf(p, sizeof p, "%s/%s", dir, N[3]); { FILE *t = fopen(p, "w"); fputs("x", t); fclose(t); }
    snprintf(p, sizeof p, "%s/%s", dir, N[4]); { FILE *t = fopen(p, "w"); fputs("RIFF", t); fclose(t); }

    snprintf(p, sizeof p, "%s/%s", dir, N[2]);
    assert(k_wav_info(p, &fr, &rate, &ch) == K_WAV_OK && fr == 22050 && rate == 44100 && ch == 2);
    snprintf(p, sizeof p, "%s/%s", dir, N[4]);
    assert(k_wav_info(p, &fr, &rate, &ch) != K_WAV_OK);

    k_smp_set_dir(dir);
    assert(!k_browse_is_open() && k_browse_sel() == -1 && !k_browse_name(0));
    assert(k_smp_assign(1, "b-stereo.wav") == K_WAV_OK);
    assert(k_browse_open(1) == 4 && k_browse_is_open() && k_browse_track() == 1);   /* (the .txt left out; the broken one listed) */
    assert(!strcmp(k_browse_name(0), "a-short.wav") && !strcmp(k_browse_name(3), "z-broken.wav"));
    assert(k_browse_sel() == 1);                                                     /* the track's own file in hand */
    assert(k_browse_ms(0) == 100 && k_browse_ms(1) == 500 && k_browse_ms(2) == 2000 && k_browse_ms(3) == 0);

    assert(k_browse_move(1) == K_WAV_OK && k_browse_sel() == 2 && !strcmp(k_smp_path(1), "c-long.wav") && k_smp_sample(1));
    assert(k_browse_move(-9) == K_WAV_OK && k_browse_sel() == 0 && !strcmp(k_smp_path(1), "a-short.wav"));   /* held at the top */
    k_browse_close(0);                                                               /* BACK: the old one */
    assert(!k_browse_is_open() && !strcmp(k_smp_path(1), "b-stereo.wav"));

    assert(k_browse_open(1) == 4 && k_browse_pick(2) == K_WAV_OK);
    assert(k_browse_pick(9) == -1 && k_browse_sel() == 2);
    k_browse_close(1);                                                               /* OK: kept */
    assert(!strcmp(k_smp_path(1), "c-long.wav"));

    assert(k_browse_open(0) == 4 && k_browse_sel() == 0);                            /* a track with no file: the first */
    k_browse_close(0);
    assert(!k_smp_path(0)[0]);

    k_smp_clear_all();
    for (ch = 0; ch < 5; ch++) { snprintf(p, sizeof p, "%s/%s", dir, N[ch]); unlink(p); }
    assert(k_browse_open(0) == 0 && k_browse_sel() == -1 && k_browse_move(1) == -1);   /* an empty folder */
    k_browse_close(1);
    rmdir(dir);
    printf("browse: ok\n");
    return 0;
}
