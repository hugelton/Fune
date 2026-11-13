/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_SAVE_H
#define KREISEL_SAVE_H
/* SAVE: song slots and user presets, after Felucca's SAVE > PROJECT and SAVE > USER (the owner's design).
 *
 * SAVE (not in a menu) opens the SAVE overlay on its SONG side; SAVE again turns it to SOUND and back. ALGORITHM (or UP / DOWN)
 * picks a slot. A knob arms an action: K1 LOAD, K2 SAVE, K3 NAME, K4 ERASE; OCT+ (or ACCEPT) does it, OCT- (or BACK) lets it go;
 * BACK with nothing armed (HOME) closes the overlay. SAVE goes through the NAME screen first, prefilled; NAME renames a used
 * slot. On the NAME screen K1 moves the cursor, K2 (or ALGORITHM, or the value keys) turns the letter, OCT+ writes, OCT-
 * cancels. Without knobs (a pad, a keyboard) the value keys step the action, ACCEPT does it. Saving, renaming and erasing wait
 * for the transport to stop (the FM-1's flash erase silences the audio).
 *
 * A song slot holds a whole song (kreisel_song.c, "KRSG"); a preset slot one track's sound ("KRSP"): LOAD puts it on the track
 * in hand. The platform keeps the bytes behind a KStore: flash sectors on the FM-1, files on the SDL build, memory elsewhere. */
#include <stddef.h>
#include <stdint.h>
#include "kreisel.h"

#define K_SONG_SLOTS 6
#define K_USER_PRESETS 24
#define K_SAVE_BUF (896 * K_TRACKS)   /* >= k_song_max_size() and k_preset_max_size(): checked by k_store_set (3584 at four tracks: a song slot on the FM-1 holds 3584) */
enum { K_STORE_SONG, K_STORE_PRESET };
enum { K_SAVE_NONE, K_SAVE_LOAD, K_SAVE_SAVE, K_SAVE_NAME, K_SAVE_ERASE, K_SAVE_SURE };   /* SURE: ERASE asked once, OCT+ again erases */

typedef struct {
    long (*read)(void *ctx, int kind, int slot, uint8_t *buf, size_t cap);        /* its bytes; 0 empty; < 0 an error */
    int (*write)(void *ctx, int kind, int slot, const uint8_t *buf, size_t n);    /* 0 written */
    int (*erase)(void *ctx, int kind, int slot);                                  /* 0 erased */
    int (*latest)(void *ctx);   /* the song slot written last (power-on loads it), -1 none; may be 0 (no such thing) */
    void *ctx;
    long (*read_settings)(void *ctx, uint8_t *buf, size_t cap);        /* the settings' few bytes (k_settings_*); 0 none; may be 0 */
    int (*write_settings)(void *ctx, const uint8_t *buf, size_t n);    /* 0 written; may be 0 (they are then not kept) */
} KStore;
#define K_SETTINGS_BYTES 64      /* the most a settings record takes ("KRST", version, count, values) */

/* The platform's store (0: none; SAVE then says SAVE FAILED); reads every slot's name. 0, or -1 when the buffer is too small. */
int k_store_set(const KStore *store);
void k_save_scan(void);                       /* the slots' names again (a platform that changed them behind the core's back) */
int k_save_used(int kind, int slot);
const char *k_save_name(int kind, int slot);  /* "" when empty or unnamed */
uint32_t k_save_engine(int slot);              /* a preset's engine id, 0 when empty */
int k_settings_load(KApp *app);   /* the settings kept (power-on, before the song): 0 read, else the defaults stay */
int k_settings_save(const KApp *app);   /* 0 written (the platform waits for the transport to stop: a flash erase) */
int k_save_boot(KApp *app);                   /* power-on: the song slot written last; 0 loaded, else nothing changed */
int k_save_song(KApp *app, int slot);         /* the song (with its name) into a slot, as SAVE does: 0 saved (a console's `save`) */
int k_save_load(KApp *app, int slot);         /* a song slot loaded: 0, else nothing changed */
int k_save_preset_step(KApp *app, int d);
int k_save_preset_load(KApp *app, int slot);  /* a user preset by its slot onto the track in hand (a desktop's list); 0 loaded */     /* PRESETS: the next (d > 0) or the one before user preset onto the track in hand; 0 loaded */

/* Files moved to and from a computer (the FM-1's console and fm1_slots.py; a desktop's open): the bytes exactly as a slot keeps
 * them, a "KRSG" song or a "KRSP" preset (kreisel_song.h), the same on every device (a .fune file). */
enum { K_SAVE_ERR_PLAYING = -20, K_SAVE_ERR_WRITE = -21 };
long k_save_get(int kind, int slot, uint8_t *out, size_t cap);   /* a slot's bytes: their count, 0 empty, < 0 an error */
/* a file into a slot: refused (K_SONG_ERR_*) unless the load would take it as that kind, while the transport runs
 * (K_SAVE_ERR_PLAYING: the FM-1's flash erase), or when the store fails (K_SAVE_ERR_WRITE); bytes past the file's end are dropped */
int k_save_put(KApp *app, int kind, int slot, const uint8_t *buf, size_t n);
/* a file opened as it is (a desktop's drop, its command line): a song replaces the song (no slot's), a preset goes onto the track
 * in hand with its engine; 0, else K_SONG_ERR_* and nothing changed */
int k_save_open_file(KApp *app, const uint8_t *buf, size_t n);

void k_save_open(KApp *app);                  /* SAVE: open on SONG, or turn the side */
int k_save_dispatch(KApp *app, KAction action);   /* the overlay's input; 1 when it took the action */
int k_save_quick(KApp *app, KAction action);  /* SAVE on a synth page over the user preset in hand: asks, then overwrites; 1 when it took the action */
const char *k_save_act_name(int act);         /* "Load" "Save" "Name" "Erase" */
int k_save_menu_row(int full, int i);         /* the SAVE menu's row i: LOAD SAVE NAME ERASE (full: a slot with something in it), else SAVE alone */
const char *k_save_charset(void);             /* the NAME screen's letters, in turning order */

#endif
