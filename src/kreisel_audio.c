/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The audio host. A sample-clock sequencer, and the host the engines need (kreisel_engine_types.h, kreisel_voice_dsp.c): the
 * types and the parameter numbers (kreisel_engine_types.h lists them), a voice allocator, the amplitude envelope, the block loop
 * and a small mixer, with Kreisel's own drum voices beside them. Everything in the sample loops is integer.
 *
 * The voice handling (voice_alloc, voice_start, env_tick, the per-voice modulation in track_block) and the engine switch's
 * short fade are ported from felucca-2.0-snap/firmware/src/voice.c, the author's own code (relicensed MPL-2.0 for Kreisel): reduced to what Kreisel has (no unison / glide / matrix / sends).
 * The track LFO (lfo_wave, lfo_tick: wave shapes, S&H on each cycle, the FADE, a restart at PHS on a fresh phrase, and its four
 * routings to pitch / cutoff / shape / amplitude) is ported from felucca-2.0-snap/firmware/src/voice.c (lfo_wave, lfo_rand,
 * track_lfo_tick, track_render: the LFO DEST terms), the author's own (relicensed MPL-2.0). ENV 2 is
 * Kreisel's own (Felucca 2.0 has one envelope): it reuses Felucca's exponential decay table (ENV_EXP) and the destination terms. */
#include "kreisel_audio.h"
#include "kreisel_plugin.h"
#include "kreisel_arp.h"
#include "kreisel_fx.h"
#include <math.h>
#include "kreisel_math.h"
#include "kreisel_level.h"      /* the faders' dB scale */
#include <stdlib.h>
#include <string.h>

#define K_PI_2 1.57079632679f
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"

/* ------------------------------------------------------------ the engines' host --- */
#include "kreisel_dsp.h"         /* NVOICE NPART, the parameter slots P_* (shared with the plugins: sdk/kreisel_sdk.h) */
_Static_assert(NVOICE == K_AUDIO_VOICES && NPART == K_TRACKS, "kreisel_dsp.h's voices and parts");
#include "fe_tables.h"          /* FS, CTL, SINE, PITCH_OCT, SVF_G, TANH_Q15, ENV_LIN, ENV_EXP (tools/gen_audio_tables.py) */
#include "kreisel_engine_types.h"
#include "kreisel_engine_names.h"   /* the engines' names as shown (eng_*.c .name) */

_Static_assert(P_COUNT == K_AUDIO_NP, "K_AUDIO_NP");
_Static_assert(CTL == K_AUDIO_BLOCK && FS == K_AUDIO_FS, "tables");

typedef struct { int32_t lvl; uint16_t cnt; uint8_t stage; } e2_t;   /* ENV 2 of a voice: 0 delay, 1 hold, 2 fall, 3 done; lvl Q24 */

typedef struct track {
    K_TRACK_PREFIX;              /* (first: what an outside engine sees, kreisel_dsp.h) */
    voice_t v[NVOICE];
    e2_t e2[NVOICE];
    uint32_t lfo_ph, lfo_rnd;    /* the track LFO: phase, and the S&H value of the cycle (xorshift) */
    int32_t lfo_val, lfo_fade;   /* its value (Q15, bipolar) and the FADE's gain (Q15) */
    int16_t pe_old[K_ENGINE_PARAMS];   /* the engine's own values, for the fade of a switch */
    uint8_t lane[NVOICE];        /* an outside drum: the lane each voice plays (its values: KAudio.lane_p, lane_vals) */
    uint8_t engine, xf_on, xf, rr;
    int32_t glide_last;          /* Kreisel's ANALOG: the pitch its last voice reached (1/16 st << 8), where a glide starts; 0 none */
} track_t;
static track_t trk[NPART];

#include "kreisel_voice_dsp.c"
#include "kreisel_mul.h"         /* the 64-bit products, out of line on the FM-1 (its CPU erratum) */
#include "eng_kanalog.c"        /* Kreisel's own ANALOG */
#include "eng_klofi.c"          /* Kreisel's own LOFI (NOISE folded in) */
#include "eng_kphase.c"         /* Kreisel's own PHASE */
#include "eng_ktrio.c"          /* Kreisel's own TRIO */
#include "eng_korgan.c"         /* Kreisel's own ORGAN */
#if K_WITH_ENSEMBLE
#include "eng_kensemble.c"      /* Kreisel's own ENSEMBLE (not in the MVP) */
#endif
#if K_WITH_VECTOR
#include "eng_kvector.c"        /* Kreisel's own VECTOR: a 2D wavetable morph (not in the MVP) */
#endif
#include "eng_kfm2.c"           /* Kreisel's own 2-operator FM */

/* A track's engine is a plugin (kreisel_plugin.h, kreisel_engine_api.h): what kind it is says how it plays, its DSP how it sounds.
 * The built-in engines are records like any outside plugin's, with their DSP in them (here, where the engines are built); the
 * registry (pagedefs.c) puts them first, in K_ENGINE's order (tests/plugin_test.c checks each record's DSP is its engine's). */
#define K_BUILTIN_SYNTH(i, dsp) {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_SYNTH, &K_ENGINE[i], dsp, FS, 0, 0, 0, 0, 0}
const KPlugin K_BUILTIN_PLUGINS[K_ENGINE_COUNT] = {
    K_BUILTIN_SYNTH(0, &ENG_KANALOG), K_BUILTIN_SYNTH(1, &ENG_KPHASE), K_BUILTIN_SYNTH(2, &ENG_KTRIO), K_BUILTIN_SYNTH(3, &ENG_KLOFI),
    K_BUILTIN_SYNTH(4, &ENG_KORGAN),
#if K_WITH_ENSEMBLE
    K_BUILTIN_SYNTH(5, &ENG_KENSEMBLE),
#endif
#if K_WITH_VECTOR
    K_BUILTIN_SYNTH(5 + K_WITH_ENSEMBLE, &ENG_KVECTOR),
#endif
    K_BUILTIN_SYNTH(K_ENGINE_COUNT - 2, &ENG_KFM2),
    {sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_DRUM, &K_ENGINE[K_ENGINE_COUNT - 1], 0, FS, 0, 0, 0, 0, 0}};   /* DRUM: its pieces are the host's (drum_kreisel.c) */
_Static_assert(K_ENGINE_COUNT == 7 + K_WITH_ENSEMBLE + K_WITH_VECTOR, "K_BUILTIN_PLUGINS lists every built-in engine");
/* The fallback: a track whose engine is not here (a plugin missing, or a synth with no DSP on this device) plays a plain sine, so
 * its notes are still heard (as a DAW plays a missing instrument's part with a default one); its own values are kept untouched. */
static void ksine_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t ph = v->ph[0], i;
    (void)t;
    for(i = 0; i < n; i++) { out[i] += voice_amp(mulq15(sine_i(ph), 14000), m, i); ph += m->inc; }
    v->ph[0] = ph;
}
static const engine_t ENG_KSINE = {.name = "SINE", .render = ksine_render};
#define K_SOUND_FALLBACK 255   /* (a track's sound in the audio: the sine, not an index of the registry) */
static int is_drum(int sound) { return sound != K_SOUND_FALLBACK && k_plugin_at(sound)->kind == K_PLUG_DRUM; }   /* plays lanes, not notes */
static int is_synth_drum(int sound) { return sound != K_SOUND_FALLBACK && k_plugin_at(sound)->kind == K_PLUG_SYNTH && k_plugin_at(sound)->desc->drum; }   /* a synth that says it is a drum: its lanes are notes */
static const engine_t *dsp_of(int sound)   /* a synth's DSP; the sine when there is none here */
{
    const KPlugin *p;
    if(sound == K_SOUND_FALLBACK) return &ENG_KSINE;
    p = k_plugin_at(sound);
    if(p->kind != K_PLUG_SYNTH) return 0;
    return p->dsp ? (const engine_t *)p->dsp : &ENG_KSINE;
}

