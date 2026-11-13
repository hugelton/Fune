/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#ifndef KREISEL_AUDIO_H
#define KREISEL_AUDIO_H

#include "kreisel.h"
#include <stddef.h>
#include <stdint.h>

/* The audio host: a sample-clock sequencer (frames, sample accurate) and the engines' DSP (kreisel_voice_dsp.c,
 * the author's own, from Felucca-Engines) behind it, all integer in the sample loops (no float, no divide: the FM-1 has neither). Tracks whose sound is ANALOG,
 * LOFI, NOISE, PHASE, TRIO or WHEEL play through that engine with the SOUND page's parameters and the ENV pages; a DRUM track
 * plays Kreisel's own drum voices (8 lanes, a piece each: drum_kreisel.c).
 *
 * Time. The engines work in blocks of 32 samples (CTL). A step or a note that falls inside a block starts at the next block
 * (at most 0.7 ms later); steps themselves are counted to the frame (triggers, last_trigger_frame). Output does not depend on
 * how the caller chops the stream into buffers.
 *
 * Rate. The tables are made for 44100 Hz (K_AUDIO_FS). Any other rate works: the pitch is corrected (not the filters and
 * times, which then scale with the rate). 44117 (the FM-1's I2S) needs no correction.
 *
 * One KAudio plays at a time: the engines keep their state in file-level arrays (WHEEL's rotors, the parts' voices). A
 * render or note on another KAudio than the last one starts that one from silence (voices sounding on the first end). */
#define K_AUDIO_FS 44100u
#define K_AUDIO_VOICES 6         /* voices per synth track (Felucca has 8) */
#ifndef K_SYS_VOICES
#define K_SYS_VOICES (K_AUDIO_VOICES * K_TRACKS)   /* the system's, every track's together (a platform may set fewer: platforms.toml) */
#endif
#define K_ARP_LIVE_MAX 16       /* simultaneously held keys per track; no allocation */
typedef struct { uint32_t order; uint8_t note, vel; } KArpKey;
typedef struct { uint64_t until; uint32_t order; uint8_t note, vel; } KArpSeqNote;
#define K_AUDIO_BLOCK 32         /* CTL: the engines' control block, in frames */

#define K_AUDIO_NP 45            /* a track's engine, LFO, ENV 2 and third-envelope parameters (kreisel_audio.c: P_*) */
#define K_DRUM_LANES K_LANES

/* A drum voice (drum_kreisel.c): a lane's piece. A pool is shared by the drum tracks; a lane that is hit again restarts its voice. */
#define K_DRUM_POOL 12
typedef struct { int32_t a1, a2, a3, k; } KDrumSvf;
typedef struct { int32_t v; uint32_t t, dly, hold, cut, k1, k2, ka; } KDrumEnv;   /* Q30 value, blocks, per-block Q16 decays */
typedef struct {
    uint8_t active, track, lane, piece, age, choked, teeth, tooth, crush;
    uint32_t rng;
    int32_t gain;                    /* Q15 */
    KDrumEnv e[6];                   /* the oscillators' three, the noise paths' two, the metal's */
    uint32_t ph[3], inc[3];
    int16_t lvl[3];
    int32_t pe[3];                   /* the pitch above the note, Q12, falling */
    uint32_t kp[3];
    int16_t nlvl[2];
    uint8_t nm[2][2];
    KDrumSvf nf[2][2];
    int32_t ns[2][2][2];
    int16_t mlvl;
    uint8_t mn, msine, mxor, mdrive, mm[2];
    uint32_t minc[8], mph[8], mduty;
    KDrumSvf mf[2];
    int32_t ms[2][2];
    int32_t lpk, hpk, lp, hp;        /* the output one-poles */
    int32_t clk;                     /* the lane's CLIK: a noise burst at the strike, Q15, falling ~1 ms */
    int16_t pan_l, pan_r;            /* the lane's PAN as two gains, Q12 (4096 both at the centre: as loud as before) */
} KDrumVoice;

