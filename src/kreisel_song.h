/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_SONG_H
#define KREISEL_SONG_H
/* The song serializer: what a project is, as bytes a platform can keep (a file on the SDL build, a few KB of flash on the FM-1).
 * No malloc, no stdio, no floats. Engines are named by their 4-character id (never by a
 * table index), the file keeps its own format version and each engine's version, an engine's parameters are kept by id (a hash of
 * the parameter's label, not its position), and every stored value is untrusted on load.
 *
 * File: "KRSG", u16 format version, u16 payload length, the payload (tagged chunks), u16 CRC-16/CCITT of all that comes before it.
 * Chunk: 4-character tag, u16 length, the data. All numbers are little-endian. A chunk with a tag the loader does not know is
 * skipped, so a newer writer may add chunks. Chunks:
 *   GLOB  5 bytes: tempo, swing, clock source, mute mask, solo mask
 *   SONG  32 bytes: the scene chain, scene by scene, four tracks each
 *   TRAK  per track: track, engine id (u32), engine version, colour, pattern in use, count n, n x (parameter id u16, value+128);
 *         an outside drum's (2026-11-12) then m, and K_LANES x m (value+128): each lane's values of the first m parameters above
 *   STEP  per track: track, 4 patterns x u16 step bits, 16 x (note, velocity, length)
 *   DRUM  per track: track, 8 x u16 lane bits (pattern A), 8 x 10 lane values (DRUM's kit), 3 x 8 x u16 (patterns B C D); a track
 *         whose TRAK has an outside drum's lanes: track, 4 x 8 x u16 (the patterns' lanes alone). (Older: 10 x u16, 10 x 3 values)
 *   PRMS  per track: track, then entries (page id, parameter, value+128) for each host parameter that is not at its default
 *   NAME  the song's name (up to K_NAME_LEN of A-Z 0-9 space - .), when it has one
 * Not saved (UI or device state): the page, focus, window, menu, balloon, touched cards, the track and step/lane in hand, the
 * play state and position, recording, the held keys and live notes, the battery, the number of cards shown, the output level
 * (MASTER) and the octave (a device's controls). */
#include <stddef.h>
#include <stdint.h>
#include "kreisel.h"

#define K_SONG_FORMAT 1
enum { K_SONG_OK = 0, K_SONG_ERR_ARG = -1, K_SONG_ERR_MAGIC = -2, K_SONG_ERR_VERSION = -3, K_SONG_ERR_TRUNC = -4, K_SONG_ERR_CRC = -5,
       K_SONG_ERR_TRACKS = -6 };   /* TRACKS: no longer returned (a song of more tracks opens with its first ones) */

/* Bytes written for app (0 when cap is too small or an argument is 0). buf may be unaligned. */
size_t k_song_save(const KApp *app, uint8_t *buf, size_t cap);
/* Reads a song. 0: loaded. Negative: refused before anything was changed (the app is as it was). On success the song part of the app
 * is replaced and the rest (the UI, the device's settings) is reset to a fresh state: stopped, on HOME. Never crashes on garbage. */
int k_song_load(KApp *app, const uint8_t *buf, size_t n);
/* The most bytes k_song_save can write: a buffer of this size always suffices. */
size_t k_song_max_size(void);

/* A user preset ("KRSP", the same header, chunks and CRC): one track's sound, its engine and parameters, a drum track's kit (the
 * lanes' pieces and their ENV), and the track's FX, ENV 1, ENV 2, ENV DEST and LFO pages. Not its steps, colour or mixer. */
size_t k_preset_max_size(void);
size_t k_preset_save(const KApp *app, int track, const char *name, uint8_t *buf, size_t cap);   /* 0: refused (a missing plugin) */
int k_preset_load(KApp *app, int track, const uint8_t *buf, size_t n);   /* K_SONG_OK, or refused with the track as it was */
/* What the loads would say, without an app: K_SONG_OK when k_song_load / k_preset_load would take the file, else their refusal */
int k_song_check(const uint8_t *buf, size_t n);
int k_preset_check(const uint8_t *buf, size_t n);
/* A song or a preset: valid, its name ("" when none; K_NAME_LEN + 1 bytes) and its (first) engine's id; K_SONG_ERR_* when not */
int k_file_info(const uint8_t *buf, size_t n, char *name, uint32_t *engine);
/* The file with its name changed in place (cap: the buffer's size): its new length, 0 when invalid or it does not fit */
size_t k_file_rename(uint8_t *buf, size_t n, size_t cap, const char *name);

#endif