static int bound(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

/* one KAudio plays at a time (the engines' state is file-level): another one starts from silence */
static KAudio *g_owner;
static uint32_t vage;                                   /* voice ages */

static void bind(KAudio *a)
{
    if (g_owner == a) return;
    memset(trk, 0, sizeof trk);
    memset(ko_t, 0, sizeof ko_t);                       /* ORGAN's own state */
#if K_WITH_VECTOR
    memset(kv_t, 0, sizeof kv_t);                       /* VECTOR's cached wavetables */
#endif
    memset(ko_v, 0, sizeof ko_v);
#if K_WITH_ENSEMBLE
    memset(ke_ch, 0, sizeof ke_ch); /* ENSEMBLE's one delay per track */
#endif
    memset(a->drum, 0, sizeof a->drum);
    memset(a->voices, 0, sizeof a->voices);
    for(int t = 0; t < NPART; t++) trk[t].lfo_rnd = 0x9E3779B9u * (uint32_t)(t + 1);
    a->bpos = K_AUDIO_BLOCK;
    vage = 0;
    k_fx_bind(a);
    g_owner = a;
}

void k_audio_init(KAudio *a, uint32_t rate)
{
    int t;
    memset(a, 0, sizeof(*a));
    a->rate = rate ? rate : 48000u;
    a->bpm = 120;
    a->current_step = -1;
    a->bpos = K_AUDIO_BLOCK;
    a->rng = 0x9E3779B9u;
    for(t = 0; t < K_TRACKS; ++t) {
        a->tp[t][P_SUS] = 64;
        a->tp[t][P_REL] = 60;
    }
    if(a->rate != FS && (a->rate > FS ? a->rate - FS : FS - a->rate) * 500u > FS) {
        /* the tables are for 44100: the pitch is moved by the ratio (1/16 semitone, and the rest as the engines' "fine") */
        float s16 = 192.f * k_log2f((float)FS / (float)a->rate), fl = k_floorf(s16);
        a->tune16 = (int32_t)fl;
        a->tune_fine = (int32_t)((s16 - fl) * 14.81f + .5f);     /* 2^(1/192) - 1 = 14.81 / 4096 */
    }
    if(g_owner == a) g_owner = 0;
    k_fx_forget();
}

/* ------------------------------------------------------------ the sequencer --- */
/* ENV 1 (0..100) as the tables' 0..127: the attack on a squared curve (the default 50 is ~10 ms), the others linear */
static int16_t env_quad(int v) { v = bound(v, 0, 100); return (int16_t)((v * v * 127 + 5000) / 10000); }
static int16_t env_lin(int v) { return (int16_t)((bound(v, 0, 100) * 127 + 50) / 100); }
static int16_t bipolar(int v) { return (int16_t)bound((bound(v, 0, 100) - 50) * 64 / 50, -64, 63); }   /* 50 = none */

/* every voice of a track lets go (a stop, a change of sound): they ring out through their releases */
static void release_track(KAudio *a, int t)
{
    uint32_t k;
    bind(a);
    for(k = 0; k < NVOICE; k++) {
        voice_t *v = &trk[t].v[k];
        if(v->active && v->gate) { v->gate = 0; v->stage = 3; }
    }
}

static void arp_mode_switch(KAudio *a, int t, int old_mode, int new_mode);
static void arp_release(KAudio *a, int t);
/* Caller holds the audio-device lock. Nothing in the callback reads mutable KApp. */
/* The scope (a plugin's K_W_SCOPE): the track in hand as the host hears it, after its engine and inserts, before its fader;
 * every 4th sample, 8 bits. The render writes, the UI reads (a torn read is a glitch on the screen, nothing more). */
#define SCOPE_N 128
static int8_t scope_buf[SCOPE_N];
static volatile uint8_t scope_w;
static uint8_t scope_t, scope_d;
static void scope_take(const int32_t *b, uint32_t n)
{
    uint32_t i;
    uint8_t w = scope_w, d = scope_d;
    for(i = 0; i < n; i++) {
        if(++d < 4) continue;
        d = 0;
        {
            const int32_t x = b[i] >> 5;   /* (a loud track flattens at the top: the picture, not the sound) */
            scope_buf[w] = (int8_t)(x > 127 ? 127 : x < -127 ? -127 : x);
        }
        w = (uint8_t)((w + 1) & (SCOPE_N - 1));
    }
    scope_d = d;
    scope_w = w;
}
int k_audio_scope(int8_t *dst, int n)
{
    const int w = scope_w;
    int k, start = 0, got;
    if(!dst || n <= 0) return 0;
    if(n > SCOPE_N / 2) n = SCOPE_N / 2;
    /* the oldest points it may start at: a rising zero crossing among them holds the picture still */
    for(k = 1; k < SCOPE_N - n; k++) {
        const int at = (w + k) & (SCOPE_N - 1);
        if(scope_buf[(at - 1) & (SCOPE_N - 1)] < 0 && scope_buf[at] >= 0) { start = k; break; }
    }
    if(!start) start = SCOPE_N - n;
    for(got = 0; got < n; got++) dst[got] = scope_buf[(w + start + got) & (SCOPE_N - 1)];
    return got;
}

void k_audio_sync(KAudio *a, const KApp *p)
{
    int t, s, i, lane;
    int want = p->playing != 0;
    bind(a);
    for(t = 0; t < K_TRACKS; t++) a->bend[t] = p->bend[t];
    scope_t = (uint8_t)(p->track >= 0 && p->track < K_TRACKS ? p->track : 0); /* effect instances and synth voices share the same single-audio-owner rule */
    for(i = 0; i < 2; i++) {   /* the buses' TONE and RTN (BUS 1 / BUS 2's third and fourth: one pair for the song) */
        const KVal *bp = p->params[0][i ? K_PAGE_BUS2 : K_PAGE_BUS1];
        a->bus_tone_q15[i] = 1500 + bound(bp[2], 0, 100) * 312;   /* 0: dark (~350 Hz) .. 100: open */
        if (!(a->bus_set >> i & 1u) || bound(bp[3], 0, 100) != a->bus_ret_v[i]) {   /* RTN: a fader on the dB scale (only when it moves: powf) */
            const int v = bound(bp[3], 0, 100);
            a->bus_ret_q12[i] = v ? (int32_t)(powf(10.f, k_level_db(v) / 20.f) * 4096.f) : 0;
            a->bus_ret_v[i] = (uint8_t)v;
            a->bus_set |= (uint8_t)(1u << i);
        }
    }
    for(t = 0; t < K_TRACKS; ++t) {
        int old_sound = a->sound[t];
        int sound = k_track_missing(p, t) ? K_SOUND_FALLBACK : bound(p->sound[t], 0, k_engine_count() - 1);   /* a missing plugin: the sine */
        int16_t *tp = a->tp[t];
        int f;
        if(old_sound != sound) {
            release_track(a, t);
            memset(a->arp_live[t], 0, sizeof a->arp_live[t]);
            memset(a->arp_seq[t], 0, sizeof a->arp_seq[t]);
        }
        a->sound[t] = sound;
        { int q; for(q = 0; q < 4; ++q) { a->steps[t][q] = p->steps[t][q]; a->chance[t][q] = p->chance[t][q]; } }
        if(!a->song_on) a->play_pat[t] = (uint8_t)bound(p->pattern[t], 0, 3);   /* (a song: the scene's, set at the bar) */
        f = k_track_audible(p, t) ? bound(p->params[t][K_PAGE_MIXER][0], 0, 100) : 0;   /* muted, or not soloed: silent */
        if (f != a->fader[t] || bound(p->params[t][K_PAGE_MIXER][1], 0, 100) != a->pan[t] || !(a->gains_set >> t & 1u)) {   /* (only when they move: soft
                                         * float powf / cosf / sinf on the FM-1, with its audio interrupt held off meanwhile) */
            const int pan = bound(p->params[t][K_PAGE_MIXER][1], 0, 100);
            const float db = k_level_db(f);   /* the fader in dB, as the mixer draws it (kreisel_level.h) */
            const float g = f ? powf(10.f, db / 20.f) * 4096.f : 0.f;
            a->gain_l[t] = (uint16_t)(cosf((K_PI_2) * (float)pan / 100.f) * g);
            a->gain_r[t] = (uint16_t)(sinf((K_PI_2) * (float)pan / 100.f) * g);
            a->gain_m[t] = (uint16_t)g;   /* (the sends: after the fader, not panned) */
            a->fader[t] = f;
            a->pan[t] = pan;
            a->gains_set |= 1u << t;
        }
        a->send_q12[t][0] = (uint16_t)(bound(p->params[t][K_PAGE_MIXER][2], 0, 100) * 4096 / 100);   /* B1, B2: 0 .. unity */
        a->send_q12[t][1] = (uint16_t)(bound(p->params[t][K_PAGE_MIXER][3], 0, 100) * 4096 / 100);
        {   /* the track's notes, every pattern's, packed (a zero length after the last): a song changes pattern at the bar, in here */
            int n = 0;
            for(s = 0; s < K_ROLL_NOTES; ++s) {
                const KNote *k = &p->roll[t][s];
                if(k->len && (k->at >> 4) < 4) a->roll[t][n++] = (KNote){k->at, (uint8_t)bound(k->len, 1, 16), (uint8_t)(k->note & 127), (uint8_t)bound(k->vel, 1, 127)};
            }
            if(n < K_ROLL_NOTES) a->roll[t][n].len = 0;
        }
        {
            const int m = sound == K_SOUND_FALLBACK ? 0 : k_plugin_lane_params(sound);   /* an outside drum: its values each lane */
            for(lane = 0; lane < K_LANES; ++lane) {
                for(s = 0; s < 4; ++s) a->lane_on[t][s][lane] = k_lanes(p, t, s)[lane];
                for(s = 0; s < K_LANE_VALS; ++s)
                    a->lane_p[t][lane][s] = (int16_t)(s < m ? k_lane_val(p, t, lane, s) : bound(p->lane_p[t][lane][s], 0, !s ? K_DRUM_PIECES - 1 : s >= K_LANE_ENV ? 100 : 127));
            }
        }
        /* PHASE has two dedicated ADSRs: ENV1 is DCW, ENV2 is DCA.
         * Every other engine keeps its existing ENV1 amplitude and ENV2 auxiliary mapping. */
        const int phase = sound != K_SOUND_FALLBACK && k_plugin_at(sound)->desc->uid == K_UID('K','P','H','S');
        /* an organ (KEngine.gate) has no ENV or LFO pages: it plays as a gate, whatever the track's ENV and LFO pages hold */
        const int organ = sound != K_SOUND_FALLBACK && sound < K_ENGINE_COUNT && k_engine_at(sound)->gate;
        const int nomod = organ || is_drum(sound);   /* ORGAN, DRUM: no ENV or LFO page, so none of theirs reaches the sound */
        static const KVal GATE[4] = {0, 50, 100, 10};
        const KVal *amp_env = organ ? GATE : p->params[t][phase ? K_PAGE_ENV2 : K_PAGE_ENV1];
        tp[P_ATK] = env_quad(amp_env[0]);
        tp[P_DEC] = env_lin(amp_env[1]);
        tp[P_SUS] = env_lin(amp_env[2]);
        tp[P_REL] = env_lin(amp_env[3]);
        tp[P_ED_PIT] = bipolar(p->params[t][K_PAGE_ENV_DEST][0]);   /* ENV DEST: how far the envelope moves pitch, filter, shape */
        tp[P_ED_FLT] = bipolar(p->params[t][K_PAGE_ENV_DEST][1]);
        tp[P_ED_SHP] = bipolar(p->params[t][K_PAGE_ENV_DEST][2]);
        tp[P_FTYPE] = 0;
        tp[P_LRATE] = (int16_t)bound(p->params[t][K_PAGE_LFO][0], 0, 127);        /* LFO: RATE WAVE PHS FADE */
        tp[P_LWAVE] = (int16_t)bound(p->params[t][K_PAGE_LFO][1], 0, 4);
        tp[P_LPHASE] = (int16_t)bound(p->params[t][K_PAGE_LFO][2], 0, 127);
        tp[P_LFADE] = (int16_t)bound(p->params[t][K_PAGE_LFO][3], 0, 127);
        tp[P_LD_PIT] = (int16_t)bound(p->params[t][K_PAGE_LFO][4], -64, 63);                       /* its routings (Felucca's LFO DEST): items 4..7, -64..63, 0 = none */
        tp[P_LD_FLT] = (int16_t)bound(p->params[t][K_PAGE_LFO][5], -64, 63);
        tp[P_LD_SHP] = (int16_t)bound(p->params[t][K_PAGE_LFO][6], -64, 63);
        tp[P_LD_AMP] = (int16_t)bound(p->params[t][K_PAGE_LFO][7], -64, 63);
        if (nomod) tp[P_ED_PIT] = tp[P_ED_FLT] = tp[P_ED_SHP] = tp[P_LD_PIT] = tp[P_LD_FLT] = tp[P_LD_SHP] = tp[P_LD_AMP] = 0;
        if (phase) {
            /* These four control slots are repurposed by the PHASE DSP as
             * DCW A/D/S/R. Never run generic ENV2's delay/hold/decay or amp route. */
            const KVal *dcw = p->params[t][K_PAGE_ENV1];
            tp[P_E2_DLY] = env_quad(dcw[0]);
            tp[P_E2_HOLD] = env_lin(dcw[1]);
            tp[P_E2_CRV] = env_lin(dcw[2]);
            tp[P_E2_AMT] = env_lin(dcw[3]);
            tp[P_ED2_AMP] = 0;
            {   /* the DCO envelope, ENV 2's items 4 .. 7 (ENV 2's A D S R are the DCA's: the amplitude above) */
                const KVal *dco = p->params[t][K_PAGE_ENV2] + 4;
                tp[P_X_A] = env_quad(dco[0]); tp[P_X_D] = env_lin(dco[1]); tp[P_X_S] = env_lin(dco[2]); tp[P_X_R] = env_lin(dco[3]);
            }
        } else {
            tp[P_E2_DLY] = (int16_t)bound(p->params[t][K_PAGE_ENV2][0], 0, 100);       /* ENV 2: DLY HOLD CRV AMT */
            tp[P_E2_HOLD] = (int16_t)bound(p->params[t][K_PAGE_ENV2][1], 0, 100);
            tp[P_E2_CRV] = env_lin(p->params[t][K_PAGE_ENV2][2]);
            tp[P_E2_AMT] = bipolar(p->params[t][K_PAGE_ENV2][3]);
            tp[P_ED2_AMP] = organ ? 0 : bipolar(p->params[t][K_PAGE_ENV_DEST][3]);
            if (organ) tp[P_E2_AMT] = 0;
        }
        if(!is_drum(sound)) {
            a->eng_req[t] = (uint8_t)sound;
            for(i = 0; i < K_ENGINE_PARAMS; ++i) tp[P_E0 + i] = (int16_t)p->params[t][K_PAGE_SOUND][4 + i];
            for(i = 0; i < K_LANE_VALS && sound != K_SOUND_FALLBACK && i < k_plugin_lane_params(sound); ++i) tp[P_E0 + i] = a->lane_p[t][0][i];   /* (an outside
                                         * drum: lane 1's outside its voices, in its block and post hooks; a voice has its lane's) */
        } else {
            for(i = 0; i < 4; ++i) a->dp[t][i] = (uint8_t)bound(p->params[t][K_PAGE_SOUND][4 + 10 + i], 0, 127);   /* the kit: TUNE DCAY CRSH HUMN (its first ten are the lanes') */
            a->dp[t][4] = (uint8_t)bound(p->params[t][K_PAGE_LFO][8], 0, 8);   /* the LFO's lane: 0 all */
        }
        {
            int mode = is_drum(sound) || is_synth_drum(sound) ? 0 : bound(p->params[t][K_PAGE_ARP][0], 0, 14);
            if (old_sound != sound || a->arp_mode[t] != mode)
                arp_mode_switch(a, t, a->arp_mode[t], mode);
            a->arp_mode[t] = (uint8_t)mode;
            a->arp_rate[t] = (uint8_t)bound(p->params[t][K_PAGE_ARP][1], 0, 9);
            a->arp_oct[t] = (uint8_t)bound(p->params[t][K_PAGE_ARP][2], 1, 4);
            a->arp_gate[t] = (uint8_t)bound(p->params[t][K_PAGE_ARP][3], 1, 127);
        }
    }
    a->bpm = bound(p->bpm, 40, 240);
    a->swing = bound(p->swing, 0, 100);
    a->metro = (uint8_t)(p->set_v[K_SET_METRO] && p->recording);   /* (the settings' Metronome: only while REC) */
    a->click_kind = (uint8_t)(p->set_v[K_SET_CLICK] < 3 ? p->set_v[K_SET_CLICK] : 0);
    a->master = bound(p->master, 0, 100);
    a->master_q12 = (uint16_t)((uint32_t)(a->master * a->master * a->master) * 4096u / 1000000u);   /* an audio taper: the cube of
                                                 * its 0..1 (50 is -18 dB, 20 is -42 dB), so it comes up from silence gently, as a
                                                 * volume pot does, not as a straight line, loud at once */
    {
        const int ext = p->clock_src > 0;
        const int restart = p->clk.gen != a->clk_gen;   /* an external Start or Continue, even while playing */
        if(ext != a->ext) {
            a->ext = ext; a->frames_to_step = 0; a->clk_acc = 0;
            for (t = 0; t < K_TRACKS; t++) {
                a->arp_ext_acc[t] = a->arp_pending[t] = 0;
                a->arp_next_q16[t] = 0;
            }
        }
        a->clk_gen = p->clk.gen;
        if(!p->song_play || !want) { a->song_on = 0; a->song_end = 0; }
        else if(!a->song_on && (want != a->playing || !a->playing)) {   /* a song started with PLAY: from its scene in hand */
            int sc, tr;
            memcpy(a->song, p->song, sizeof a->song);
            a->scene_last = -1;
            for(sc = 0; sc < K_SCENES; ++sc) for(tr = 0; tr < K_TRACKS; ++tr) if(p->song[sc][tr] < 4) a->scene_last = sc;
            a->scene = bound(p->song_scene, 0, K_SCENES - 1);
            a->song_on = a->scene_last >= a->scene;
            a->song_end = 0; a->song_first = 1;
            if(a->song_on) for(tr = 0; tr < K_TRACKS; ++tr) a->play_pat[tr] = a->song[a->scene][tr] > 4 ? 4 : a->song[a->scene][tr];
        }
        if(a->song_on) memcpy(a->song, p->song, sizeof a->song);   /* (cells changed while it plays: heard from the next bar) */
        if(want != a->playing || (want && ext && restart)) {
            a->playing = want;
            a->current_step = -1;
            if(want) {
                /* PLAY always starts from step zero, at the next rendered audio sample. A Continue keeps the step. */
                if(!(ext && p->clk.cont)) a->next_step = 0;
                a->frames_to_step = 0;
                a->count_left = (uint8_t)(p->count_in && !ext ? K_STEPS : 0);   /* (MET COUNT, armed: a bar of clicks first) */
                a->clk_acc = 5;          /* external: the first clock fires the step */
                if (!(ext && p->clk.cont)) for (t = 0; t < K_TRACKS; t++) {
                    arp_release(a, t);
                    a->arp_step[t] = a->arp_walk[t] = 0;
                    a->arp_ext_acc[t] = a->arp_pending[t] = 0;
                    a->arp_next_q16[t] = 0;
                }
            } else {
                a->count_left = 0;
                for(t = 0; t < K_TRACKS; ++t) {
                    release_track(a, t);
                    memset(a->arp_seq[t], 0, sizeof a->arp_seq[t]);
                    a->arp_pending[t] = 0;
                    a->arp_next_q16[t] = 0;
                }
            }
        }
    }
    k_fx_sync(p);
}

void k_audio_clock_tick(KAudio *a, uint32_t n)
{
    if(!a->ext || !a->playing) return;
    a->clk_acc = n >= 24u - a->clk_acc ? 24u : a->clk_acc + n; /* no burst after a stalled consumer */
    for (int t = 0; t < K_TRACKS; t++) if (a->arp_mode[t]) {
        uint32_t pulses = (uint32_t)a->arp_ext_acc[t] + n;   /* (32 bits: no 64-bit arithmetic here, kreisel_mul.h) */
        unsigned div = K_ARP_CLOCKS[a->arp_rate[t]];
        if (pulses >= div) a->arp_pending[t] = 1;
        a->arp_ext_acc[t] = (uint16_t)(pulses % div);
    }
}

/* the length of a step in frames, Q16. Swing stretches the even 16th and shortens the following odd 16th; the pair is unchanged. */
static int64_t duration_q16(const KAudio *a, int step)
{
    const int64_t sw = 1000 + ((step & 1) ? -(int64_t)a->swing * 4 : (int64_t)a->swing * 4);   /* up to +/- 40 % */
    return (int64_t)(k_umul(a->rate * 15u, (uint32_t)(65536 * sw)) / (uint32_t)(a->bpm * 1000));   /* (sw 600..1400) */
}

/* ------------------------------------------------------------ synth voices --- */
/* the voices a track may take (1 .. NVOICE): the platform lowers it when the sound does not keep up (the FM-1's load guard),
 * so the work is thinned before the sound has to stop; the voices past it end as released ones, new notes do not take them */
static volatile uint32_t g_vcap = NVOICE;
void k_audio_song_follow(const KAudio *a, KApp *p)
{
    int t;
    if(!p->song_play) return;
    if(a->song_end || (!a->song_on && p->playing && a->playing)) {   /* the end of the last scene (or nothing there to play): stopped */
        p->playing = 0; p->song_play = 0; p->step = -1; p->beat = -1;
        return;
    }
    if(!a->song_on) return;
    p->song_scene = a->scene;
    for(t = 0; t < K_TRACKS; ++t) if(a->play_pat[t] < 4) k_pattern_set(p, t, a->play_pat[t]);   /* (k_pattern_set: a drum track's lanes swap with it) */   /* (the screen shows what plays) */
}
static uint32_t g_syscap = K_SYS_VOICES;
void k_audio_sys_voices(int n) { g_syscap = n < 1 ? 1u : n > (int)(NVOICE * NPART) ? NVOICE * NPART : (uint32_t)n; }
int k_audio_voices_on(void) { uint32_t q, j; int on = 0; for(q = 0; q < NPART; q++) for(j = 0; j < NVOICE; j++) on += trk[q].v[j].active && trk[q].v[j].stage != 4; return on; }
void k_audio_voice_cap(int n) { g_vcap = n < 1 ? 1u : n > (int)NVOICE ? NVOICE : (uint32_t)n; }
int k_audio_voice_cap_now(void) { return (int)g_vcap; }
static uint32_t trk_nvoice(void) { return NVOICE; }

/* a free voice (round robin), else the oldest released, else the oldest held one but the lowest note (the bass);
 * a voice already playing the note is restarted. Only the first g_vcap: a voice past the cap is let go (released) as it is met */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    const uint32_t cap = g_vcap;
    uint32_t i, k, best = NVOICE, low = NVOICE;
    for(i = cap; i < NVOICE; i++) if(t->v[i].active && t->v[i].gate) { t->v[i].gate = 0; t->v[i].stage = 3; }   /* (over the cap: into their release) */
    for(i = 0; i < cap; i++)
        if(t->v[i].active && t->v[i].note == note) return &t->v[i];
    for(i = 0; i < cap && best == NVOICE; i++) {
        k = (t->rr + i) % cap;   /* (rr may stand past a cap just lowered) */
        if(!t->v[k].active) best = k;
    }
    if(best < NVOICE) {
        t->rr = (uint8_t)(best + 1u >= cap ? 0u : best + 1u);
        if(g_syscap < NVOICE * NPART) {   /* the system's voices all sounding (K_SYS_VOICES, every track's together): the oldest gives way, a released one first */
            uint32_t q, j, on = 0;
            voice_t *old = 0;
            int old_gate = 1;
            for(q = 0; q < NPART; q++)
                for(j = 0; j < NVOICE; j++) {
                    voice_t *v = &trk[q].v[j];
                    if(!v->active || v->stage == 4) continue;   /* (one given way already: fading out this block) */
                    on++;
                    if(!old || (old_gate && !v->gate) || (old_gate == (v->gate != 0) && v->age < old->age)) { old = v; old_gate = v->gate != 0; }
                }
            if(on >= g_syscap && old) { old->gate = 0; old->stage = 4; }   /* faded out over a few blocks (env_tick's 4), not cut: no click */
        }
        return &t->v[best];
    }
    for(i = 0; i < cap; i++)
        if(t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note)) low = i;
    for(i = 0; i < cap; i++)
        if(!t->v[i].gate && (best == NVOICE || t->v[i].age < t->v[best].age)) best = i;
    if(best == NVOICE)
        for(i = 0; i < cap; i++)
            if((i != low || cap == 1u) && (best == NVOICE || t->v[i].age < t->v[best].age)) best = i;
    return &t->v[best == NVOICE ? 0 : best];
}