typedef struct {
    uint32_t rate;
    uint64_t frame;                    /* total rendered sample frames */
    int64_t frames_to_step;            /* frames to the next step, Q16 (sample accurate across callback sizes) */
    int playing, bpm, swing, master;
    int ext;                           /* the steps follow an external clock (clock_src USB or TRS), not the sample clock */
    uint32_t clk_acc, clk_gen;         /* external: clocks toward the next step (6 a step); the follower's start counter */
    int current_step, next_step;
    int sound[K_TRACKS];
    int fader[K_TRACKS], pan[K_TRACKS];
    uint16_t steps[K_TRACKS][4];   /* each pattern's steps that have notes (KApp.steps) */
    uint32_t chance[K_TRACKS][4];  /* their chances to sound (KApp.chance, 2 bits a step) */
    uint32_t rng;                  /* the chances' dice (xorshift32 from a fixed seed: k_audio_init, so a test hears the same) */
    uint8_t play_pat[K_TRACKS];    /* the pattern each track plays: the one in hand, or the song's scene's (4: silent in it) */
    uint8_t song[K_SCENES][K_TRACKS], song_on, song_end, song_first;   /* the song playing (KApp.song_play): its scenes, a bar each */
    int scene, scene_last;         /* the scene playing, the last one that sounds (0..K_SCENES-1) */
    uint16_t lane_on[K_TRACKS][4][K_LANES];   /* a drum track's lanes, each pattern's (k_lanes) */
    int16_t lane_p[K_TRACKS][K_LANES][K_LANE_VALS];  /* a drum lane: piece, tune, tone, decay, level; attack, pitch depth, pitch time, click; pan.
                                                * An outside drum's track: each lane's own values of the drum (k_lane_val, k_plugin_lane_params) */
    KNote roll[K_TRACKS][K_ROLL_NOTES];   /* the track's notes, every pattern's (KApp.roll's, packed: a zero length ends them) */
    uint64_t triggers[K_TRACKS];      /* useful for deterministic transport tests */
    uint64_t last_trigger_frame[K_TRACKS];
    uint8_t arp_last[K_TRACKS];       /* the arp's last note on each track (its pitch), for the screen: k_audio_arp_last */
    uint8_t voices[K_TRACKS];         /* voices sounding on a track, as of the last block (a drum track: lanes) */
    /* Arpeggio playback: per-track keyed notes, the sequencer's held steps, clocks
     * and note release masks. Pages/songs already store MODE RATE OCT GATE. */
    KArpKey arp_live[K_TRACKS][K_ARP_LIVE_MAX];
    KArpSeqNote arp_seq[K_TRACKS][K_STEPS];
    uint64_t arp_next_q16[K_TRACKS];
    uint32_t arp_step[K_TRACKS], arp_rng[K_TRACKS], arp_walk[K_TRACKS], arp_order;
    uint16_t arp_ext_acc[K_TRACKS];
    uint8_t arp_mode[K_TRACKS], arp_rate[K_TRACKS], arp_oct[K_TRACKS], arp_gate[K_TRACKS];
    uint8_t arp_pending[K_TRACKS], arp_voice_mask[K_TRACKS];
    uint8_t mo_arp[K_TRACKS][K_AUDIO_VOICES];   /* MIDI out: the arp's note sent from each voice, + 1 (0: none), off when the voice lets go */

    /* --- the host (kreisel_audio.c) --- */
    int16_t tp[K_TRACKS][K_AUDIO_NP]; /* the track's engine parameters, as the engines index them */
    uint8_t dp[K_TRACKS][8];          /* a drum track's kit: [0] TUNE [1] DCAY [2] CRSH [3] HUMN, [4] the LFO's lane (0 all, 1..8) */
    uint8_t eng_req[K_TRACKS];        /* the synth engine asked for (0..K_ENGINE_COUNT-2) */
    uint16_t gain_l[K_TRACKS], gain_r[K_TRACKS];   /* level * pan, Q12 */
    uint16_t gain_m[K_TRACKS];                      /* the fader alone (Q12): the sends take it */
    uint16_t send_q12[K_TRACKS][2];                 /* B1, B2 a track (Q12, 0 .. unity) */
    int32_t bus_ret_q12[2], bus_tone_q15[2], bus_lp[2];   /* the buses' RTN (Q12, 4096 unity: the fader's dB, kreisel_level.h), TONE, its low-pass */
    uint8_t bus_ret_v[2], bus_set;   /* the RTN faders the returns were made from, and which are made */
    uint8_t gains_set;   /* the tracks whose gain_l / gain_r are made (k_audio_sync makes them again only when the fader or pan moves) */
    uint8_t master_out;   /* 1: MASTER is the platform's, after the render (k_audio_master_outside); the render stays at full level */
    uint16_t master_q12;
    uint64_t rel_at[K_TRACKS][K_AUDIO_VOICES];     /* frame at which a sequenced note is released (~0: a held key) */
    KDrumVoice drum[K_DRUM_POOL];
    int16_t bend[K_TRACKS];           /* the tracks' pitch bend (KApp.bend, k_audio_sync): 1/4096 semitone */
    int32_t tune16, tune_fine;        /* the rate's pitch correction (1/16 semitone, 1/4096) */
    int64_t dc[2];                    /* the output DC blocker: 4096 x the DC of each side (dc_block) */
    uint32_t lim_red;                 /* the master limiter's reduction, Q16 (0: none; 65536 - its gain) */
    volatile uint16_t pk_trk[K_TRACKS], pk_out[2];   /* the tracks' peaks after their faders, and the output's: k_audio_take_peaks */
    uint8_t metro, count_left, click_kind;   /* the settings' Metronome while REC; the steps of a bar still counted in (0: none); its Sound */
    uint32_t click_ph, click_inc;     /* the metronome's click: a sine, falling (into the output only, never MIDI out) */
    int32_t click_env;
    uint32_t bpos;                    /* next sample of the block buffer (K_AUDIO_BLOCK: empty) */
    int16_t bl[K_AUDIO_BLOCK], br[K_AUDIO_BLOCK];  /* the rendered block, Q15 */
    /* MIDI out of the sequencer (k_audio_midi_pop): the render writes, the main loop reads */
    uint8_t mo_on;                    /* someone listens (k_audio_sync: k_io_sending) */
    volatile uint8_t mo_w, mo_r;
    uint32_t mo_q[32];                /* status | d1 << 8 | d2 << 16 */
    struct { uint8_t t, note, left; } mo_hold[16];   /* notes sent and sounding: steps to their off (0: free) */
    uint8_t mo_run;                   /* Start sent, Stop not yet (the transport's own clock, not an external one) */
    int64_t mo_clk_q16;               /* frames to the next clock (F8, 24 a quarter), Q16 */
} KAudio;

