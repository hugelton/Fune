/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The parameters of the pages that have no engine behind them: LFO, ARP, SCL and CLOCK, as Felucca 2.0 names them (its
 * params.c: the labels, the ranges, the value names; the author's own). Only the first page of each family, and none of the
 * finer pages (LFO 2, the matrix, ARP 2, the chord keys) for now. */
#include "kreisel.h"
#include <string.h>
#include "kreisel_plugin.h"
#include "kreisel_engine_api.h"
#include "kreisel_fx.h"

static const char *const N_LWAVE[] = {"Sin", "Tri", "Saw", "Sqr", "S&H", 0};
static const char *const N_AMODE[] = {"Off", "Up", "Dn", "UpDn", "Rnd", "Ord", "Repeat", "DnUp", "Up+8", "Conv", "Divg", "Pinky",
                                      "Thumb", "Walk", "Chord", 0}   /* (words, not capitals) */;
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2bar", "4bar", 0};
static const char *const N_SCALE[] = {"Chr", "Maj", "Min", "Dor", "Mix", "Pen", "MPen", "Harm", "Phry", "Lyd", "Loc", "Mel", "Blues",
                                      "Whole", "DimHW", "DimWH", 0};
static const char *const N_QUANT[] = {"Off", "Snap", "White", "Seq", "Seq+T", 0};
#if defined(KREISEL_RTPMIDI) && KREISEL_RTPMIDI   /* (the Switch: its clock comes over the network, RTP-MIDI, as source 1) */
static const char *const N_CLOCK[] = {"INT", "NET", "TRS", 0};
#else
static const char *const N_CLOCK[] = {"INT", "USB", "TRS", 0};
#endif

static const KParamDesc ROLL[4] = {   /* the step in hand, and its note, length and velocity */
    {"STEP", KF_STEPS, 0, 15, 0, 0, 0}, {"NOTE", KF_MNOTE, 24, 96, 60, 0, 0}, {"LEN", KF_INT, 1, 16, 1, 0, 0}, {"VEL", KF_INT, 1, 127, 100, 0, 0},
};
static const char *const N_LLANE[] = {"All", "1", "2", "3", "4", "5", "6", "7", "8", 0};
static const KParamDesc LFO[9] = {   /* the LFO, then (the second row) where it goes: Felucca's LFO DEST, 0 = none; a drum track's: which lane */
    {"RATE", KF_LFOHZ, 0, 127, 60, 0, 0}, {"WAVE", KF_ENUM, 0, 4, 0, N_LWAVE, 0}, {"PHS", KF_INT, 0, 127, 0, 0, 0}, {"FADE", KF_TIME, 0, 127, 0, 0, 0},
    {"PIT", KF_BIPCT, -64, 63, 0, 0, 0}, {"FLT", KF_BIPCT, -64, 63, 0, 0, 0}, {"SHP", KF_BIPCT, -64, 63, 0, 0, 0}, {"AMP", KF_BIPCT, -64, 63, 0, 0, 0},
    {"LANE", KF_ENUM, 0, 8, 0, N_LLANE, 0},
};
static const KParamDesc ARP[4] = {
    {"MODE", KF_ENUM, 0, 14, 0, N_AMODE, 0}, {"RATE", KF_ENUM, 0, 9, 2, N_DIV, 0}, {"OCT", KF_INT, 1, 4, 1, 0, 0}, {"GATE", KF_PCT, 1, 127, 64, 0, 0},
};
static const KParamDesc SCL[4] = {
    {"ROOT", KF_NOTE, 0, 11, 0, 0, 0}, {"SCL", KF_ENUM, 0, 15, 0, N_SCALE, 0}, {"QNT", KF_ENUM, 0, 4, 0, N_QUANT, 0}, {"TRN", KF_SEMI, -24, 24, 0, 0, 0},
};
static const char *const N_PROB[] = {"100%", "75%", "50%", "25%", 0};   /* a step's chance (KApp.chance: 0 always .. 3 a quarter) */
static const KParamDesc SEQC[4] = {   /* STEP's and DRUM's cards: the clock's three, then the step in hand's chance (a step's
                                       * value is a card's, not a tool's) */
    {"BPM", KF_BPM, 40, 240, 120, 0, 0}, {"SWG", KF_PCT, 0, 100, 0, 0, 0}, {"CLK", KF_ENUM, 0, 2, 0, N_CLOCK, 0}, {"PROB", KF_ENUM, 0, 3, 0, N_PROB, 0},
};

const KParamDesc *k_page_desc(KPage page)
{
    switch (page) {
    case K_PAGE_ROLL: return ROLL;
    case K_PAGE_STEP: case K_PAGE_DRUM: return SEQC;
    case K_PAGE_SONG: return SEQC;   /* (the clock's: four entries, as every page's table is read; SONG has no cards, CLOCK counts three) */
    case K_PAGE_LFO: return LFO;
    case K_PAGE_ARP: return ARP;
    case K_PAGE_SCL: return SCL;
    case K_PAGE_CLOCK: return SEQC;
    default: return 0;
    }
}

/* ----------------------------------------------------------------- the plugin registry --- */
/* The built-in engines are plugins like any other (kreisel_plugin.h), first and in a fixed order; registered ones follow.
 * Every lookup is checked: an index from a song or a knob can never reach past the table. Their records (K_BUILTIN_PLUGINS) are
 * in kreisel_audio.c, beside their DSP; they pass the same k_plugin_check (tests/plugin_test.c). */
static KPlugin slot[K_PLUGIN_SLOTS];   /* copies: a registered record need not outlive the call (its descriptor and DSP must) */
static int nslot;
static KPlugin effect_slot[K_EFFECT_PLUGIN_SLOTS];
static int neffect;

int k_plugin_count(void) { return K_ENGINE_COUNT + nslot; }
const KPlugin *k_plugin_at(int i)
{
    if ((unsigned)i < K_ENGINE_COUNT) return &K_BUILTIN_PLUGINS[i];
    if (i >= K_ENGINE_COUNT && i < K_ENGINE_COUNT + nslot) return &slot[i - K_ENGINE_COUNT];
    return &K_BUILTIN_PLUGINS[0];
}
int k_effect_count(void) { return (int)K_FX_BUILTINS + neffect; }
const KPlugin *k_effect_at(int type)
{
    if (type <= 0 || type > k_effect_count()) return 0;
    if (type <= (int)K_FX_BUILTINS) return k_fx_builtin_at((unsigned)(type - 1));
    return &effect_slot[type - (int)K_FX_BUILTINS - 1];
}
int k_effect_find(uint32_t uid)
{
    for (int i = 1; uid && i <= k_effect_count(); i++)
        if (k_effect_at(i)->desc->uid == uid) return i;
    return 0;
}
void k_plugin_reset(void) { nslot = 0; neffect = 0; }
static KDeviceCaps caps;
static void host_room(void);
static int caps_set;
const KDeviceCaps *k_plugin_caps(void) { return caps_set ? &caps : 0; }
void k_plugin_set_caps(const KDeviceCaps *c) { caps_set = c != 0; if (c) caps = *c; host_room(); }
int k_plugin_fits(const KPlugin *p, const KDeviceCaps *c, uint32_t used)
{
    if (!p || !c) return 0;
    if (p->fs && c->fs && p->fs != c->fs) return K_PLUG_E_RATE;
    if (p->needs & ~c->has) return K_PLUG_E_NEED;
    if (p->ram && (p->ram > c->ram || used > c->ram - p->ram)) return K_PLUG_E_RAM;
    if (p->cost && (uint32_t)p->cost * (p->kind == K_PLUG_EFFECT ? 1u : (c->voices ? c->voices : 1u)) > c->cost) return K_PLUG_E_CPU;
    return 0;
}
static int uid_ok(uint32_t uid)   /* four printable characters, at least one lower case (Hügelton's own: upper case and digits) */
{
    int k, lower = 0;
    for (k = 0; k < 4; k++) {
        unsigned ch = (uid >> (24 - 8 * k)) & 255u;
        if (ch < 0x21u || ch > 0x7Eu) return 0;
        lower |= ch >= 'a' && ch <= 'z';
    }
    return lower;
}
int k_plugin_lane_note(int index, int lane)
{
    static const uint8_t GM[K_LANES] = {36, 38, 42, 46, 39, 45, 50, 37};   /* BD SD CH OH CP LT HT RS */
    const KPlugin *p = k_plugin_at(index);
    if (lane < 0 || lane >= K_LANES) return 36;
    return p->lane_notes && p->lane_notes[lane] && p->lane_notes[lane] < 128 ? p->lane_notes[lane] : GM[lane];
}
int k_plugin_lane_params(int index)
{
    const KPlugin *p;
    if (index < 0 || index >= k_plugin_count()) return 0;
    p = k_plugin_at(index);
    if (p->kind != K_PLUG_SYNTH || !p->desc->drum) return 0;
    return p->desc->nparams < K_LANE_VALS ? p->desc->nparams : K_LANE_VALS;
}
/* ------------------------------------------------- a plugin's screens, the host's view of itself (kreisel_plugin_ui.h) --- */
static KHostApi host_api = {sizeof(KHostApi), K_HOST_API_VERSION, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const KHostApi *k_host_api(void) { return &host_api; }
static void host_room(void)
{
    host_api.fs = caps_set ? caps.fs : 0; host_api.ram = caps_set ? caps.ram : 0; host_api.cost = caps_set ? caps.cost : 0;
    host_api.voices = caps_set ? caps.voices : 0; host_api.has = caps_set ? caps.has : 0;
}
int k_screen_kind(int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (w == h && w <= 260) return K_SCR_SQUARE;
    return w <= 400 && h <= 240 ? K_SCR_SMALL : K_SCR_LARGE;
}
void k_host_set_screen(int w, int h, unsigned input, int knobs)
{
    host_api.screen = (uint8_t)k_screen_kind(w, h);
    host_api.screen_w = (uint16_t)(w > 0 ? w : 0); host_api.screen_h = (uint16_t)(h > 0 ? h : 0);
    host_api.input = (uint16_t)input; host_api.knobs = (uint8_t)(knobs > 0 ? knobs : 0);
}
/* the extension as far as the plugin knows it (its size), or 0 */
static const KPluginExt *plugin_ext(const KPlugin *p)
{
    const KPluginExt *e = p ? p->ext : 0;
    return e && e->version >= 1 && e->size >= offsetof(KPluginExt, draw) ? e : 0;
}
static void (*ext_draw(const KPluginExt *e))(const KDraw *, int, const int16_t *, int)
{
    return e->size >= offsetof(KPluginExt, draw) + sizeof e->draw ? e->draw : 0;
}
/* its layouts are sound: grids of 1 .. 16, cells inside them, parameters its own (a widget of a kind this host does not know: fine),
 * pages its kind has (an effect: FX alone; a drum: SOUND, EDIT, DRUM; a synth: SOUND, EDIT) */
static int layouts_ok(const KPluginExt *e, const KEngine *d, int kind)
{
    const int fx = kind == K_PLUG_EFFECT, drum = kind == K_PLUG_DRUM || d->drum;
    int l, k;
    if (!e || !e->nlayouts) return 1;
    if (!e->layouts) return 0;
    for (l = 0; l < e->nlayouts; l++) {
        const KLayout *L = &e->layouts[l];
        if (!L->cols || !L->rows || L->cols > 16 || L->rows > 16 || L->page >= K_LPAGE_COUNT || (L->n && !L->w)) return 0;
        if (fx ? L->page != K_LPAGE_FX : L->page == K_LPAGE_FX || (L->page == K_LPAGE_DRUM && !drum)) return 0;
        for (k = 0; k < L->n; k++) {
            const KWidget *w = &L->w[k];
            const int need = w->kind == K_W_ENV ? 4 : w->kind == K_W_LABEL || w->kind == K_W_SCOPE || w->kind == K_W_PICTURE || w->kind >= K_W_COUNT ? 0 : 1;
            if (!w->w || !w->h || w->x + w->w > L->cols || w->y + w->h > L->rows) return 0;
            if (need && !(fx && need == 1 && w->param == K_WP_MIX) && (w->param < 0 || w->param + need > d->nparams)) return 0;
            if (w->kind == K_W_PICTURE && !ext_draw(e)) return 0;
        }
    }
    return 1;
}
static const KLayout *ext_layout(const KPluginExt *e, int page, int screen)
{
    int l;
    for (l = 0; screen && e && e->layouts && l < e->nlayouts; l++)
        if (e->layouts[l].page == page && (e->layouts[l].screens & screen)) return &e->layouts[l];
    return 0;
}
const KLayout *k_plugin_layout(int engine, int page, int screen)
{
    if (engine < K_ENGINE_COUNT || engine >= k_plugin_count()) return 0;
    return ext_layout(plugin_ext(k_plugin_at(engine)), page, screen);
}
const KLayout *k_effect_layout(int type, int screen)
{
    return type > (int)K_FX_BUILTINS && type <= k_effect_count() ? ext_layout(plugin_ext(k_effect_at(type)), K_LPAGE_FX, screen) : 0;
}
void k_plugin_draw(int engine, const KDraw *g, int id, const int16_t *values, int n)
{
    const KPluginExt *e = engine >= K_ENGINE_COUNT && engine < k_plugin_count() ? plugin_ext(k_plugin_at(engine)) : 0;
    void (*f)(const KDraw *, int, const int16_t *, int) = e ? ext_draw(e) : 0;
    if (f) f(g, id, values, n);
}
void k_effect_draw(int type, const KDraw *g, int id, const int16_t *values, int n)
{
    const KPluginExt *e = type > (int)K_FX_BUILTINS && type <= k_effect_count() ? plugin_ext(k_effect_at(type)) : 0;
    void (*f)(const KDraw *, int, const int16_t *, int) = e ? ext_draw(e) : 0;
    if (f) f(g, id, values, n);
}

static int hugelton_uid(uint32_t uid)   /* Hügelton's own: four upper-case letters or digits */
{
    int k;
    for (k = 0; k < 4; k++) {
        unsigned ch = (uid >> (24 - 8 * k)) & 255u;
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= '0' && ch <= '9')) return 0;
    }
    return 1;
}
uint16_t k_param_id(const char *s)
{
    uint32_t h = 2166136261u;
    while (s && *s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    h = (h >> 16) ^ (h & 0xFFFFu);
    return (uint16_t)(h ? h : 1);
}
int k_plugin_check(const KPlugin *p, int builtin)
{
    const KEngine *d;
    int i, j;
    KPlugin rec = {0};
    if (!p) return K_PLUG_E_NULL;
    if (p->size < K_PLUGIN_SIZE_1 || p->abi != K_PLUGIN_ABI) return K_PLUG_E_ABI;
    memcpy(&rec, p, p->size < sizeof rec ? p->size : sizeof rec);   /* what it knows; the rest 0 */
    p = &rec;
    if (!p->desc) return K_PLUG_E_NULL;
    d = p->desc;
    if (builtin ? !hugelton_uid(d->uid) : !uid_ok(d->uid)) return K_PLUG_E_UID;
    if (p->version < 1 || p->version > K_ENGINE_VERSION_MAX) return K_PLUG_E_VERSION;
    if (p->kind == K_PLUG_SYNTH ? !p->dsp || (d->drum && !d->lanes[0]) : p->kind == K_PLUG_DRUM ? !d->drum || !d->lanes[0] : p->kind == K_PLUG_EFFECT ? !p->dsp || d->drum : 1) return K_PLUG_E_KIND;
    if (!d->name || !d->name[0]) return K_PLUG_E_DESC;
    if (d->nparams < 1 || d->nparams > K_ENGINE_PARAMS ||
        (p->kind == K_PLUG_EFFECT && d->nparams > 2 + K_FX_EXTRA_PER_SLOT)) return K_PLUG_E_DESC;
    for (i = 0; i < d->nparams; i++) {
        const KParamDesc *e = &d->edit[i];
        if (!e->label || e->min > e->max || e->def < e->min || e->def > e->max) return K_PLUG_E_DESC;
        for (j = 0; j < i; j++) if (k_param_id(d->edit[j].label) == k_param_id(e->label)) return K_PLUG_E_DESC;   /* (a song could not tell them apart) */
    }
    for (i = 0; i < 4; i++) if (d->macro[i] >= d->nparams) return K_PLUG_E_DESC;
    if (d->page2 > d->nparams) return K_PLUG_E_DESC;   /* (EDIT starts within its parameters) */
    if (p->ext && !plugin_ext(p)) return K_PLUG_E_DESC;   /* (an extension it says it has, but that is not one) */
    if (!layouts_ok(plugin_ext(p), d, p->kind)) return K_PLUG_E_DESC;
    return 0;
}

int k_plugin_register(const KPlugin *p)
{
    int i;
    KPlugin rec = {0};
    uint32_t used = 0;
    if ((i = k_plugin_check(p, 0)) != 0) return i;
    memcpy(&rec, p, p->size < sizeof rec ? p->size : sizeof rec);
    p = &rec;
    if (k_engine_find(p->desc->uid) >= 0 || k_effect_find(p->desc->uid)) return K_PLUG_E_TAKEN;
    if (p->kind == K_PLUG_EFFECT ? neffect >= K_EFFECT_PLUGIN_SLOTS : nslot >= K_PLUGIN_SLOTS) return K_PLUG_E_FULL;
    for (i = 0; i < nslot; i++) used += slot[i].ram;
    for (i = 0; i < neffect; i++) used += effect_slot[i].ram;
    if (caps_set && (i = k_plugin_fits(p, &caps, used)) != 0) return i;
    if (plugin_ext(p) && plugin_ext(p)->attach) { host_room(); plugin_ext(p)->attach(&host_api); }
    if (p->kind == K_PLUG_EFFECT) {
        effect_slot[neffect] = *p;
        effect_slot[neffect].size = sizeof(KPlugin);
        return (int)K_FX_BUILTINS + ++neffect;
    }
    slot[nslot] = *p;
    slot[nslot].size = sizeof(KPlugin);
    return K_ENGINE_COUNT + nslot++;
}

/* the engines by the registry: a track's sound is an index into it */
int k_engine_count(void) { return k_plugin_count(); }
const KEngine *k_engine_at(int index) { return k_plugin_at(index)->desc; }
uint32_t k_engine_uid(int index) { return index >= 0 && index < k_plugin_count() ? k_plugin_at(index)->desc->uid : 0; }
int k_engine_find(uint32_t uid)
{
    int i, n = k_plugin_count();
    for (i = 0; uid && i < n; i++)
        if (k_plugin_at(i)->desc->uid == uid) return i;
    return -1;
}
int k_param_clamp(const KParamDesc *d, int v)
{
    int hi = d->max;
    if (d->fmt == KF_ENUM && d->names) {   /* an enum stays inside its names, whatever the descriptor says */
        int n = 0;
        while (d->names[n]) n++;
        if (n && hi > n - 1) hi = n - 1;
    }
    return v < d->min ? d->min : v > hi ? hi : v;
}
