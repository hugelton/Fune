/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* WAV reading for the sample store (Linux and macOS only, never the FM-1). A RIFF/WAVE file is read into int16_t frames,
 * interleaved, one or two channels: PCM 16 and 24 bits, 32-bit float (format 3), and WAVE_FORMAT_EXTENSIBLE carrying them.
 * Unknown chunks are skipped; a 'smpl' chunk's first loop is kept. Every size is checked against the buffer, and a file that
 * does not fit is refused with an error code (no partial result). */
#ifndef K_WAV_H
#define K_WAV_H
#include <stddef.h>
#include <stdint.h>

enum {
    K_WAV_OK = 0,
    K_WAV_ERR_IO = -1,          /* the file could not be opened or read */
    K_WAV_ERR_NOT_WAVE = -2,    /* no RIFF/WAVE header */
    K_WAV_ERR_TRUNCATED = -3,   /* a chunk or the data runs past the end, or fmt/data is missing */
    K_WAV_ERR_FORMAT = -4,      /* an encoding not read here (8-bit, ADPCM, 64-bit float, ...) */
    K_WAV_ERR_CHANNELS = -5,    /* not mono or stereo */
    K_WAV_ERR_NOMEM = -6,
    K_WAV_ERR_TOO_BIG = -7,     /* the frames would pass the caller's memory limit */
};

typedef struct {
    int16_t *frames;            /* interleaved, channels * nframes values; malloc'd, k_wav_free releases it */
    uint32_t nframes;
    uint32_t rate;              /* frames per second as the file says */
    int channels;               /* 1 or 2 */
    int has_loop;               /* a 'smpl' loop was found and fits the frames */
    uint32_t loop_start, loop_end;   /* in frames; loop_end is one past the last looped frame */
} KWav;

/* Parse a whole file held in memory. max_bytes limits the frames' memory (0: no limit). On an error *out is left empty. */
int k_wav_parse(const uint8_t *buf, size_t n, KWav *out, size_t max_bytes);
/* Read a file into a malloc'd buffer of at most max_bytes (0: no limit; a larger file is K_WAV_ERR_TOO_BIG). */
int k_wav_read_file(const char *path, uint8_t **buf, size_t *n, size_t max_bytes);
/* Both of the above: read and parse the file, the limit applying to the frames (the file may be up to twice that, float). */
int k_wav_load(const char *path, KWav *out, size_t max_bytes);
void k_wav_free(KWav *w);
const char *k_wav_error(int err);

#endif
