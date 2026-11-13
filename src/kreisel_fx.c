/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The Pendel effects behind it (third_party/pendel-dsp) are the author's own, relicensed MPL-2.0 for Kreisel. */
#include "kreisel_fx.h"
#include "kreisel_audio.h"
#include "../third_party/pendel-dsp/pd_dsp.h"
#include "../third_party/pendel-dsp/pd_modules.h"
#include "../third_party/pendel-dsp/pd_effects.h"
#include <string.h>

#define FX_TRACKS K_TRACKS
#define FX_ROWS (FX_TRACKS + 1)   /* the tracks' inserts, then the buses' row: slot 0 bus 1 (a reverb), slot 1 bus 2 (a delay) */
#define FX_BUS_ROW FX_TRACKS
#define FX_BUS1_TYPE 4            /* REV */
#define FX_BUS2_TYPE 3            /* DLY */
#define FX_GAIN_TYPE 5            /* GAIN (its PAN: k_fx_track_balance) */
#define FX_SLOTS K_FX_SLOTS
#define FX_BLOCK 32u

/* Index 0 is NONE. Keep positions 1..4 byte-for-byte compatible with songs
 * written before the FX plugin migration: DIST, CHO, DLY, REV. */
static const PendelDSP *const source[K_FX_BUILTINS] = {
    &PD_FX_DRIVE, &PD_FX_FLANGER, &PD_FX_DELAY, &PD_FX_ROOM,
    &PD_FX_GAIN, &PD_FX_FILTER, &PD_FX_FUZZ, &PD_FX_WAH,
    &PD_FX_CRUSH, &PD_FX_PHASER, &PD_FX_TREMOLO, &PD_FX_WOBBLE,
    &PD_FX_NOISE, &PD_FX_PITCH, &PD_FX_AMP
};
static const char *const names[K_FX_BUILTINS] = {
    "DIST", "CHO", "DLY", "REV", "GAIN", "FILTER", "FUZZ",
    "WAH", "CRUSH", "PHASER", "TREMOLO", "WOBBLE", "NOISE", "PITCH", "AMP"
};
static const uint32_t ids[K_FX_BUILTINS] = {
    K_UID('D','R','V','E'), K_UID('F','L','N','G'), K_UID('D','E','L','Y'),
    K_UID('R','O','O','M'), K_UID('G','A','I','N'), K_UID('F','I','L','T'),
    K_UID('F','U','Z','Z'), K_UID('W','A','H','0'), K_UID('C','R','S','H'),
    K_UID('P','H','S','R'), K_UID('T','R','M','L'), K_UID('W','O','B','L'),
    K_UID('N','O','I','S'), K_UID('P','I','T','C'), K_UID('A','M','P','0')
};
/* Provisional microsecond estimates per 32-frame block on FM-1.
 * Measure worst-case times on hardware before publishing performance claims. */
static const uint16_t cost_us[K_FX_BUILTINS] = {   /* (2026-11-12, the third values: CHO's depth, GAIN's clip, FUZZ's and
                                                     * CRUSH's and NOISE's tilt, WAH's LFO a little more) */
    12, 19, 8, 24, 6, 12, 16, 24, 8, 22, 7, 18, 10, 24, 64
};
/* Every effect but FILTER and AMP had two values; each has a third since 2026-11-12, which at its middle (the host's 50, the
 * module's PD_PARAM_MID) sounds as before it was there (tests/fx_golden_test.c). A switch is on above the middle: off at 50 */
static const char *const short_labels[K_FX_BUILTINS][8] = {
    {"DRIV","TONE","LVL"}, {"RATE","FDBK","DPTH"}, {"TIME","FDBK","SYNC"},
    {"SIZE","DCAY","TONE"}, {"GAIN","PAN","SAT"}, {"CUT","RES","MODE"},
    {"FUZZ","BIAS","TONE"}, {"SENS","RES","MODE"}, {"BITS","DWN","TONE"},
    {"RATE","DPTH","FDBK"}, {"RATE","DPTH","WAVE"}, {"RATE","DPTH","SYNC"},
    {"HISS","CRKL","TONE"}, {"SEMI","FDBK","FINE"},
    {"MODE","GAIN","BASS","MID","TREB","LVL","CAB"}
};
static const char *const WAH_MODES[] = {"Env", "LFO", 0}, *const TRM_WAVES[] = {"Sine", "Sqr", 0};

