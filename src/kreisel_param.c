/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The values' table (restructure step C1): every value has an id (KParamId), and its entry
 * here says where it lives, its range, its default, how it is shown and where a turn stops. A page's item is only a way to one of
 * them (k_param_ref); the page's code no longer decides a value's range or default.
 *
 * Kept as it was before the table (the screens and the songs must not change), where one value shows differently on two pages:
 * - ENV 1's item 7 (a blank card) turns SHP, as item 6 does; ENV 2's items 5 .. 7 (blank) turn AMP, shown 0..100
 * - PHASE's DCW (ENV 1's items 4 .. 7) and DCA's ATK on ENV 2's page stop at 50 on the way across (K_V_C50), where other engines
 *   have destinations
 * - a drum lane's ATK PDEP PTIM CLIK reset to an envelope's 0 50 100 20 on ENV 1's page, to the engine's defaults on SOUND
 * - an effect's values are kept in 0..100 and reset to 50, whatever its descriptor says (all are 0..100, default or not) */
#include "kreisel.h"
#include "kreisel_level.h"
#include "kreisel_plugin.h"
#include <string.h>

enum { AT_TRACK, AT_SONG, AT_RAW, AT_BPM, AT_SWING, AT_CLK, AT_CHANCE, AT_LANE, AT_CUR, AT_RNOTE, AT_RLEN, AT_RVEL, AT_COLOR, AT_ENGINE };
enum { SRC_OWN, SRC_DESC, SRC_ENGINE, SRC_EFFECT, SRC_FXTYPE };
typedef struct {
    uint8_t at, page, idx;     /* where: params[t][page][idx], lane_p[t][lane][idx], or a field of its own */
    uint8_t src;               /* where the rest comes from: below (OWN), k_page_desc(page)[idx], the engine's, the effect's */
    uint8_t fmt, flags;        /* KF_*, K_PF_* */
    int16_t min, max, def, center;
} Entry;

#define NC K_NO_CENTER
#define PR (K_PF_PRESET | K_PF_DIRTY_RESET)
#define OWN(at, pg, ix, fmt, fl, lo, hi, def, c) {at, pg, ix, SRC_OWN, fmt, fl, lo, hi, def, c}
#define DESC(at, pg, ix, fl) {at, pg, ix, SRC_DESC, 0, fl, 0, 0, 0, NC}
#define ENG(k) [K_P_ENG + (k)] = {AT_ENGINE, K_PAGE_SOUND, k, SRC_ENGINE, 0, K_PF_PRESET | K_PF_DIRTY_SET | K_PF_DIRTY_RESET, 0, 0, 0, NC}
#define ADSR(id, at, pg, ix) [id] = OWN(at, pg, ix, KF_PCT, PR, 0, 100, 0, NC), [id + 1] = OWN(at, pg, ix + 1, KF_PCT, PR, 0, 100, 50, NC), \
                             [id + 2] = OWN(at, pg, ix + 2, KF_PCT, PR, 0, 100, 100, NC), [id + 3] = OWN(at, pg, ix + 3, KF_PCT, PR, 0, 100, 20, NC)
#define FXT(st) [K_P_FX + (st)] = {AT_TRACK, K_PAGE_FX, st, SRC_FXTYPE, KF_FXTYPE, K_PF_PRESET | K_PF_NOSET | K_PF_NORESET, 0, 0, 0, NC}
#define FXM(st) [K_P_FX + (st)] = OWN(AT_TRACK, K_PAGE_FX, st, KF_PCT, K_PF_PRESET | K_PF_NORESET, 0, 100, 50, NC)
#define FXP(st) [K_P_FX + (st)] = {AT_TRACK, K_PAGE_FX, st, SRC_EFFECT, 0, PR | K_PF_COARSE, 0, 100, 50, NC}

