/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Recording from an audio input into a sample (Linux and macOS only, never the FM-1). One recorder: the platform's capture
 * callback feeds it (k_rec_input, its own thread), the main thread starts and stops it and writes what it took as a WAV file.
 * Mono, 16 bits, at the rate it was set up with (the input's channels are mixed down). A start may wait for a sound: the
 * recording then begins at the first frame over the threshold, with a little of what came before it (the attack kept).
 * The input's level (its peak) is kept whether or not it records, for a meter. */
#ifndef K_REC_H
#define K_REC_H
#include <stdint.h>

enum { K_REC_IDLE = 0, K_REC_WAIT = 1, K_REC_ON = 2 };
#define K_REC_THRESHOLD 160      /* a start that waits: |frame| over this (Q15, about -46 dBFS) */
#define K_REC_PREROLL 1024       /* frames kept from before the threshold was passed */

/* the buffer: max_frames at rate (allocated here, again on a change); 0, or -1 out of memory */
int k_rec_init(uint32_t rate, uint32_t max_frames);
void k_rec_shutdown(void);
uint32_t k_rec_rate(void);
uint32_t k_rec_max(void);
/* (the capture thread) n frames of `channels` interleaved, floats in -1 .. 1 or 16-bit integers */
void k_rec_input(const float *in, uint32_t n, int channels);
void k_rec_input16(const int16_t *in, uint32_t n, int channels);
/* start (wait 1: at the first sound), stop. The state: K_REC_*; a recording that filled the buffer stops by itself */
int k_rec_start(int wait);
void k_rec_stop(void);
int k_rec_state(void);
/* what has been taken (the frames stay until the next start) */
uint32_t k_rec_frames(void);
const int16_t *k_rec_data(void);
/* the input's peak since the last call (Q15, 0 .. 32767); it falls back as it is read */
int k_rec_level(void);
/* the platform's input is open (1) or not: a meter shows the level only while it is */
void k_rec_set_live(int on);
int k_rec_live(void);
/* the silence before the first sound cut off (|frame| over threshold), K_REC_PREROLL frames before it kept; none over it: all
 * kept. The frames cut, from the start */
uint32_t k_rec_trim(int threshold);
/* the frames taken brought up so their peak is near full scale, by at most max_db (a quiet microphone): the gain used, in dB */
int k_rec_normalize(int max_db);
/* write the frames taken as a WAV file: K_WAV_OK, or a K_WAV_ERR_* (nothing taken: K_WAV_ERR_TRUNCATED) */
int k_rec_save(const char *path);

#endif