static KEngine descriptions[K_FX_BUILTINS];
static KEffectDSP backends[K_FX_BUILTINS];
static KPlugin builtin[K_FX_BUILTINS];
static int initialized;
static void init_catalog(void)
{
    if (initialized) return;
    for (unsigned i = 0; i < K_FX_BUILTINS; i++) {
        const PendelDSP *src = source[i];
        KEngine *d = &descriptions[i];
        KEffectDSP *e = &backends[i];
        e->name = src->name; e->state_size = src->state_size;
        e->mem_size = src->mem_size; e->num_params = src->num_params;
        e->param_names = src->param_names; e->init = src->init;
        e->process = src->process; e->reset = src->reset; e->set_param = src->set_param;
        d->name = names[i]; d->uid = ids[i]; d->nparams = (uint8_t)src->num_params;
        d->page2 = d->nparams;
        for (unsigned p = 0; p < src->num_params && p < K_ENGINE_PARAMS; p++) {
            d->edit[p] = (KParamDesc){short_labels[i][p] ? short_labels[i][p] : "VAL",
                KF_PCT, 0, 100, 50, 0, 0};
        }
        if (src == &PD_FX_PITCH) d->edit[PD_PIT_SEMI].fmt = KF_FXSEMI;   /* -12 .. +12 st, 0 in the middle */
        if (src == &PD_FX_PITCH) { d->edit[PD_PIT_FINE].fmt = KF_CENTER; d->edit[PD_PIT_FINE].unit = "ct"; }   /* -50 .. +50 cents */
        if (src == &PD_FX_GAIN) { d->edit[PD_GAIN_PAN].fmt = KF_CENTER; d->edit[PD_GAIN_SAT].fmt = KF_ONOFF; }
        if (src == &PD_FX_ROOM) d->edit[PD_ROOM_TONE].fmt = KF_CENTER;   /* the tilts and the room's damping: - 0 + */
        if (src == &PD_FX_FUZZ) d->edit[PD_FUZZ_TONE].fmt = KF_CENTER;
        if (src == &PD_FX_CRUSH) d->edit[PD_CRUSH_TONE].fmt = KF_CENTER;
        if (src == &PD_FX_NOISE) d->edit[PD_NOISE_TONE].fmt = KF_CENTER;
        if (src == &PD_FX_DELAY) d->edit[PD_DLY_SYNC].fmt = KF_ONOFF;    /* (SYNC on: TIME a division, KF_FXDIV: kreisel_param.c) */
        if (src == &PD_FX_WOBBLE) d->edit[PD_WOB_SYNC].fmt = KF_ONOFF;
        if (src == &PD_FX_WAH) { d->edit[PD_WAH_MODE].fmt = KF_ONOFF; d->edit[PD_WAH_MODE].names = WAH_MODES; }
        if (src == &PD_FX_TREMOLO) { d->edit[PD_TRM_WAVE].fmt = KF_ONOFF; d->edit[PD_TRM_WAVE].names = TRM_WAVES; }
        if (src == &PD_FX_AMP) { d->edit[PD_AMP_BASS].fmt = d->edit[PD_AMP_MID].fmt = d->edit[PD_AMP_TREBLE].fmt = KF_CENTER; }   /* the tone stack: cut - 0 + boost */
        for (unsigned m = 0; m < 4; m++) d->macro[m] = (uint8_t)(m < d->nparams ? m : 0);
        builtin[i] = (KPlugin){sizeof(KPlugin), K_PLUGIN_ABI, 1, K_PLUG_EFFECT,
            d, e, K_AUDIO_FS, (uint32_t)src->state_size +
            (uint32_t)(src->mem_size ? src->mem_size(K_AUDIO_FS) : 0), cost_us[i], 0, 0, 0};
    }
    initialized = 1;
}
const KPlugin *k_fx_builtin_at(unsigned index)
{
    init_catalog();
    return index < K_FX_BUILTINS ? &builtin[index] : 0;
}