/* ENV 2's delay and hold: 0..100 -> 0 .. 2 s on a cubic curve (50 is 0.25 s), in blocks; (v^3 * 2889) >> 20 = v^3 * 2755 / 1e6 */
static uint16_t e2_blocks(int v) { return (uint16_t)(((uint32_t)(v * v * v) * 2889u) >> 20); }

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel)
{
    const engine_t *e = dsp_of(t->engine);
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], k;
    int32_t s[8];
    if(!e) return;                                      /* (tone_on keeps a track with no DSP from here) */
    memcpy(s, v->s, sizeof s);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->fine = 0;
    t->e2[v - t->v] = (e2_t){0, e2_blocks(t->p[P_E2_DLY]), 0};
    if(!sounding) v->env = v->env_out = 0;            /* sounding: the attack starts from the current level */
    if(e->note_on) e->note_on(t, v);
    if(sounding && !e->sampled) {                     /* retrigger / steal: keep phases and filter states (resetting clicks) */
        v->ph[0] = ph0; v->ph[1] = ph1; v->ph[2] = ph2;
        for(k = 0; k < 8u; k++)
            if(k != 2u && k != 3u && (e->keep >> k & 1u)) v->s[k] = s[k];
    }
}

static void load_params(KAudio *a, int ti)
{
    track_t *t = &trk[ti];
    uint32_t i;
    for(i = 0; i < P_COUNT; i++) t->p[i] = a->tp[ti][i];
    for(i = 0; i < K_ENGINE_PARAMS; i++) t->pe_old[i] = t->p[P_E0 + i];
}