static const Entry T[K_P_COUNT] = {
    [K_P_RAW] = OWN(AT_RAW, 0, 0, KF_PCT, K_PF_DIRTY_RESET, 0, 100, 50, NC),
    ENG(0), ENG(1), ENG(2), ENG(3), ENG(4), ENG(5), ENG(6), ENG(7), ENG(8), ENG(9),
    ENG(10), ENG(11), ENG(12), ENG(13), ENG(14), ENG(15), ENG(16), ENG(17), ENG(18), ENG(19),
    ADSR(K_P_ENV1, AT_TRACK, K_PAGE_ENV1, 0),
    [K_P_ENV2 + 0] = OWN(AT_TRACK, K_PAGE_ENV2, 0, KF_PCT, PR, 0, 100, 0, NC),    /* DLY, HOLD none; CRV straight; AMT none */
    [K_P_ENV2 + 1] = OWN(AT_TRACK, K_PAGE_ENV2, 1, KF_PCT, PR, 0, 100, 0, NC),
    [K_P_ENV2 + 2] = OWN(AT_TRACK, K_PAGE_ENV2, 2, KF_PCT, PR, 0, 100, 50, NC),
    [K_P_ENV2 + 3] = OWN(AT_TRACK, K_PAGE_ENV2, 3, KF_PCT, PR, 0, 100, 50, NC),
    ADSR(K_P_PH_DCO, AT_TRACK, K_PAGE_ENV2, 4),
    ADSR(K_P_PH_DCA, AT_TRACK, K_PAGE_ENV2, 0),
    [K_P_DEST + 0] = OWN(AT_TRACK, K_PAGE_ENV_DEST, 0, KF_LINK_FENV, PR, 0, 100, 50, 50),   /* -100 .. +100 %, 50 none */
    [K_P_DEST + 1] = OWN(AT_TRACK, K_PAGE_ENV_DEST, 1, KF_LINK_FENV, PR, 0, 100, 50, 50),
    [K_P_DEST + 2] = OWN(AT_TRACK, K_PAGE_ENV_DEST, 2, KF_LINK_FENV, PR, 0, 100, 50, 50),
    [K_P_DEST + 3] = OWN(AT_TRACK, K_PAGE_ENV_DEST, 3, KF_LINK_FENV, PR, 0, 100, 50, 50),
    ADSR(K_P_KIT_ENV, AT_LANE, 0, K_LANE_ENV),
    [K_P_LFO + 0] = DESC(AT_TRACK, K_PAGE_LFO, 0, K_PF_PRESET), [K_P_LFO + 1] = DESC(AT_TRACK, K_PAGE_LFO, 1, K_PF_PRESET),
    [K_P_LFO + 2] = DESC(AT_TRACK, K_PAGE_LFO, 2, K_PF_PRESET), [K_P_LFO + 3] = DESC(AT_TRACK, K_PAGE_LFO, 3, K_PF_PRESET),
    [K_P_LFO + 4] = DESC(AT_TRACK, K_PAGE_LFO, 4, K_PF_PRESET), [K_P_LFO + 5] = DESC(AT_TRACK, K_PAGE_LFO, 5, K_PF_PRESET),
    [K_P_LFO + 6] = DESC(AT_TRACK, K_PAGE_LFO, 6, K_PF_PRESET), [K_P_LFO + 7] = DESC(AT_TRACK, K_PAGE_LFO, 7, K_PF_PRESET),
    [K_P_LFO + 8] = DESC(AT_TRACK, K_PAGE_LFO, 8, K_PF_PRESET),
    [K_P_ARP + 0] = DESC(AT_TRACK, K_PAGE_ARP, 0, 0), [K_P_ARP + 1] = DESC(AT_TRACK, K_PAGE_ARP, 1, 0),
    [K_P_ARP + 2] = DESC(AT_TRACK, K_PAGE_ARP, 2, 0), [K_P_ARP + 3] = DESC(AT_TRACK, K_PAGE_ARP, 3, 0),
    [K_P_SCL + 0] = DESC(AT_TRACK, K_PAGE_SCL, 0, 0), [K_P_SCL + 1] = DESC(AT_TRACK, K_PAGE_SCL, 1, 0),
    [K_P_SCL + 2] = DESC(AT_TRACK, K_PAGE_SCL, 2, 0), [K_P_SCL + 3] = DESC(AT_TRACK, K_PAGE_SCL, 3, 0),
    [K_P_BPM] = DESC(AT_BPM, K_PAGE_CLOCK, 0, 0), [K_P_SWING] = DESC(AT_SWING, K_PAGE_CLOCK, 1, 0),
    [K_P_CLOCK_SRC] = DESC(AT_CLK, K_PAGE_CLOCK, 2, 0), [K_P_CHANCE] = DESC(AT_CHANCE, K_PAGE_STEP, 3, 0),
    [K_P_ROLL_STEP] = DESC(AT_CUR, K_PAGE_ROLL, 0, K_PF_NOSET | K_PF_NORESET),
    [K_P_ROLL_NOTE] = DESC(AT_RNOTE, K_PAGE_ROLL, 1, 0), [K_P_ROLL_LEN] = DESC(AT_RLEN, K_PAGE_ROLL, 2, 0),
    [K_P_ROLL_VEL] = DESC(AT_RVEL, K_PAGE_ROLL, 3, 0),
    [K_P_LEVEL] = OWN(AT_TRACK, K_PAGE_MIXER, 0, KF_FADER, 0, 0, 100, K_LEVEL_0DB, K_LEVEL_0DB),
    [K_P_PAN] = OWN(AT_TRACK, K_PAGE_MIXER, 1, KF_CENTER, 0, 0, 100, 50, 50),   /* - 0 +, 50 the centre */
    [K_P_SEND1] = OWN(AT_TRACK, K_PAGE_MIXER, 2, KF_PCT, 0, 0, 100, 0, NC),
    [K_P_SEND2] = OWN(AT_TRACK, K_PAGE_MIXER, 3, KF_PCT, 0, 0, 100, 0, NC),
    [K_P_BUS1 + 0] = OWN(AT_SONG, K_PAGE_BUS1, 0, KF_PCT, 0, 0, 100, 50, NC),   /* a reverb: SIZE DCY TONE, RTN */
    [K_P_BUS1 + 1] = OWN(AT_SONG, K_PAGE_BUS1, 1, KF_PCT, 0, 0, 100, 50, NC),
    [K_P_BUS1 + 2] = OWN(AT_SONG, K_PAGE_BUS1, 2, KF_PCT, 0, 0, 100, 50, NC),
    [K_P_BUS1 + 3] = OWN(AT_SONG, K_PAGE_BUS1, 3, KF_FADER, 0, 0, 100, K_LEVEL_0DB, K_LEVEL_0DB),
    [K_P_BUS2 + 0] = OWN(AT_SONG, K_PAGE_BUS2, 0, KF_PCT, 0, 0, 100, 50, NC),   /* a delay: TIME FB TONE, RTN */
    [K_P_BUS2 + 1] = OWN(AT_SONG, K_PAGE_BUS2, 1, KF_PCT, 0, 0, 100, 50, NC),
    [K_P_BUS2 + 2] = OWN(AT_SONG, K_PAGE_BUS2, 2, KF_PCT, 0, 0, 100, 50, NC),
    [K_P_BUS2 + 3] = OWN(AT_SONG, K_PAGE_BUS2, 3, KF_FADER, 0, 0, 100, K_LEVEL_0DB, K_LEVEL_0DB),
    FXT(0), FXP(1), FXP(2), FXM(3), FXT(4), FXP(5), FXP(6), FXM(7),   /* slot 1: TYPE P0 P1 MIX, slot 2 the same */
    FXP(8), FXP(9), FXP(10), FXP(11), FXP(12), FXP(13), FXP(14), FXP(15),   /* slot 1's P2 .. P9 */
    FXP(16), FXP(17), FXP(18), FXP(19), FXP(20), FXP(21), FXP(22), FXP(23),   /* slot 2's */
    [K_P_COLOR] = OWN(AT_COLOR, 0, 0, KF_COLOR, K_PF_NORESET, 0, K_PALETTE - 1, 0, NC),
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int is_engine(const KApp *a, uint32_t uid) { return !k_track_missing(a, a->track) && k_engine_uid(a->sound[a->track]) == uid; }
static int drum_kit(const KApp *a) { return is_engine(a, K_UID('D', 'R', 'U', 'M')); }
static int phase_envelopes(const KApp *a) { return is_engine(a, K_UID('K', 'P', 'H', 'S')); }   /* DCO DCW DCA on ENV's page */
static int preset_page(KPage page) { return page == K_PAGE_SOUND || page == K_PAGE_EDIT || page == K_PAGE_ENV1 || page == K_PAGE_ENV2 || page == K_PAGE_ENV_DEST || page == K_PAGE_LFO || page == K_PAGE_FX; }
/* an insert's stored value: its slot, and which of its effect's parameters (-1 TYPE, -2 MIX) */
static int fx_param(int st, int *slot)
{
    if (st < 8) { *slot = st / 4; return st % 4 == 0 ? -1 : st % 4 == 3 ? -2 : st % 4 - 1; }
    *slot = (st - 8) / K_FX_EXTRA_PER_SLOT;
    return 2 + (st - 8) % K_FX_EXTRA_PER_SLOT;
}

KParamRef k_param_ref(const KApp *a, KPage page, int i)
{
    const int t = a->track;
    const KEngine *en = k_engine_at(a->sound[t]);
    KParamRef r = {K_P_RAW, (uint8_t)t, (uint8_t)page, 0, 0};
    KPage pg;
    int j;
    if (page == K_PAGE_SOUND && i >= en->page2) r.view = K_V_NOSHOW | ((a->lay & K_LAY_SYNTH_ALL) ? 0 : K_V_NOTURN);   /* (EDIT has the rest) */
    if (k_synth_all_sub(a, page, i, &pg, &j)) { KParamRef s = k_param_ref(a, pg, j); s.view |= r.view; return s; }   /* the Switch's SOUND: ENV, LFO */
    page = k_item_page(a, page, &i);   /* a pair's second page */
    r.page = (uint8_t)page;
#define ID(x) (r.id = (uint8_t)(x), r)
    if (page == K_PAGE_ENV1 && drum_kit(a)) return ID(K_P_KIT_ENV + (i & 3));   /* the lane's ATK PDEP PTIM CLIK */
    if (page == K_PAGE_FX) {
        int slot, param, st = k_fx_item(a, i, &slot, &param);
        if (param == -3) r.view |= K_V_DEAD;   /* (a row past the effect's, or no effect) */
        return ID(K_P_FX + st);
    }
    if (page == K_PAGE_CLOCK || page == K_PAGE_STEP || page == K_PAGE_DRUM || page == K_PAGE_SONG) return ID(i == 0 ? K_P_BPM : i == 1 ? K_P_SWING : i == 2 || page == K_PAGE_CLOCK || page == K_PAGE_SONG ? K_P_CLOCK_SRC : K_P_CHANCE);   /* the song's */
    if (page == K_PAGE_SOUND && i >= 0 && i < K_ENGINE_PARAMS) { r.idx = (uint8_t)i; return ID(K_P_ENG + i); }   /* the engine's own, in its order */
    if ((page == K_PAGE_ENV1 || page == K_PAGE_ENV2) && phase_envelopes(a)) {   /* DCO, DCW, DCA */
        const int k = clampi(i, 0, 11);
        if ((page == K_PAGE_ENV1 && i >= 4) || (page == K_PAGE_ENV2 && i == 4)) r.view |= K_V_C50;
        return ID(k < 4 ? K_P_PH_DCO + k : k < 8 ? K_P_ENV1 + k - 4 : K_P_PH_DCA + k - 8);
    }
    if (page == K_PAGE_ENV1 && i >= 4) return ID(K_P_DEST + (i - 4 < 3 ? i - 4 : 2));   /* ENV 1's destinations: PIT FLT SHP */
    if (page == K_PAGE_ENV2 && i >= 4) { if (i != 4) r.view |= K_V_PLAIN; return ID(K_P_DEST + 3); }   /* ENV 2's: the level */
    if (page == K_PAGE_EDIT) {   /* the engine's from page2 on */
        const int k = clampi(en->page2 + i, 0, 255);
        r.idx = (uint8_t)k;
        return ID(K_P_ENG + (k < K_ENGINE_PARAMS ? k : K_ENGINE_PARAMS - 1));
    }
    if (page == K_PAGE_ROLL) return ID(i == 0 ? K_P_ROLL_STEP : i == 1 ? K_P_ROLL_NOTE : i == 2 ? K_P_ROLL_LEN : K_P_ROLL_VEL);
    if (K_IS_HOME(page)) {   /* MACRO (the track in hand's four), then LEVEL, PAN, TRACK COLOR of each track */
        if (i < 4) { const int k = k_engine_macro(a->sound[t], i); r.idx = (uint8_t)k; return ID(K_P_ENG + k); }
        r.track = (uint8_t)K_HOME_TRACK(i);
        return ID(i >= K_HOME_COLOR0 ? K_P_COLOR : (i - 4) / K_TRACKS == 0 ? K_P_LEVEL : K_P_PAN);
    }
    if ((page == K_PAGE_BUS1 || page == K_PAGE_BUS2) && i >= 0 && i < 4) { r.track = 0; return ID((page == K_PAGE_BUS1 ? K_P_BUS1 : K_P_BUS2) + i); }
    if (page == K_PAGE_MIXER && i >= 0 && i < 4) return ID(K_P_LEVEL + i);
    if (i < 0) i = 0;   /* (never before the page's first) */
    if (page == K_PAGE_ENV1 && i < 4) return ID(K_P_ENV1 + i);
    if (page == K_PAGE_ENV2 && i < 4) return ID(K_P_ENV2 + i);
    if (page == K_PAGE_LFO && i < 9) return ID(K_P_LFO + i);
    if (page == K_PAGE_ARP && i < 4) return ID(K_P_ARP + i);
    if (page == K_PAGE_SCL && i < 4) return ID(K_P_SCL + i);
    if (page == K_PAGE_BUS1 || page == K_PAGE_BUS2) r.track = 0;
    r.idx = (uint8_t)(i >= K_MAX_PARAMS ? K_MAX_PARAMS - 1 : i);   /* (never past the page's slots) */
    return ID(K_P_RAW);
#undef ID
}

/* an outside drum's lanes taken for the track's engine: each lane starts from the track's values (a song from before the lanes, a
 * drum just chosen), as it played them. Another drum's lanes kept there are let go (one drum's lanes a track) */
static void lane_claim(KApp *a, int t, const KEngine *e, int m)
{
    int l, k;
    if (a->plug_lane_uid[t] == e->uid) return;
    for (l = 0; l < K_LANES; l++) for (k = 0; k < m; k++) a->plug_lane[t][l][k] = a->params[t][K_PAGE_SOUND][4 + k];
    a->plug_lane_uid[t] = e->uid;
}
int k_lane_val(const KApp *a, int t, int lane, int k)
{
    const int m = k_track_missing(a, t) ? 0 : k_plugin_lane_params(a->sound[t]);
    if (k < m && lane >= 0 && lane < K_LANES && a->plug_lane_uid[t] == k_engine_at(a->sound[t])->uid) return a->plug_lane[t][lane][k];
    return k >= 0 && k < K_ENGINE_PARAMS ? a->params[t][K_PAGE_SOUND][4 + k] : 0;
}
/* the engine's k-th parameter of the track: its slot, or, for a slot that stands for the host's (KF_LINK_FENV), the host's */
static KVal *eng_slot(KApp *a, int t, int k)
{
    const KEngine *e = k_engine_at(a->sound[t]);
    if (k >= K_ENGINE_PARAMS) k = K_ENGINE_PARAMS - 1;
    if (k < e->nparams && e->edit[k].fmt == KF_LINK_FENV) return &a->params[t][K_PAGE_ENV_DEST][1];
    if (k < 10 && e->uid == K_UID('D', 'R', 'U', 'M')) {   /* the lane in hand's: PIECE LVL PAN | TUNE TONE DCAY CLIK | ATK PDEP PTIM */
        static const uint8_t DRUM_LANE[10] = {0, 4, K_LANE_PAN, 1, 2, 3, K_LANE_ENV + 3, K_LANE_ENV, K_LANE_ENV + 1, K_LANE_ENV + 2};
        return &a->lane_p[t][a->cur_lane[t]][DRUM_LANE[k]];
    }
    if (k < k_plugin_lane_params(a->sound[t]) && !k_track_missing(a, t)) {   /* an outside drum's: the lane in hand's */
        lane_claim(a, t, e, k_plugin_lane_params(a->sound[t]));
        return &a->plug_lane[t][a->cur_lane[t] % K_LANES][k];
    }
    return &a->params[t][K_PAGE_SOUND][4 + k];
}
KVal *k_param_at(KApp *a, KParamRef r)
{
    const Entry *e = &T[r.id < K_P_COUNT ? r.id : K_P_RAW];
    const int t = r.track % K_TRACKS;
    switch (e->at) {
    case AT_SONG: return &a->params[0][e->page][e->idx];
    case AT_RAW: return &a->params[t][r.page % K_PAGE_COUNT][r.idx % K_MAX_PARAMS];
    case AT_BPM: return &a->bpm;
    case AT_SWING: return &a->swing;
    case AT_CLK: return &a->clock_src;
    case AT_CHANCE: a->chance_ui = (KVal)k_chance(a, t, a->pattern[t] & 3, a->cur[t] & 15); return &a->chance_ui;
    case AT_LANE: return &a->lane_p[t][a->cur_lane[t]][e->idx];
    case AT_CUR: return &a->cur[t];
    case AT_RNOTE: return &a->rnote[t];
    case AT_RLEN: return &a->rlen[t];
    case AT_RVEL: return &a->rvel[t];
    case AT_COLOR: return &a->tcolor[t];
    case AT_ENGINE: return eng_slot(a, t, r.idx);
    default: return &a->params[t][e->page][e->idx];
    }
}

/* where a turn stops by the descriptor: a - 0 + value's default (an effect's semitones: 50), a bipolar range's 0 */
static int desc_center(const KParamDesc *d)
{
    if (d->fmt == KF_CENTER || d->fmt == KF_FXSEMI) return d->fmt == KF_FXSEMI ? 50 : d->def;
    return d->fmt != KF_ENUM && d->min < 0 && d->max > 0 ? 0 : K_NO_CENTER;
}
int k_param_props(const KApp *a, KParamRef r, KParamProps *p)
{
    const Entry *e = &T[r.id < K_P_COUNT ? r.id : K_P_RAW];
    const int miss = k_track_missing(a, a->track);
    int derived = 0;
    p->d = (KParamDesc){"", e->fmt, e->min, e->max, e->def, 0, 0};
    p->flags = e->flags;
    if (e->at == AT_RAW && preset_page((KPage)r.page)) p->flags |= K_PF_PRESET;
    if (r.view & K_V_DEAD) return 0;
    switch (e->src) {
    case SRC_DESC: p->d = k_page_desc((KPage)e->page)[e->idx]; derived = 1; break;
    case SRC_ENGINE: {
        const KEngine *en = k_engine_at(a->sound[a->track]);
        if (miss || r.idx >= en->nparams) return 0;   /* no plugin: its values are kept, not turned; past its parameters: nothing */
        p->d = en->edit[r.idx];
        derived = 1;
        break;
    }
    case SRC_EFFECT: {
        int slot, param = fx_param(e->idx, &slot);
        const KPlugin *pl = k_effect_at(a->params[a->track][K_PAGE_FX][slot * 4]);
        if (!pl || !pl->desc || param >= pl->desc->nparams) return 0;
        p->d = pl->desc->edit[param];
        if (param == 0 && pl->desc->nparams > 2 && pl->desc->edit[2].fmt == KF_ONOFF && !strcmp(pl->desc->edit[2].label, "SYNC") &&
            k_switch_on(&pl->desc->edit[2], a->params[a->track][K_PAGE_FX][k_fx_store_param(slot, 2)]))
            p->d.fmt = KF_FXDIV;   /* (a switch called SYNC on: the first value a note division, DLY's TIME, WOBBLE's RATE) */
        derived = 1;
        break;
    }
    case SRC_FXTYPE: p->d.max = (int16_t)k_effect_count(); break;
    default: break;
    }
    p->lo = e->src == SRC_EFFECT ? e->min : p->d.min;   /* (an effect's: 0..100 as it is stored) */
    p->hi = e->src == SRC_EFFECT ? e->max : p->d.max;
    p->reset = e->src == SRC_EFFECT ? e->def : p->d.def;
    p->center = derived ? (miss ? K_NO_CENTER : (int16_t)desc_center(&p->d)) : e->center;
    if (r.view & K_V_PLAIN) { p->d.fmt = KF_PCT; p->center = K_NO_CENTER; }
    if (r.view & K_V_C50) p->center = 50;
    return 1;
}
static void packed(KApp *a, KParamRef r)   /* (a value kept packed elsewhere: back where it lives, after a write through k_param_at) */
{
    const int t = r.track % K_TRACKS;
    if (T[r.id < K_P_COUNT ? r.id : K_P_RAW].at == AT_CHANCE) k_chance_set(a, t, a->pattern[t] & 3, a->cur[t] & 15, a->chance_ui);
}
int k_ref_set(KApp *a, KParamRef r, int v)
{
    KParamProps p;
    if (!k_param_props(a, r, &p) || (p.flags & K_PF_NOSET)) return 0;
    *k_param_at(a, r) = T[r.id].src == SRC_ENGINE ? k_param_clamp(&p.d, v) : clampi(v, p.lo, p.hi);
    packed(a, r);
    if (p.flags & K_PF_DIRTY_SET) a->preset_dirty[a->track] = 1;
    return 1;
}
int k_ref_reset(KApp *a, KParamRef r)
{
    KParamProps p;
    if (!k_param_props(a, r, &p) || (p.flags & K_PF_NORESET)) return 0;
    *k_param_at(a, r) = p.reset;
    packed(a, r);
    if (p.flags & K_PF_DIRTY_RESET) a->preset_dirty[a->track] = 1;
    return 1;
}
void k_ref_turn(KApp *a, KParamRef r, int delta)
{
    KParamProps p;
    KVal *v;
    if (!k_param_props(a, r, &p) || (r.view & K_V_NOTURN) || p.hi <= p.lo) return;
    if (!(p.flags & K_PF_COARSE) && (p.d.fmt == KF_ENUM || p.d.fmt == KF_MNOTE || p.hi - p.lo <= 60)) delta = (delta > 0) - (delta < 0);   /* few values (a note: a semitone): one a detent */
    v = k_param_at(a, r);
    if (p.d.fmt == KF_ONOFF && p.hi - p.lo > 1) *v = delta > 0 ? p.hi : p.lo;   /* (an effect's switch, kept 0..100: on, off at a detent) */
    else *v = clampi(*v + delta, p.lo, p.hi);
    packed(a, r);
}
int k_ref_center(const KApp *a, KParamRef r)
{
    KParamProps p;
    return k_param_props(a, r, &p) ? p.center : K_NO_CENTER;
}