typedef struct {
    const KEffectDSP *dsp;
    void *state;
    int type;
    int v[2 + K_FX_EXTRA_PER_SLOT];
    int mix, mix_q15;
    uint8_t bpm;     /* the tempo it was given (a built-in's PD_HOST_TEMPO), 0 none yet */
} FxSlot;
static FxSlot slots[FX_ROWS][FX_SLOTS];
static uint16_t balance[FX_TRACKS][2];   /* a track's left and right after its inserts (GAIN's PAN), Q12; 0 0: centred */
static uint8_t pool[K_FX_POOL_BYTES];
static size_t used;
static const void *fx_owner;
static void *alloc_state(size_t len)
{
    size_t pos = (used + 7u) & ~(size_t)7;
    if (len > sizeof(pool) || pos > sizeof(pool) - len) return 0;
    used = pos + len;
    memset(pool + pos, 0, len);
    return pool + pos;
}
#include "kreisel_mul.h"
#define FX_HEADROOM 2   /* the inserts run this many bits down (k_fx_process_track) */
void k_fx_forget(void) { fx_owner = 0; used = 0; memset(slots, 0, sizeof(slots)); memset(balance, 0, sizeof(balance)); }
void k_fx_bind(const void *owner)
{
    if (fx_owner == owner) return;
    k_fx_forget(); fx_owner = owner;
}
/* Place the effects asked for: all of them (full), or only the slots whose type changed (the rest kept as they run). The device's
 * budget counts every slot that runs. 0: a changed slot found no room in the pool (the caller packs them all again) */
static int fx_place(int desired[FX_ROWS][FX_SLOTS], int full)
{
    const KDeviceCaps *caps = k_plugin_caps();
    uint32_t budget_used = 0, cost_used[FX_ROWS] = {0};
    unsigned t, s;
    for (t = 0; t < FX_ROWS && !full; t++)
        for (s = 0; s < FX_SLOTS; s++)
            if (slots[t][s].dsp && slots[t][s].type == desired[t][s]) {
                const KPlugin *p = k_effect_at(slots[t][s].type);
                if (p) { budget_used += p->ram; cost_used[t] += p->cost; }
            }
    for (t = 0; t < FX_ROWS; t++)
        for (s = 0; s < FX_SLOTS; s++) {
            FxSlot *node = &slots[t][s];
            const KPlugin *p = k_effect_at(desired[t][s]);
            const KEffectDSP *d;
            size_t extra;
            void *state, *mem;
            if (!full && node->type == desired[t][s] && (node->dsp || !desired[t][s])) continue;   /* (one left out for want of room
                                                                                 * is tried again: failing, they are all packed anew) */
            memset(node, 0, sizeof *node);   /* (its old state stays in the pool until the next packing) */
            node->type = desired[t][s];
            if (!p || !p->dsp) continue;
            if (caps && (k_plugin_fits(p, caps, budget_used) ||
                cost_used[t] > caps->cost || p->cost > caps->cost - cost_used[t]))
                continue; /* the device cannot safely run this instance */
            d = (const KEffectDSP *)p->dsp;
            extra = d->mem_size ? d->mem_size(K_AUDIO_FS) : 0;
            state = alloc_state(d->state_size); mem = extra ? alloc_state(extra) : 0;
            if (!state || (extra && !mem)) {
                if (!full) return 0;
                continue; /* safely bypass when budget is exhausted */
            }
            node->dsp = d; node->state = state;
            d->init(state, mem, K_AUDIO_FS);
            for (unsigned k = 0; k < 2 + K_FX_EXTRA_PER_SLOT; k++) node->v[k] = -1;
            budget_used += p->ram;
            cost_used[t] += p->cost;
        }
    return 1;
}
/* the song's tempo to a built-in effect (one that keeps time reads it; the rest ignore it). Not an outside plugin's: its
 * set_param need not take an id past its parameters */