/* an outside drum's voice: its lane's values in the part's p, for the engine's calls on that voice (n of them; 0: not a drum's) */
static void lane_vals(const KAudio *a, track_t *tr, int t, int lane, int n)
{
    int k;
    for(k = 0; k < n; k++) tr->p[P_E0 + k] = a->lane_p[t][lane][k];
}
/* a note on a synth track: voice index, or -1 when the track is changing its engine (the note is dropped). lane: an outside drum's
 * lane the note plays (its values), -1 none */
static int tone_on_lane(KAudio *a, int t, int note, int vel, int lane)
{
    track_t *tr = &trk[t];
    voice_t *v;
    if(is_drum(a->sound[t]) || tr->xf_on || !dsp_of(a->eng_req[t])) return -1;   /* (a synth with no DSP here: silent) */
    if(tr->engine != a->eng_req[t]) {                  /* a new engine takes a silent track at once, else the note is dropped */
        uint32_t i, any = 0;
        for(i = 0; i < NVOICE; i++) any |= tr->v[i].active;
        if(any) return -1;
        tr->engine = a->eng_req[t];
    }
    load_params(a, t);                                 /* (note_on may read them: the block has not run yet) */
    {   /* a fresh phrase (no key held): the LFO restarts at PHS (voice.c track_note_on, TRIG NOTE). The FADE starts again on every
         * note, held keys or not: a sequence's notes come before the last one's off, and never faded */
        uint32_t i, held = 0;
        for(i = 0; i < NVOICE; i++) held |= tr->v[i].gate;
        if(!held) tr->lfo_ph = (uint32_t)tr->p[P_LPHASE] << 25;
        tr->lfo_fade = 0;
    }
    v = voice_alloc(tr, (uint32_t)note);
    {
        const int m = lane >= 0 && lane < K_LANES ? k_plugin_lane_params(tr->engine) : 0;
        tr->lane[v - tr->v] = (uint8_t)(m ? lane : 0);
        if(m) lane_vals(a, tr, t, lane, m);            /* (its note_on reads its lane's) */
        voice_start(tr, v, (uint32_t)note, (uint32_t)vel);
        if(m) load_params(a, t);
    }
    return (int)(v - tr->v);
}
static int tone_on(KAudio *a, int t, int note, int vel) { return tone_on_lane(a, t, note, vel, -1); }