void k_audio_init(KAudio *audio, uint32_t sample_rate);
const char *k_audio_drum_piece(int piece);   /* a drum piece's name as the DSP has it (the DRUM engine's PIECE names must match) */
/* Copy project changes and transport. Call with the platform audio callback locked. */
void k_audio_sync(KAudio *audio, const KApp *app);
/* n external MIDI clocks (k_clock_take) under the same lock as k_audio_sync, called after it. Every 6 the sequencer steps (at the
 * next render); ignored unless the app's clock is external. */
void k_audio_clock_tick(KAudio *audio, uint32_t n);
/* the sequencer's next MIDI message out (its notes on the track's channel, a drum lane's as General MIDI's; the clock, 24 a
 * quarter, with Start and Stop, while it runs on its own tempo, not an external clock), from the main loop: its length (1 for
 * the clock's), 0 none (kreisel_io.c's k_io_send_audio sends them all) */
int k_audio_midi_pop(KAudio *audio, uint8_t msg[3]);
extern const uint8_t k_audio_gm_drum[K_LANES];   /* a lane's General MIDI note: BD 36, SD 38, CH 42, OH 46, CP 39, LT 45, HT 50, RS 37 */
/* A live note (a key, MIDI in) on a track: on sounds until its off. Call with the platform audio callback locked. */
void k_audio_note(KAudio *audio, int track, int note, int velocity, int on);
/* the scope: the track in hand's sound (after its engine and inserts), n points (64 at most) from a rising zero crossing; how many */
int k_audio_scope(int8_t *dst, int n);
/* Fill interleaved stereo float32 (-1..1), independent of SDL and independent of buffer length. */
/* the song (KApp.song_play) after a render: the scene playing and its patterns into the app (the screen follows them); at the end
 * of its last scene the transport stops. The platform calls it where it takes the playhead (audio.current_step) */
void k_audio_song_follow(const KAudio *audio, KApp *app);
void k_audio_sys_voices(int n);   /* the voices of all the tracks together (K_SYS_VOICES at the start): past it the oldest gives way */
int k_audio_voices_on(void);      /* the voices sounding now, every track's */
void k_audio_render(KAudio *audio, float *stereo, size_t frames);
/* The same without float: interleaved stereo int32, 24-bit LEFT-justified as the FM-1's I2S wants it: the 16-bit sample
 * << 7, so full scale is 2^22 (-6 dBFS, as Felucca's OUT_SCALE) and the sample is the output word as it is. Use this one on a
 * device with no FPU. */
void k_audio_render_s32(KAudio *audio, int32_t *stereo, size_t frames);
/* MASTER outside the render (on: 1): the render stays at full level (the limiter's ceiling) and the platform scales its own output
 * by k_audio_master_q12 (0 .. 4096, the audio taper), so a second output (the FM-1's USB audio) keeps its level whatever the
 * volume knob says. Off (the default): MASTER is applied in the render, as before. */
void k_audio_master_outside(KAudio *audio, int on);
int k_audio_master_q12(const KAudio *audio);
void k_audio_take_peaks(KAudio *audio, KApp *app, uint32_t ms, int out);   /* the meters' peaks to the app (each frame), then cleared */

/* the voices a track may take, 1 .. 6 (all at start): the platform lowers it when its sound does not keep up (the FM-1's load
 * guard: thinned before it has to stop), and raises it again once there is room */
void k_audio_voice_cap(int n);
int k_audio_voice_cap_now(void);
/* the arp's last note on track t (its pitch, -1: no arp) and how many notes it has played: the platform hands them to the screen
 * (k_arp_note) */
int k_audio_arp_last(const KAudio *a, int t, uint32_t *count);
#endif