static void fx_tempo(FxSlot *node, int bpm)
{
    if (!node->dsp || node->type < 1 || node->type > (int)K_FX_BUILTINS || node->bpm == bpm) return;
    node->dsp->set_param(node->state, PD_HOST_TEMPO, bpm);
    node->bpm = (uint8_t)bpm;
}
void k_fx_sync(const KApp *app)
{
    int dirty = 0, bpm;
    int desired[FX_ROWS][FX_SLOTS];
    if (!app || !fx_owner) return;
    for (unsigned t = 0; t < FX_TRACKS; t++)
        for (unsigned s = 0; s < FX_SLOTS; s++) {
            int n = app->params[t][K_PAGE_FX][s * 4u];
            desired[t][s] = (n > 0 && n <= k_effect_count()) ? n : 0;
            if (desired[t][s] != slots[t][s].type) dirty = 1;
        }
    for (unsigned s = 0; s < FX_SLOTS; s++) {   /* a bus runs while a track sends to it (its room and time only then) */
        int want = 0;
        for (unsigned t = 0; t < FX_TRACKS && s < 2; t++) want |= app->params[t][K_PAGE_MIXER][2 + s] > 0;
        desired[FX_BUS_ROW][s] = want ? (s ? FX_BUS2_TYPE : FX_BUS1_TYPE) : 0;
        if (desired[FX_BUS_ROW][s] != slots[FX_BUS_ROW][s].type) dirty = 1;
    }
    if (dirty && !fx_place(desired, 0)) {   /* the slots that changed, from the pool's free end: the others keep their state (a
                                             * delay's tail on another track went on). Out of room: every slot anew, packed */
        used = 0;
        memset(slots, 0, sizeof(slots));
        (void)fx_place(desired, 1);
    }
    bpm = app->bpm < 20 ? 20 : app->bpm > 255 ? 255 : app->bpm;
    for (unsigned t = 0; t < FX_TRACKS; t++) {
        int32_t bl = 4096, br = 4096;
        for (unsigned s = 0; s < FX_SLOTS; s++) {
            FxSlot *node = &slots[t][s];
            node->mix = app->params[t][K_PAGE_FX][s * 4u + 3u];
            if (node->mix < 0) node->mix = 0;
            if (node->mix > 100) node->mix = 100;
            node->mix_q15 = (node->mix * 32768 + 50) / 100; /* control thread only */
            if (!node->dsp) continue;
            for (unsigned k = 0; k < 2 + K_FX_EXTRA_PER_SLOT && k < node->dsp->num_params; k++) {
                int v = app->params[t][K_PAGE_FX][k_fx_store_param((int)s, (int)k)];
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                if (node->v[k] != v) {
                    node->dsp->set_param(node->state, k, (v * 32767 + 50) / 100);
                    node->v[k] = v;
                }
            }
            fx_tempo(node, bpm);
            if (node->type == FX_GAIN_TYPE && node->mix) {   /* GAIN's PAN: a balance after the inserts (the chain is mono),
                                                        * as much as its MIX lets it; two of them both */
                const int off = (node->v[PD_GAIN_PAN] - 50) * node->mix / 100;   /* -50 .. 50 */
                if (off > 0) bl = bl * (50 - off) / 50;
                if (off < 0) br = br * (50 + off) / 50;
            }
        }
        balance[t][0] = (uint16_t)(bl == 4096 && br == 4096 ? 0 : bl);
        balance[t][1] = (uint16_t)(bl == 4096 && br == 4096 ? 0 : br);
    }
    for (unsigned s = 0; s < 2 && s < FX_SLOTS; s++) {   /* the buses: wet only, their first two values from BUS 1 / BUS 2 (the
                                                  * reverb's SIZE DCY, the delay's TIME FB; TONE and RTN are the mixer's: audio) */
        FxSlot *node = &slots[FX_BUS_ROW][s];
        node->mix = 100; node->mix_q15 = 32768;
        if (!node->dsp) continue;
        for (unsigned k = 0; k < 2 && k < node->dsp->num_params; k++) {
            int v = app->params[0][s ? K_PAGE_BUS2 : K_PAGE_BUS1][k];
            v = v < 0 ? 0 : v > 100 ? 100 : v;
            if (node->v[k] != v) { node->dsp->set_param(node->state, k, (v * 32767 + 50) / 100); node->v[k] = v; }
        }
        fx_tempo(node, bpm);   /* (its third value stays at its start: the delay free, the room's tone as it was) */
    }
}
int k_fx_track_balance(unsigned t, int32_t *l, int32_t *r)
{
    if (t >= FX_TRACKS || !fx_owner || (!balance[t][0] && !balance[t][1])) return 0;
    *l = balance[t][0]; *r = balance[t][1];
    return 1;
}
int k_fx_bus_active(unsigned b) { return b < 2 && b < FX_SLOTS && fx_owner && slots[FX_BUS_ROW][b].dsp; }
void k_fx_process_bus(unsigned b, int32_t *samples, unsigned frames)   /* a bus's sum in, its effect out (wet), in place */
{
    FxSlot *node;
    unsigned i;
    if (!k_fx_bus_active(b) || !samples || frames > FX_BLOCK) return;
    node = &slots[FX_BUS_ROW][b];
    for (i = 0; i < frames; i++) samples[i] >>= FX_HEADROOM;
    node->dsp->process(node->state, samples, samples, frames);
    for (i = 0; i < frames; i++) {
        const int32_t w = samples[i] < -65535 ? -65535 : samples[i] > 65535 ? 65535 : samples[i];
        samples[i] = w * (1 << FX_HEADROOM);
    }
}
int k_fx_track_active(unsigned t)
{
    unsigned s;
    if (t >= FX_TRACKS || !fx_owner) return 0;
    for (s = 0; s < FX_SLOTS; s++) if (slots[t][s].dsp && slots[t][s].mix) return 1;
    return 0;
}
void k_fx_process_track(unsigned t, int32_t *samples, unsigned frames)
{
    unsigned s, i, any = 0;
    if (t >= FX_TRACKS || !samples || frames > FX_BLOCK || !fx_owner) return;
    for (s = 0; s < FX_SLOTS; s++) any |= slots[t][s].dsp && slots[t][s].mix;
    if (!any) return;
    /* The inserts work at +/-65535 (their delay lines 16 bits), the track runs louder (a six-note chord ~200 000): the chain
     * runs 12 dB down and comes back up after, so a unity insert is unity; only what an effect makes past its rail clips.
     * The dry signal is never clamped. */
    for (i = 0; i < frames; i++) samples[i] >>= FX_HEADROOM;
    for (s = 0; s < FX_SLOTS; s++) {
        FxSlot *node = &slots[t][s];
        if (!node->dsp || !node->mix) continue;
        int32_t dry[FX_BLOCK];
        if (node->mix < 100) memcpy(dry, samples, sizeof(int32_t) * frames);
        node->dsp->process(node->state, samples, samples, frames);
        for (i = 0; i < frames; i++) {
            int32_t wet = samples[i] < -65535 ? -65535 : samples[i] > 65535 ? 65535 : samples[i];
            if (node->mix < 100) {
                const int32_t delta = wet - dry[i];   /* (|dry| < 2^19 after the headroom shift) */
                wet = dry[i] + k_mulsh(delta, node->mix_q15, 15);   /* (64 bits: dry is not clamped; out of line on the FM-1) */
            }
            samples[i] = wet;
        }
    }
    for (i = 0; i < frames; i++) samples[i] *= 1 << FX_HEADROOM;
}