static void mo_arp_on(KAudio *a, int t, int k, int note, int vel);   /* (MIDI out: below) */
static void mo_arp_off(KAudio *a, int t, int k);
#include "kreisel_arp.c"  /* live and sequenced ARP, after tone_on */

static void start_tone(KAudio *a, int t, int note, int velocity, int length, int64_t step_q16, int lane)
{
    int k;
    if(!velocity) return;
    k = tone_on_lane(a, t, note, velocity, lane);
    a->triggers[t]++;
    a->last_trigger_frame[t] = a->frame;
    if(k >= 0) {
        /* released 0.08 of a step before the next one starts: (length - .08) steps */
        const uint64_t steps16 = (uint64_t)((int64_t)length * 128 - 10);
        a->rel_at[t][k] = a->frame + (k_umul((uint32_t)((uint64_t)step_q16 >> 16), (uint32_t)steps16) >> 7);
    }
}

static void drum_on(KAudio *a, int t, int lane);
int k_audio_arp_last(const KAudio *a, int t, uint32_t *count)
{
    if (t < 0 || t >= K_TRACKS) return -1;
    if (count) *count = (uint32_t)a->triggers[t];
    return a->arp_mode[t] ? a->arp_last[t] : -1;
}
void k_audio_note(KAudio *a, int t, int note, int velocity, int on)
{
    int k, lane = -1;
    if(t < 0 || t >= K_TRACKS || note < 0 || note > 127) return;
    bind(a);
    if(is_drum(a->sound[t])) {                               /* a drum track: the note is the lane to hit */
        if(on && note < K_LANES) drum_on(a, t, note);
        return;
    }
    if(is_synth_drum(a->sound[t])) {                         /* a synth that is a drum: the lane's note, with its values */
        if(note >= K_LANES) return;
        lane = note;
        note = k_plugin_lane_note(a->sound[t], note);
    }
    if(!on) {
        track_t *tr = &trk[t];
        arp_live_off(a, t, note);
        if (a->arp_mode[t]) {
            if (!arp_has(a, t)) {
                arp_release(a, t);
                a->arp_next_q16[t] = 0;
            }
            return;
        }
        for(k = 0; k < NVOICE; ++k) {
            voice_t *v = &tr->v[k];
            if(v->active && v->gate && v->note == (uint8_t)note && a->rel_at[t][k] == ~(uint64_t)0) { v->gate = 0; v->stage = 3; }
        }
        return;
    }
    if(is_drum(a->sound[t])) return;                         /* the drum track has no pitch */
    arp_live_note(a, t, note, velocity > 0 ? bound(velocity, 1, 127) : 100);
    if (a->arp_mode[t]) return;                     /* the arp owns voice starts */
    k = tone_on_lane(a, t, note, velocity > 0 ? velocity : 100, lane);
    if(k >= 0) a->rel_at[t][k] = ~(uint64_t)0;               /* held until its off */
}

/* one control tick of the amplitude envelope; returns Q15. Ported from voice.c env_tick (Felucca's ADSR tables, no ENV SYNC). */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch(v->stage) {
    case 1:
        v->env += (int32_t)ENV_LIN[p[P_ATK] & 127];
        if(v->env >= (1 << 24)) { v->env = 1 << 24; v->stage = 2; }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ENV_EXP[p[P_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[P_REL] & 127]);
        if(v->env < (1 << 12)) { v->env = 0; v->stage = 0; v->active = 0; }
        break;
    default:                                            /* 4: given up (the system's voices all taken): halved a block, out under -36 dB,
                                                         * about 4 ms (one block, 0.7 ms, could click on a low note; 2026-11-12) */
        v->env -= v->env >> 1;
        if(v->env < (1 << 18)) { v->env = 0; v->stage = 0; v->active = 0; }
        break;
    }
    return v->env >> 9;
}

/* one control tick of ENV 2 of voice i: 0 for DLY, 1.0 for HOLD (at 100: as long as the key is held), then an exponential fall
 * at the rate CRV picks from the decay table (as a DEC would). Returns Q15 (0..32767). */
static int32_t env2_tick(track_t *t, uint32_t i, const voice_t *v)
{
    e2_t *e = &t->e2[i];
    const int16_t *p = t->p;
    if(e->stage == 0) {
        if(e->cnt) { e->cnt--; return 0; }
        e->stage = 1;
        e->cnt = e2_blocks(p[P_E2_HOLD]);
    }
    if(e->stage == 1) {
        if(p[P_E2_HOLD] >= 100 ? v->gate : e->cnt) {
            if(p[P_E2_HOLD] < 100) e->cnt--;
            return 32767;
        }
        e->stage = 2;
        e->lvl = 1 << 24;
    }
    if(e->stage == 2) {
        e->lvl -= mulq16(e->lvl, ENV_EXP[p[P_E2_CRV] & 127]);
        if(e->lvl < (1 << 12)) { e->lvl = 0; e->stage = 3; }
        return e->lvl >> 9;
    }
    return 0;
}

/* the track LFO, one tick a block: SIN TRI SAW SQR S&H (a new random value as each cycle begins), FADE as a ramp in */
static int32_t lfo_wave(const track_t *t, uint32_t ph)
{
    switch(t->p[P_LWAVE]) {
    case 1: return osc_tri(ph);
    case 2: return (int32_t)(ph >> 16) - 32768;
    case 3: return ph < 0x80000000u ? 32767 : -32767;
    case 4: return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default: return osc_sine(ph);
    }
}
static void lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += LFO_INC[t->p[P_LRATE] & 127];
    if(t->lfo_ph < old) {                               /* a cycle ends: the next S&H value */
        uint32_t s = t->lfo_rnd ? t->lfo_rnd : 0x9E3779B9u;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        t->lfo_rnd = s;
    }
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if(t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

#define XF_BLOCKS 4u                                    /* an engine switch fades the old voices over 4 blocks */

/* one block of a synth track into out (cleared here): the voices of its engine. Returns the voices rendered. */
static uint32_t track_block(KAudio *a, int ti, int32_t *out)
{
    track_t *t = &trk[ti];
    const int16_t *tp = a->tp[ti];
    const uint32_t want = a->eng_req[ti];
    uint32_t i, nr = 0, fade, any = 0;
    int16_t pe_new[K_ENGINE_PARAMS];
    for(i = 0; i < CTL; i++) out[i] = 0;
    for(i = 0; i < NVOICE; i++) any |= t->v[i].active;
    /* the engine switch (voice.c engine_block): sounding voices fade out on the old engine, then the new one starts */
    if(t->xf_on && !t->xf) {
        for(i = 0; i < NVOICE; i++) { voice_t *v = &t->v[i]; v->active = v->gate = 0; v->stage = 0; v->env = v->env_out = 0; }
        t->engine = (uint8_t)want;
        t->xf_on = 0;
        any = 0;
    } else if(!t->xf_on && t->engine != want) {
        if(any) { t->xf_on = 1; t->xf = XF_BLOCKS; }
        else t->engine = (uint8_t)want;
    }
    fade = t->xf_on;
    for(i = 0; i < P_E0; i++) t->p[i] = tp[i];
    for(i = P_E19 + 1u; i < P_COUNT; i++) t->p[i] = tp[i];
    if(fade) {                                          /* the old engine reads its own values (an index into its tables) */
        for(i = 0; i < K_ENGINE_PARAMS; i++) { pe_new[i] = tp[P_E0 + i]; t->p[P_E0 + i] = t->pe_old[i]; }
    } else {
        for(i = 0; i < K_ENGINE_PARAMS; i++) t->p[P_E0 + i] = t->pe_old[i] = tp[P_E0 + i];
    }
    lfo_tick(t);                                        /* (also with no voice: its phase runs on, as Felucca's) */
    if(any && dsp_of(t->engine)) {
        const engine_t *e = dsp_of(t->engine);
        const int phase = e == &ENG_KPHASE;
        const int16_t *p = t->p;
        const int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
        const int lanes = fade ? 0 : k_plugin_lane_params(t->engine);   /* an outside drum: each voice its lane's values */
        if(e->block) e->block(t);
        for(i = 0; i < NVOICE; i++) {
            voice_t *v = &t->v[i];
            vmod_t m;
            int32_t env, pitch, e2, e2raw;
            if(!v->active) continue;
            if(lanes) lane_vals(a, t, ti, t->lane[i] % K_LANES, lanes);
            env = env_tick(t, v);
            e2raw = phase ? 0 : env2_tick(t, i, v);
            e2 = phase ? 0 : (e2raw * p[P_E2_AMT]) >> 6; /* PHASE uses those fields for DCW ADSR */
            if(e->amp) env = e->amp(t, v, env);          /* the engine's own amplitude curve (LOFI's steps) */
            m.envq15 = env;
            m.amp1 = mulq15(env, (int32_t)v->vel * 258);
            if(p[P_LD_AMP])                              /* LFO to the amplitude: a tremolo, unipolar; the sign flips its phase */
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(bound(abs(p[P_LD_AMP]) * 512, 0, 32767),
                                                      p[P_LD_AMP] > 0 ? (lfo + 32768) >> 1 : 32767 - ((lfo + 32768) >> 1)));
            if(p[P_ED2_AMP] && p[P_E2_AMT])              /* ENV 2 to the amplitude: it opens it (+) or ducks it (-); AMT sets how far */
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(mulq15(bound(abs(p[P_E2_AMT]) * 512, 0, 32767), bound(abs(p[P_ED2_AMP]) * 512, 0, 32767)),
                                                      p[P_ED2_AMP] > 0 ? 32767 - e2raw : e2raw));
            if(fade) m.amp1 = (m.amp1 * (int32_t)(t->xf - 1u)) >> 2;      /* linear to 0 over the fade (XF_BLOCKS 4) */
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
            if(!env && !m.amp0 && v->stage == 2 && !e->sampled) continue;   /* held at a silent sustain */
            pitch = v->pitch_cur + a->tune16 + (a->bend[ti] >> 8) + ((lfo * p[P_LD_PIT] * 3) >> 15) + ((m.envq15 * p[P_ED_PIT] * 3) >> 15)
                    + ((e2 * p[P_ED_PIT] * 3) >> 15);
            m.pitch16 = clamp(pitch, 0, 2047);
            m.inc = pitch_inc((uint32_t)m.pitch16);
            m.fine = v->fine + a->tune_fine + (((a->bend[ti] & 255) * 15) >> 8);   /* (the bend under 1/16 semitone: 14.81 a 1/16) */
            if(m.fine) m.inc += (uint32_t)((int32_t)(m.inc >> 12) * m.fine);
            m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) +
                       (phase ? 0 : ((m.envq15 * p[P_ED_FLT]) >> 7) + ((e2 * p[P_ED_FLT]) >> 7)); 
            /* PHASE's DCW ADSR is evaluated separately in eng_kphase.c,
             * so the DCA envelope does not accidentally bend the waveform. */
            if(v->vel > 110) m.cutoff += (m.envq15 * 24) >> 7;           /* accent opens the filter with the env */
            m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7) + ((e2 * p[P_ED_SHP]) >> 7);
            e->render(t, v, out, CTL, &m);
            nr++;
        }
        for(i = 0; i < (uint32_t)lanes; i++) t->p[P_E0 + i] = tp[P_E0 + i];   /* (lane 1's again: the post hook's) */
    }
    /* ENSEMBLE's chorus is after the sum of voices, never per voice. */
#if K_WITH_ENSEMBLE
    const int kens = k_engine_uid(t->engine) == K_UID('K', 'E', 'N', 'S');
    if (kens) kensemble_post(t, (uint32_t)ti, out, CTL);
#else
    const int kens = 0;
#endif
    if(nr || kens) {   /* the engines brought to one loudness: a three-note chord at their defaults, ~-16 dB RMS as ANALOG, PHASE, ORGAN
                * (measured 2026-11-06: TRIO -20.8, LOFI -23.3, ENSEMBLE -33.2, FM2 -12.5; FM2 cut only 1 dB). ENSEMBLE's chorus
                * keeps its gain after the last voice (its tail) */
        const uint32_t u = k_engine_uid(t->engine);
        const int32_t g = u == K_UID('K','T','R','I') ? 6900 : u == K_UID('K','L','O','F') ? 9200 : u == K_UID('K','E','N','S') ? 30000 :
                          u == K_UID('K','F','M','2') ? 3600 : 4096;   /* Q12 (FM2: its presets, their index falling, are darker than its defaults) */
        if(g != 4096) for(i = 0; i < CTL; i++) out[i] = k_mulsh(out[i], g, 12);
    }
#if K_WITH_ENSEMBLE
    if (!kens) kensemble_drain((uint32_t)ti, out, CTL);   /* (a switch away: the tail rings out, faded, at ENSEMBLE's own gain) */
#endif
    if(fade) {
        for(i = 0; i < K_ENGINE_PARAMS; i++) t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}

#include "drum_kreisel.c"   /* the drum voices: a lane's piece */

/* ------------------------------------------------------------ the mix --- */
/* the master's DC blocker: the DC is followed as a sum (4096 x the DC, in 64 bits: nothing lost to rounding), moving 1/4096 of
 * the way to the sample each sample: a one-pole high-pass at ~1.7 Hz. Adds and shifts only (no multiply: the FM-1's erratum). */
static inline int32_t dc_block(int32_t x, int64_t *acc)
{
    *acc += x - (int32_t)(*acc >> 12);
    return x - (int32_t)(*acc >> 12);
}

/* the metronome (2026-11-12): a click on each beat, the bar's first higher and louder; a sine that falls in ~20 ms, mixed in before
 * the master (its level follows MASTER), never sent to MIDI out */
static void click_on(KAudio *a, int accent)   /* (the settings' Sound: Beep a sine G6 / C6 ~20 ms; Click a noise burst ~3 ms; Wood a sine
                                              * two octaves up, ~6 ms) */
{
    static const uint16_t HZ[3][2] = {{1047, 1568}, {0, 0}, {2093, 3136}};
    const int k = a->click_kind < 3 ? a->click_kind : 0;
    a->click_ph = 0;
    a->click_inc = HZ[k][0] ? (uint32_t)(((uint64_t)HZ[k][accent ? 1 : 0] << 32) / a->rate) : 0;
    a->click_env = (k == 1 ? (accent ? 16000 : 10000) : accent ? 12000 : 7000);
}
static void click_mix(KAudio *a, int32_t *l, int32_t *r)
{
    uint32_t i;
    for(i = 0; i < CTL && a->click_env; i++) {
        int32_t s;
        if(a->click_kind == 1) { a->click_ph = a->click_ph * 1664525u + 1013904223u; s = ((int32_t)(a->click_ph >> 16) - 32768) * a->click_env >> 15; }   /* (Click: noise) */
        else { s = (sine_i(a->click_ph) * a->click_env) >> 15; a->click_ph += a->click_inc; }
        a->click_env -= (a->click_env >> (a->click_kind == 0 ? 8 : a->click_kind == 1 ? 5 : 6)) + 1;   /* (Beep ~20 ms, Click ~3, Wood ~6) */
        if(a->click_env < 64) a->click_env = 0;
        l[i] += s; r[i] += s;
    }
}

/* the master limiter (2026-11-12), on the block as it leaves (Q15, 32767 full scale), both sides by one gain. Below its ceiling
 * (-1 dBFS) it does nothing: the samples go through as they are. A block that would pass it takes the gain that brings its peak
 * to the ceiling (one divide a block, none a sample): the gain glides there from where it was (over 8 samples), and a sample that
 * the glide leaves over the ceiling is held at the ceiling (a few samples, by a little): fast, no delay. (It once took the block's
 * gain at once there: a step in the whole wave's level, heard as a click when a chord came in loud.) Then it
 * comes back up, 1/128 of the way a block (~90 ms). Its reduction is kept (0: none, as k_audio_init leaves it). */
#define LIM_CEIL 29204   /* -1 dBFS */
static void limit_block(KAudio *a, int32_t *l, int32_t *r)
{
    const int32_t g0 = 65536 - (int32_t)a->lim_red;
    int32_t g1, peak = 0;
    uint32_t i;
    for(i = 0; i < CTL; i++) {
        const int32_t x = l[i] < 0 ? -l[i] : l[i], y = r[i] < 0 ? -r[i] : r[i];
        if(x > peak) peak = x;
        if(y > peak) peak = y;
    }
    g1 = g0 + ((65536 - g0 + 127) >> 7);   /* the release */
    if(peak > LIM_CEIL) {
        const int32_t gt = (int32_t)(((uint32_t)LIM_CEIL << 16) / (uint32_t)peak);   /* (peak gt <= the ceiling << 16) */
        if(gt < g1) g1 = gt;
    }
    if(g0 == 65536 && g1 == 65536) return;   /* (no reduction: untouched) */
    for(i = 0; i < CTL; i++) {
        const int32_t g = g1 >= g0 ? g0 + (((g1 - g0) * (int32_t)(i + 1)) >> 5)   /* (CTL 32: coming back, over the block) */
                        : i < 8 ? g0 + (((g1 - g0) * (int32_t)(i + 1)) >> 3) : g1;   /* (going down: over 8 samples) */
        const int32_t yl = k_mulsh(l[i], g, 16), yr = k_mulsh(r[i], g, 16);
        l[i] = yl > LIM_CEIL ? LIM_CEIL : yl < -LIM_CEIL ? -LIM_CEIL : yl;
        r[i] = yr > LIM_CEIL ? LIM_CEIL : yr < -LIM_CEIL ? -LIM_CEIL : yr;
    }
    a->lim_red = (uint32_t)(65536 - g1);
}
_Static_assert(CTL == 32, "limit_block's glide: >> 5");

static void mix_block(KAudio *a)
{
    int32_t sl[CTL], sr[CTL], b[CTL], bus[2][CTL];
    uint32_t i;
    int t, k;
    const int bus_on[2] = {k_fx_bus_active(0), k_fx_bus_active(1)};
    for(i = 0; i < CTL; i++) sl[i] = sr[i] = bus[0][i] = bus[1][i] = 0;
    /* releases: sequenced notes at their time */
    for(t = 0; t < K_TRACKS; t++) {
        track_t *tr = &trk[t];
        for(k = 0; k < NVOICE; k++) {
            voice_t *v = &tr->v[k];
            if(v->active && v->gate && a->frame >= a->rel_at[t][k]) { v->gate = 0; v->stage = 3; mo_arp_off(a, t, (int)k); }
        }
    }
    for(t = 0; t < K_TRACKS; t++) {
        uint32_t nr = 0, any = 0;
        int32_t dl[CTL], dr[CTL];
        for(k = 0; k < NVOICE; k++) any |= trk[t].v[k].active;
        /* a synth track renders (and switches engines) always; a drum track only the synth voices left from before it was one */
        if(any || !is_drum(a->sound[t])) nr = track_block(a, t, b);
        else {   /* a drum track: its LFO runs on (a hit takes its value, drum_on) */
            for(i = 0; i < CTL; i++) b[i] = 0;
            trk[t].p[P_LRATE] = a->tp[t][P_LRATE]; trk[t].p[P_LWAVE] = a->tp[t][P_LWAVE]; trk[t].p[P_LFADE] = a->tp[t][P_LFADE];
            lfo_tick(&trk[t]);
        }
        {   /* its drum lanes (also left from before a switch to a synth). With no insert on the track each lane is panned on its
             * own (PAN); an insert takes the track as one mono signal, so then the lanes are summed and only the track's pan holds */
            const int split = !k_fx_track_active((unsigned)t);
            for(i = 0; i < CTL; i++) dl[i] = dr[i] = 0;
            for(k = 0; k < K_DRUM_POOL; k++) {
                KDrumVoice *v = &a->drum[k];
                if(!v->active || v->track != t) continue;
                if(!split || (v->pan_l == 4096 && v->pan_r == 4096)) { nr += (uint32_t)drum_block(v, b); continue; }
                {
                    int32_t one[CTL];
                    const int32_t pl = v->pan_l, pr = v->pan_r;
                    for(i = 0; i < CTL; i++) one[i] = 0;
                    nr += (uint32_t)drum_block(v, one);
                    for(i = 0; i < CTL; i++) { dl[i] += (one[i] * pl) >> 12; dr[i] += (one[i] * pr) >> 12; }
                }
            }
            nr += (uint32_t)drum_tails(a, t, b);   /* (a stolen hit's last bit falling away, on its own track) */
        }
        a->voices[t] = (uint8_t)(nr > 255 ? 255 : nr);
        k_fx_process_track((unsigned)t, b, CTL); /* per-track inserts, before fader/pan */
        if(t == scope_t) scope_take(b, CTL);
        if(!nr) {
            for(i = 0; i < CTL; i++) if(b[i]) { nr = 1; break; } /* FX tails may outlive notes */
        }
        if(!nr) continue;
        {
            int32_t gl = a->gain_l[t], gr = a->gain_r[t], bl, br;
            if(k_fx_track_balance((unsigned)t, &bl, &br)) { gl = (gl * bl) >> 12; gr = (gr * br) >> 12; }   /* GAIN's PAN (after the inserts, before the track's own) */
            {   int32_t pk = a->pk_trk[t];   /* (the track's peak after its fader, for the mixer's meter: two compares a sample) */
                for(i = 0; i < CTL; i++) {
                    int32_t x = b[i] >> 2;                                      /* pre-shift: loud chords */
                    const int32_t yl = ((x + (dl[i] >> 2)) * gl) >> 10, yr = ((x + (dr[i] >> 2)) * gr) >> 10;   /* (dl, dr: the panned lanes, 0 but on a drum track) */
                    sl[i] += yl; sr[i] += yr;
                    if(yl > pk) pk = yl; else if(-yl > pk) pk = -yl;
                    if(yr > pk) pk = yr; else if(-yr > pk) pk = -yr;
                }
                a->pk_trk[t] = (uint16_t)(pk > 32767 ? 32767 : pk);
            }
            for(k = 0; k < 2; k++) if(bus_on[k] && a->send_q12[t][k]) {   /* the sends: after the fader, before the pan */
                const int32_t gs = k_mulsh(a->gain_m[t], a->send_q12[t][k], 12);
                for(i = 0; i < CTL; i++) bus[k][i] += k_mulsh(b[i] + ((dl[i] + dr[i]) >> 1), gs, 12);
            }
        }
    }
    for(k = 0; k < 2; k++) if(bus_on[k]) {   /* the buses (bus 1 a reverb, bus 2 a delay: kreisel_fx.c): the effect, its TONE (a
                                              * one-pole low-pass), its RTN into both sides */
        int32_t lp = a->bus_lp[k];
        const int32_t kt = a->bus_tone_q15[k], rt = a->bus_ret_q12[k];
        k_fx_process_bus((unsigned)k, bus[k], CTL);
        for(i = 0; i < CTL; i++) {
            int32_t y;
            lp += k_mulsh(bus[k][i] - lp, kt, 15);
            y = k_mulsh(lp, rt, 12);
            sl[i] += y; sr[i] += y;
        }
        a->bus_lp[k] = lp;
    }
    click_mix(a, sl, sr);
    {
        const int32_t mg = a->master_out ? 4096 : a->master_q12;   /* (MASTER outside: the platform's, k_audio_master_outside) */
        for(i = 0; i < CTL; i++) {
            int32_t l = k_mulsh(sl[i], mg, 12), r = k_mulsh(sr[i], mg, 12);   /* (64 bits: four loud tracks took 32 past 2^31) */
            l = l > (1 << 22) ? (1 << 22) : l < -(1 << 22) ? -(1 << 22) : l;   /* (a bound for the DC sum and the limiter, which */
            r = r > (1 << 22) ? (1 << 22) : r < -(1 << 22) ? -(1 << 22) : r;   /*  takes it from there) */
            sl[i] = dc_block(l, &a->dc[0]);
            sr[i] = dc_block(r, &a->dc[1]);
        }
        limit_block(a, sl, sr);
        for(i = 0; i < CTL; i++) {
            a->bl[i] = (int16_t)clamp(sl[i], -32767, 32767);
            a->br[i] = (int16_t)clamp(sr[i], -32767, 32767);
            if(a->bl[i] > (int)a->pk_out[0]) a->pk_out[0] = (uint16_t)a->bl[i]; else if(-a->bl[i] > (int)a->pk_out[0]) a->pk_out[0] = (uint16_t)-a->bl[i];
            if(a->br[i] > (int)a->pk_out[1]) a->pk_out[1] = (uint16_t)a->br[i]; else if(-a->br[i] > (int)a->pk_out[1]) a->pk_out[1] = (uint16_t)-a->br[i];
        }
    }
}

void k_audio_master_outside(KAudio *a, int on) { a->master_out = (uint8_t)(on != 0); }
int k_audio_master_q12(const KAudio *a) { return a->master_q12; }

/* a song's bar begins: the next scene (its first bar: the one it starts on); past the last scene that sounds, the end (nothing
 * more is played; k_audio_song_follow stops the transport) */
static void song_bar(KAudio *a)
{
    int t;
    if(!a->song_on || a->song_end) return;
    if(a->song_first) a->song_first = 0;
    else if(++a->scene > a->scene_last) { a->song_end = 1; a->scene = a->scene_last; return; }
    for(t = 0; t < K_TRACKS; ++t) a->play_pat[t] = a->song[a->scene][t] > 4 ? 4 : a->song[a->scene][t];
}
/* MIDI out of the sequencer: the render's ring, read by the main loop */
const uint8_t k_audio_gm_drum[K_LANES] = {36, 38, 42, 46, 39, 45, 50, 37};
static void mo_put(KAudio *a, int st, int d1, int d2)
{
    const uint8_t w = (uint8_t)((a->mo_w + 1) & 31);
    if(w == a->mo_r) return;   /* (full: dropped) */
    a->mo_q[a->mo_w] = (uint32_t)st | (uint32_t)d1 << 8 | (uint32_t)d2 << 16;
    a->mo_w = w;
}
static void mo_note(KAudio *a, int t, int note, int vel, int steps)   /* on now, off `steps` steps on */
{
    int i;
    if(!a->mo_on || note < 0 || note > 127) return;
    for(i = 0; i < 16; i++) if(a->mo_hold[i].left && a->mo_hold[i].t == t && a->mo_hold[i].note == note) {   /* (sounding: off first) */
        mo_put(a, 0x80 | t, note, 0); a->mo_hold[i].left = 0;
    }
    for(i = 0; i < 16 && a->mo_hold[i].left; i++) {}
    if(i == 16) return;   /* (no room for its off: not sent) */
    mo_put(a, 0x90 | t, note, vel > 127 ? 127 : vel < 1 ? 1 : vel);
    a->mo_hold[i].t = (uint8_t)t; a->mo_hold[i].note = (uint8_t)note; a->mo_hold[i].left = (uint8_t)(steps < 1 ? 1 : steps > 255 ? 255 : steps);
}
static void mo_arp_off(KAudio *a, int t, int k)   /* the arp's note of voice k, off (if one was sent) */
{
    if(a->mo_arp[t][k]) { mo_put(a, 0x80 | t, a->mo_arp[t][k] - 1, 0); a->mo_arp[t][k] = 0; }
}
static void mo_arp_on(KAudio *a, int t, int k, int note, int vel)   /* the arp played voice k: its note out */
{
    if(!a->mo_on || k < 0 || k >= (int)NVOICE || note < 0 || note > 127) return;
    mo_arp_off(a, t, k);
    mo_put(a, 0x90 | t, note, vel > 127 ? 127 : vel < 1 ? 1 : vel);
    a->mo_arp[t][k] = (uint8_t)(note + 1);
}
static void mo_steps(KAudio *a, int all)   /* a step passes (all: the transport stopped): the notes whose time is up, off */
{
    int i;
    if(all) { int t; for(t = 0; t < K_TRACKS; t++) for(i = 0; i < (int)NVOICE; i++) mo_arp_off(a, t, i); }
    for(i = 0; i < 16; i++) if(a->mo_hold[i].left && (all || !--a->mo_hold[i].left)) {
        mo_put(a, 0x80 | a->mo_hold[i].t, a->mo_hold[i].note, 0); a->mo_hold[i].left = 0;
    }
}
int k_audio_midi_pop(KAudio *a, uint8_t m[3])
{
    uint32_t v;
    if(a->mo_r == a->mo_w) return 0;
    v = a->mo_q[a->mo_r];
    a->mo_r = (uint8_t)((a->mo_r + 1) & 31);
    m[0] = (uint8_t)v; m[1] = (uint8_t)(v >> 8); m[2] = (uint8_t)(v >> 16);
    return m[0] >= 0xF8 ? 1 : 3;   /* (the clock, Start, Stop: a byte) */
}
/* the clock out: Start as the transport starts on its own tempo, a clock every 1/24 quarter from the first step (not swung: a
 * receiver swings its own), Stop as it stops, or as an external clock takes over */
static int64_t clock_q16(const KAudio *a) { return (int64_t)(k_umul(a->rate * 15u, 65536u) / (uint32_t)(a->bpm * 6)); }
static void mo_transport(KAudio *a)
{
    const int run = a->mo_on && a->playing && !a->ext && !a->song_end && !a->count_left;
    if(run == a->mo_run) return;
    a->mo_run = (uint8_t)run;
    mo_put(a, run ? 0xFA : 0xFC, 0, 0);
    a->mo_clk_q16 = 0;   /* (the first clock with the first step) */
}
static void mo_clock(KAudio *a)   /* the clocks due by now */
{
    while(a->mo_run && a->mo_clk_q16 <= 0) { mo_put(a, 0xF8, 0, 0); a->mo_clk_q16 += clock_q16(a); }
}
static void fire_step(KAudio *a, int step)
{
    if(a->mo_on) mo_steps(a, 0);
    int t, lane;
    const int64_t frames = duration_q16(a, step);
    if(step == 0) song_bar(a);
    if(a->song_end) return;
    if(a->metro && !(step & 3)) click_on(a, step == 0);   /* MET: the beats, the bar's first accented */
    for(t = 0; t < K_TRACKS; ++t) {
        const int pat = a->play_pat[t];
        if(pat > 3) continue;   /* (silent in the song's scene) */
        {   /* the step's chance (2 bits: 0 always, 1 75 %, 2 50 %, 3 25 %): its notes or hits sound together, or none of them. The dice
             * are thrown only for a step that has one (a pattern without chances draws nothing: the same sequence as before) */
            const uint32_t c = a->chance[t][pat] >> (2 * step) & 3u;
            if(c) {
                uint32_t x = a->rng;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                a->rng = x;
                if((x >> 30) < c) continue;   /* (two bits, 0..3: under c, c in 4 times, silent) */
            }
        }
        if(is_drum(a->sound[t])) {
            for(lane = 0; lane < K_LANES; ++lane)
                if(a->lane_on[t][pat][lane] & (uint16_t)(1u << step)) {
                    drum_on(a, t, lane);
                    mo_note(a, t, k_audio_gm_drum[lane], 100, 1);
                }
        } else if(is_synth_drum(a->sound[t])) {                 /* a synth that is a drum: each lane's note, a step long */
            for(lane = 0; lane < K_LANES; ++lane)
                if(a->lane_on[t][pat][lane] & (uint16_t)(1u << step)) {
                    start_tone(a, t, k_plugin_lane_note(a->sound[t], lane), 100, 1, frames, lane);
                    mo_note(a, t, k_plugin_lane_note(a->sound[t], lane), 100, 1);
                }
        } else if(a->steps[t][pat] & (uint16_t)(1u << step)) {   /* every note starting at the step (a chord: all of them) */
            int k;
            for(k = 0; k < K_ROLL_NOTES && a->roll[t][k].len; ++k) {
                const KNote *n = &a->roll[t][k];
                if(n->at != (uint8_t)(pat << 4 | step)) continue;
                if (!a->arp_mode[t]) mo_note(a, t, n->note, n->vel, n->len);   /* (an arp's track: the notes it plays go out instead) */
                if (a->arp_mode[t]) arp_seq_on(a, t, n->note, n->vel, n->len, (uint64_t)frames);
                else start_tone(a, t, n->note, n->vel, n->len, frames, -1);
            }
        }
    }
}

/* Q15 stereo into out (interleaved int32), each sample << shift */
static void fill(KAudio *a, int32_t *out, size_t frames, unsigned shift)
{
    bind(a);
    if(!a->playing || a->song_end) mo_steps(a, 1);   /* (stopped: what was sent, off) */
    mo_transport(a);
    if(g_vcap < NVOICE) {   /* the voices over a lowered cap let go now, not at the next note (a held chord kept the load up) */
        uint32_t t, i;
        for(t = 0; t < NPART; t++)
            for(i = g_vcap; i < NVOICE; i++) if(trk[t].v[i].active && trk[t].v[i].gate) { trk[t].v[i].gate = 0; trk[t].v[i].stage = 3; }
    }
    while(frames) {
        size_t run = frames;
        if(a->playing && a->ext) {
            if(a->clk_acc >= 6) {            /* external clock: a step every 6 clocks (one a render, the rest catch up) */
                const int step = a->next_step;
                a->clk_acc -= 6;
                fire_step(a, step);
                a->current_step = step;
                a->next_step = (step + 1) & 15;
            }
        } else if(a->playing) {
            mo_transport(a);   /* (Start as a bar counted in ends) */
            mo_clock(a);   /* (before the step's notes: a receiver counts the clock first) */
            if(a->frames_to_step <= 0 && a->count_left) {   /* a bar counted in: its beats clicked, no step played */
                const int k = K_STEPS - a->count_left;
                if(!(k & 3)) click_on(a, k == 0);
                a->count_left--;
                a->frames_to_step += duration_q16(a, k);
            } else if(a->frames_to_step <= 0) {
                const int step = a->next_step;
                fire_step(a, step);
                a->current_step = step;
                a->next_step = (step + 1) & 15;
                a->frames_to_step += duration_q16(a, step);
            }
            if(a->frames_to_step > 0) {
                const uint64_t need = ((uint64_t)a->frames_to_step + 65535u) >> 16;   /* frames until it is <= 0 */
                if(need < run) run = (size_t)need;
            } else {
                run = 1;
            }
        }
        arp_process(a);   /* run new notes after any sequencer step at this frame */
        run = arp_limit_run(a, run); /* respect the ARP's next sample-clock edge */
        while(run) {
            size_t n;
            if(a->bpos >= K_AUDIO_BLOCK) { mix_block(a); a->bpos = 0; }
            n = K_AUDIO_BLOCK - a->bpos;
            if(n > run) n = run;
            {
                size_t i;
                for(i = 0; i < n; i++) {
                    out[2u * i] = (int32_t)a->bl[a->bpos + i] * (1 << shift);
                    out[2u * i + 1u] = (int32_t)a->br[a->bpos + i] * (1 << shift);
                }
            }
            out += 2u * n;
            a->bpos += (uint32_t)n;
            if(a->mo_run) a->mo_clk_q16 -= (int64_t)n << 16;
            a->frame += n;
            if(a->playing && !a->ext) a->frames_to_step -= (int64_t)n << 16;
            run -= n;
            frames -= n;
        }
    }
}

void k_audio_render_s32(KAudio *a, int32_t *out, size_t frames)
{
    if(!out || !a->rate) return;
    fill(a, out, frames, 7);
}

void k_audio_render(KAudio *a, float *out, size_t frames)
{
    int32_t tmp[2 * 128];
    if(!out || !a->rate) return;
    while(frames) {
        const size_t n = frames > 128 ? 128 : frames;
        size_t i;
        fill(a, tmp, n, 0);
        for(i = 0; i < 2u * n; i++) out[i] = (float)tmp[i] * (1.f / 32768.f);
        out += 2u * n;
        frames -= n;
    }
}

/* the meters' peaks since the last call, to the app (the platform's main loop, each frame), then cleared. (Another thread renders:
 * a block between the read and the clear is lost to the meter, no more.) out: also the header's meter (a platform with its own, the
 * FM-1's, passes 0) */
void k_audio_take_peaks(KAudio *a, KApp *p, uint32_t ms, int out)
{
    uint16_t pk[K_TRACKS];
    int t;
    for(t = 0; t < K_TRACKS; t++) { pk[t] = a->pk_trk[t]; a->pk_trk[t] = 0; }
    k_set_track_peaks(p, pk, ms);
    if(out) { k_set_out_peak(p, a->pk_out[0], a->pk_out[1], ms); a->pk_out[0] = a->pk_out[1] = 0; }
}
