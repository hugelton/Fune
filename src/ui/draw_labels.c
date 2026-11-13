/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* What a page's items are called and how their values read: the names, the icons (pages, engines, effects, presets),
 * the formats of the values (ui_param_name, ui_item_name, ui_item_value, ...). */
#include "draw_int.h"

/* PHASE's three envelopes (DCO DCW DCA) on the ENV page's items: all envelope times and levels, none a destination (kreisel.c) */
int phase_env_ui(const KApp *a) { return a && !k_track_missing(a, a->track) && k_engine_uid(a->sound[a->track]) == K_UID('K', 'P', 'H', 'S'); }
const char *ui_param_name(KPage page, int index)
{
    if (K_IS_HOME(page)) { static const char *const R[4] = {"MAC", "LEVEL", "PAN", "Color"}; return R[index < 4 ? 0 : 1 + (index - 4) / K_TRACKS]; }   /* its rows: for the four tracks, each card with its track's colour */
    /* a label has to fit the card on the smallest screen (55 px, 43 inside: ui_test checks); LEVEL is spelled out */
    static const char *env1[4] = {"ATK", "DEC", "SUS", "REL"};
    static const char *env2[4] = {"DLY", "HOLD", "CRV", "AMT"};
    static const char *dest[4] = {"PIT", "FLT", "SHP", "AMP"};
    static const char *mixer[4] = {"LEVEL", "PAN", "BUS1", "BUS2"};   /* what a row of the mixer sets, for the four tracks */
    static const char *bus1[4] = {"SIZE", "DCY", "TONE", "RTN"};    /* a reverb on bus 1 */
    static const char *bus2[4] = {"TIME", "FB", "TONE", "RTN"};       /* a delay on bus 2 */
    const char **p = env1;
    if (page == K_PAGE_SOUND) return "";   /* the synth's labels are its engine's (ui_item_name) */
    if (page == K_PAGE_ENV1 && index >= 4) { static const char *const D1[4] = {"PIT", "FLT", "SHP", ""}; return D1[index & 3]; }   /* where ENV 1 goes */
    if (page == K_PAGE_ENV2 && index >= 4) return index == 4 ? "LVL" : "";   /* where ENV 2 goes */
    switch (page) {
    case K_PAGE_ENV1: p = env1; break;
    case K_PAGE_ENV2: p = env2; break;
    case K_PAGE_ENV_DEST: p = dest; break;
    case K_PAGE_FX: return "FX";
    case K_PAGE_MIXER: return mixer[index];
    case K_PAGE_BUS1: p = bus1; break;
    case K_PAGE_BUS2: p = bus2; break;
    default: break;
    }
    return index >= 0 && index < 4 ? p[index] : "";   /* (a page of more items, LFO or EDIT, past its four: no label of its own) */
}
int ui_param_icon(KPage page, int index)
{
    if (K_IS_HOME(page)) page = K_PAGE_MIXER;
    static const int8_t env1[4] = {GI_P_ATTACK, GI_P_DECAY, GI_P_SUSTAIN, GI_P_RELEASE};
    static const int8_t env2[4] = {GI_P_TIME, GI_P_HOLD, GI_P_CURVE, GI_P_AMOUNT};
    static const int8_t dest[4] = {GI_P_PITCH, GI_P_CUTOFF, GI_P_SHAPE, GI_P_LEVEL};
    static const int8_t fx[4] = {GI_P_DIST, GI_P_CHORUS, GI_P_ECHO, GI_P_REVERB};
    static const int8_t bus1[4] = {GI_P_SIZE, GI_P_DECAY, GI_P_TONE, GI_P_RETURN};
    static const int8_t bus2[4] = {GI_P_TIME, GI_P_FEEDBACK, GI_P_TONE, GI_P_RETURN};
    const int8_t *p;
    if (index < 0 || index > 3 || page == K_PAGE_SOUND) return -1;
    switch (page) {
    case K_PAGE_ENV1: p = env1; break;
    case K_PAGE_ENV2: p = env2; break;
    case K_PAGE_ENV_DEST: p = dest; break;
    case K_PAGE_FX: p = fx; break;
    case K_PAGE_BUS1: p = bus1; break;
    case K_PAGE_BUS2: p = bus2; break;
    default: return -1;   /* the mixer's items carry a track's number instead */
    }
    return p[index];
}
#define ENGINE_NAME(k) (k_engine_at(k)->name)
/* Built-in sound icons in registry order (DRUM remains last). */
int engine_icon(int k)
{
    static const short I[K_ENGINE_COUNT] = {GI_ENG_ANALOG, GI_ENG_PHASE, GI_ENG_TRIO, GI_ENG_LOFI, GI_ENG_WHEEL,
#if K_WITH_ENSEMBLE
                                            GI_P_WAVE,
#endif
#if K_WITH_VECTOR
                                            GI_P_WAVE,
#endif
                                            GI_ENG_PHASE, GI_P_NOISE};   /* (the table's order) */
    return (unsigned)k < K_ENGINE_COUNT ? I[k] : GI_P_GENERIC;
}
/* the preset of a track that plays none (no factory or user preset picked): INIT, as a synth's init patch (the names each
 * track once had, "Sub Bass" and so on, read as presets that were not there: 2026-11-07); a plugin: its name */
const char *preset_name(int engine, int track)
{
    (void)track;
    if ((unsigned)engine >= K_ENGINE_COUNT) return k_engine_at(engine)->name;
    return "INIT";
}
/* a track's engine and preset as shown: a plugin missing says NO PLUGIN, and its id where the preset goes */
const char *gone_word(const KApp *a, int t) { return k_track_newer(a, t) ? "Update" : "No plugin"; }   /* (a newer version named) */
const char *trk_engine(const KApp *a, int t) { return k_track_missing(a, t) ? gone_word(a, t) : k_engine_at(a->sound[t])->name; }
const char *trk_preset(const KApp *a, int t)
{
    static char id[8];
    if (k_track_missing(a, t)) {
        uint32_t u = a->missing[t].uid;
        int k;
        for (k = 0; k < 4; k++) { unsigned ch = (u >> (24 - 8 * k)) & 255u; id[k] = (char)(ch >= 0x21u && ch <= 0x7Eu ? ch : '?'); }
        id[4] = 0;
        return id;
    }
    if (a->preset_name[t][0]) {   /* a user preset's or a factory one's (PRESETS, SAVE > SOUND); changed since: a '*' after it */
        static char nm[K_NAME_LEN + 2];
        if (!a->preset_dirty[t] || (a->preset_slot[t] < 0 && a->factory[t] < 0)) return a->preset_name[t];
        snprintf(nm, sizeof nm, "%s*", a->preset_name[t]);
        return nm;
    }
    return preset_name(a->sound[t], t);
}
/* the engine's icon before a preset's name (its line work, size px square, centred on cy): the width it took, 0 for a missing plugin */
int preset_icon(const KApp *a, int t, int x, int cy, int size, uint32_t col)
{
    if (k_track_missing(a, t)) return 0;
    g_icon(&cv, x, cy - size / 2, size, (GIcon)engine_icon(a->sound[t]), col);
    return size + 4;
}
/* the inserts (FX page): what an effect is called, its two parameters, and their icons */
const char *fx_name(int type)
{
    const KPlugin *p = k_effect_at(type);
    return p && p->desc ? p->desc->name : "None";
}
static const char *fx_label(int type, int param)
{
    const KPlugin *p = k_effect_at(type);
    return p && p->desc && param >= 0 && param < p->desc->nparams ?
           p->desc->edit[param].label : "-";
}
int fx_icon(int type)   /* an effect's icon, by its name (a plugin's own order may differ): Fukiai's nearest */
{
    static const struct { const char *name; int icon; } M[] = {
        {"DIST", GI_P_DIST}, {"CHO", GI_P_CHORUS}, {"DLY", GI_P_ECHO}, {"REV", GI_P_REVERB}, {"GAIN", GI_P_LEVEL}, {"FILTER", GI_FX_FILTER},
        {"FUZZ", GI_P_FOLD}, {"WAH", GI_FX_WAH}, {"CRUSH", GI_P_BITS}, {"PHASER", GI_FX_PHASER}, {"TREMOLO", GI_FX_TREMOLO},
        {"WOBBLE", GI_FX_WOBBLE}, {"NOISE", GI_P_NOISE}, {"PITCH", GI_P_PITCH}, {"AMP", GI_FX_AMP}};
    const KPlugin *p = type ? k_effect_at(type) : 0;
    size_t i;
    if (!type) return -1;
    if (p && p->desc && p->desc->name) for (i = 0; i < sizeof M / sizeof M[0]; i++) if (!strcmp(M[i].name, p->desc->name)) return M[i].icon;
    return GI_P_GENERIC;
}
int fx_type(const KApp *a, int i)
{
    int slot = 0, t;
    k_fx_item(a, i, &slot, 0);
    t = a->params[a->track][K_PAGE_FX][slot * 4];
    return t < 0 ? 0 : t > k_effect_count() ? k_effect_count() : t;
}
uint32_t fx_accent(int type)
{
    const KPlugin *p = k_effect_at(type);
    uint32_t col = p && p->desc ? k_fx_color_for_uid(p->desc->uid) : 0;
    return col ? col : th.mid; /* NONE and unclassified plugins use a neutral accent */
}
/* the label, the icon and the value text of an item of the page in hand: a page's own, or (the inserts) by the effect */
const char *ui_item_name(const KApp *a, KPage page, int i)
{
    if (a) page = k_item_page(a, page, &i);   /* a pair's second page */
    if (a && drum_track(a) && page == K_PAGE_ENV1) { static const char *const N[4] = {"ATK", "PDEP", "PTIM", "CLIK"}; return N[i & 3]; }
    if (phase_env_track(a) && (page == K_PAGE_ENV1 || page == K_PAGE_ENV2)) {   /* DCO | DCW | DCA, A D S R each */
        static const char *const N[4] = {"ATK", "DEC", "SUS", "REL"};
        return i >= 0 && i < 12 ? N[i & 3] : "";
    }
    if (a && drum_track(a) && page == K_PAGE_LFO && i >= 4 && i < 8) { static const char *const N[4] = {"PIT", "TONE", "DCAY", "LVL"}; return N[i & 3]; }
    if (k_page_desc(page)) return (page == K_PAGE_CLOCK || page == K_PAGE_SONG) && (i < 0 || i >= 3) ? "" : k_page_desc(page)[i].label;   /* LFO, ARP, SCL, CLOCK: the descriptor's
                                                 * (the clock's three: the fourth is STEP's and DRUM's chance) */
    if (page == K_PAGE_SOUND && i >= k_engine_at(a->sound[a->track])->page2) return "";   /* past its SOUND half */
    if (page == K_PAGE_EDIT) { const KEngine *en = k_engine_at(a->sound[a->track]); return k_track_missing(a, a->track) || en->page2 + i >= en->nparams ? "" : en->edit[en->page2 + i].label; }   /* its second half */
    if (k_track_missing(a, a->track) && ((K_IS_HOME(page) && i < 4) || page == K_PAGE_SOUND)) return "";   /* no plugin: no knobs to show */
    if (page == K_PAGE_SOUND) return k_engine_at(a->sound[a->track])->edit[i].label;   /* its engine's, from K1 */
    if (K_IS_HOME(page) && i < 4) return k_engine_at(a->sound[a->track])->edit[k_engine_macro(a->sound[a->track], i)].label;   /* the track in hand's macros */
    if (page != K_PAGE_FX) return ui_param_name(page, i);
    {
        int slot, param;
        k_fx_item(a, i, &slot, &param);
        return param == -1 ? (slot ? "FX2" : "FX1") :
               param == -2 ? "MIX" : param == -3 ? "" : fx_label(fx_type(a, i), param);
    }
}
/* an icon by an engine parameter's label, as Felucca 2.0 chooses (icons.c): what a label is about; a knob for the rest */
static int label_icon(const char *l)
{
    static const struct { const char *label; int8_t icon; } M[] = {
        {"WAVE", GI_P_WAVE}, {"WAVE2", GI_P_WAVE}, {"DTN", GI_P_DETUNE}, {"MIX", GI_P_MIX}, {"NOIS", GI_P_NOISE}, {"CUT", GI_P_CUTOFF},
        {"FREQ", GI_P_CUTOFF}, {"RES", GI_P_RESO}, {"DRV", GI_P_DRIVE}, {"KTR", GI_P_KEYTRACK}, {"TRK", GI_P_KEYTRACK}, {"TONE", GI_P_TONE},
        {"COLR", GI_P_TONE}, {"SUB", GI_P_SUB}, {"ENV", GI_P_ENV}, {"CRSH", GI_P_BITS}, {"FOLD", GI_P_FOLD}, {"INT2", GI_P_PITCH},
        {"INT3", GI_P_PITCH}, {"PW", GI_P_SHAPE}, {"DCW", GI_P_SHAPE}, {"DRFT", GI_P_NOISE}, {"DENS", GI_P_NOISE}, {"CLICK", GI_P_ATTACK},
        {"MODE", GI_P_WAVE}, {"CHIP", GI_P_BITS}, {"VIB", GI_P_TIME}, {"SWP", GI_P_PITCH}, {"ROTR", GI_P_TIME},
        {"RATE", GI_P_TIME}, {"PHS", GI_P_DETUNE}, {"FADE", GI_P_FADE}, {"OCT", GI_P_OCTAVE}, {"GATE", GI_P_GATE}, {"ROOT", GI_P_PITCH},
        {"SCL", GI_P_SIZE}, {"QNT", GI_P_QUANT}, {"TRN", GI_P_TRANSPOSE}, {"BPM", GI_P_TEMPO}, {"SWG", GI_P_SWING}, {"CLK", GI_P_TIME},
        {"STEP", GI_PAGE_PATTERN}, {"NOTE", GI_PAGE_SONG}, {"LEN", GI_P_LEN}, {"VEL", GI_P_VEL}, {"LVL", GI_P_LEVEL},
        {"TUN", GI_P_TUNE}, {"DCY", GI_P_DECAY},
    };
    unsigned i;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        if (!strcmp(l, M[i].label)) return M[i].icon;
    return GI_P_GENERIC;
}
int ui_item_icon(const KApp *a, KPage page, int i)
{
    int t;
    if (a) page = k_item_page(a, page, &i);
    if (phase_env_track(a) && (page == K_PAGE_ENV1 || page == K_PAGE_ENV2)) return i >= 0 && i < 12 ? ui_param_icon(K_PAGE_ENV1, i & 3) : -1;
    if (k_page_desc(page)) return k_page_desc(page)[i].label[0] ? label_icon(k_page_desc(page)[i].label) : -1;
    if (K_IS_HOME(page) && i < 4) return ui_item_name(a, page, i)[0] ? label_icon(ui_item_name(a, page, i)) : -1;
    if (page == K_PAGE_SOUND) return i >= k_engine_at(a->sound[a->track])->nparams ? -1 : label_icon(k_engine_at(a->sound[a->track])->edit[i].label);
    if (page != K_PAGE_FX) return ui_param_icon(page, i);
    {
        int slot, param;
        k_fx_item(a, i, &slot, &param);
        t = fx_type(a, i);
        return param == -1 ? fx_icon(t) : param == -2 ? GI_P_MIX :
               param == -3 ? -1 : label_icon(fx_label(t, param));
    }
}
/* 2^(off + v sl / 127) * 2^q for a value 0..127 on an exponential scale (off in Q24, sl in Q20): the text of a cutoff, a rate, a time
 * without float (the FM-1's printf has no float either) */
static uint32_t exp_v(int v, int32_t off24, int32_t sl20, int q)
{
    const int32_t m = (v < 0 ? 0 : v > 127 ? 127 : v) * sl20;   /* (< 2^31) */
    return g_exp2(off24 + (m / 127) * 16 + ((m % 127) * 16 + 63) / 127, q);
}
/* the value as text, and its unit ("" for none); returns 0 when the item is not a number (an insert's effect, an empty slot) */
/* a value by its descriptor's format: a percentage of 0..127, a cutoff in Hz, a rate in Hz, a time, a note, a name ...; 0: not a number */
static int format_desc(const KParamDesc *d, int v, char *b, size_t n, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case KF_PCT: snprintf(b, n, "%d", d->max == 100 ? v : (v * 100 + 63) / 127); *unit = "%"; return 1;
    case KF_CUTOFF: {   /* 30 Hz .. 16 kHz, exponential: hz * 256 */
        const uint32_t h = exp_v(v, 82323963, 9498938, 8);
        if (h < 1000u * 256u) { snprintf(b, n, "%d", (int)((h + 128u) >> 8)); *unit = "Hz"; }
        else { const unsigned d = (h + 12800u) / 25600u; snprintf(b, n, "%u.%u", d / 10u, d % 10u); *unit = "kHz"; }
        return 1;
    }
    case KF_LFOHZ: {   /* 0.05 .. 40 Hz, exponential: hz * 2^20 */
        const uint32_t h = exp_v(v, -72509921, 10112316, 20);
        if (h < (10u << 20)) { const unsigned d = (unsigned)((h * 25u + (1u << 17)) >> 18); snprintf(b, n, "%u.%02u", d / 100u, d % 100u); }   /* (h 100 / 2^20) */
        else { const unsigned d = (unsigned)((h * 5u + (1u << 18)) >> 19); snprintf(b, n, "%u.%u", d / 10u, d % 10u); }
        *unit = "Hz";
        return 1;
    }
    case KF_TIME: {    /* 1 ms .. 10 s, exponential: ms * 2^16 */
        const uint32_t ms = exp_v(v, 0, 13933177, 16);   /* (log2 10000 in Q20, rounded up: 10 s is 10 s) */
        if (ms < (100u << 16)) { const unsigned d = (ms * 10u + (1u << 15)) >> 16; snprintf(b, n, "%u.%u", d / 10u, d % 10u); *unit = "ms"; }
        else if (ms < (10000u << 16)) { snprintf(b, n, "%u", (unsigned)((ms + (1u << 15)) >> 16)); *unit = "ms"; }
        else { const unsigned d = (ms + (50u << 16)) / (100u << 16); snprintf(b, n, "%u.%u", d / 10u, d % 10u); *unit = "s"; }
        return 1;
    }
    case KF_BIPCT: snprintf(b, n, v > 0 ? "+%d" : "%d", v * 100 / 64); *unit = "%"; return 1;
    case KF_SEMI: if (v > 0) snprintf(b, n, "+%d", v); else snprintf(b, n, "%d", v); *unit = "st"; return 1;
    case KF_NOTE: { static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; snprintf(b, n, "%s", NN[(v % 12 + 12) % 12]); return 0; }
    case KF_BPM: snprintf(b, n, "%d", v); *unit = "BPM"; return 1;
    case KF_STEPS: snprintf(b, n, "%d", v + 1); *unit = "/16"; return 1;
    case KF_LINK_FENV: snprintf(b, n, "%+d", (v - 50) * 2); *unit = "%"; return 1;   /* the host's ENV -> filter, 50 none */
    case KF_CENTER: snprintf(b, n, v == d->def ? "0" : "%+d", v - d->def); *unit = d->unit ? d->unit : ""; return 1;   /* - 0 +, the default 0 */
    case KF_FXSEMI: { const int st = ((v < 0 ? 0 : v > 100 ? 100 : v) * 24 + 50) / 100 - 12; snprintf(b, n, st ? "%+d" : "0", st); *unit = "st"; return 1; }
    case KF_MNOTE: {   /* 60 = C4 */
        static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        snprintf(b, n, "%s%d", NN[(v % 12 + 12) % 12], v / 12 - 1); *unit = ""; return 0;
    }
    case KF_ENUM: snprintf(b, n, "%s", d->names[k_param_clamp(d, v)]); *unit = d->unit ? d->unit : ""; return 0;
    case KF_FXDIV: {   /* a synced time or rate: the division (as the effect takes it, pd_dsp.h's PD_DIV_192) */
        static const char *const DIV[16] = {"1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T",
                                            "1/4D", "1/2", "1/1T", "1/2D", "1/1"};
        snprintf(b, n, "%s", DIV[k_fx_div_index(v)]); return 0;
    }
    case KF_ONOFF: if (d->max > 1) { snprintf(b, n, "%s", k_switch_name(d, v)); return 0; }   /* an effect's switch (kept 0..100) */
    /* fall through: an engine's 0/1, a number as it was */
    default: snprintf(b, n, d->min < 0 && v > 0 ? "%+d" : "%d", v); *unit = d->unit ? d->unit : ""; return 1;   /* (a range below 0: - 0 +) */
    }
}
int ui_format_desc(const KParamDesc *d, int v, char *b, size_t n, const char **unit) { return format_desc(d, v, b, n, unit); }   /* (tests) */
/* a fader's dB as text, with no float printf (the FM-1's has none): "+6.0", "0.0", "-12.5", "-inf" */
void db_text(int v, char *b, size_t n)
{
    int t;
    if (v <= 0) { snprintf(b, n, "-inf"); return; }
    t = k_level_db10(v);   /* (tenths of a dB: exact) */
    if (!t) snprintf(b, n, "0.0");
    else snprintf(b, n, "%c%d.%d", t < 0 ? '-' : '+', (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
}
/* an item's value as text, by its entry in the values' table (k_param_props: its format, an engine's or an effect's own) */
int ui_item_value(const KApp *a, KPage page, int i, char *b, size_t n, const char **unit)
{
    const int v = k_param_get(a, page, i);
    const KParamRef r = k_param_ref(a, page, i);
    KParamProps p;
    if (!k_param_props(a, r, &p) || (r.view & K_V_NOSHOW)) { b[0] = 0; *unit = ""; return 0; }   /* (no plugin, past the engine's, an empty row) */
    switch (p.d.fmt) {
    case KF_FADER: db_text(v, b, n); *unit = "dB"; return 1;
    case KF_COLOR: snprintf(b, n, "%s", PALETTE_NAME[v % K_PALETTE]); *unit = ""; return 0;
    case KF_FXTYPE: snprintf(b, n, "%s", fx_name(v)); *unit = ""; return 0;
    default: return format_desc(&p.d, v, b, n, unit);
    }
}
int ui_param_track(KPage page, int index) { return K_IS_HOME(page) && index >= 4 ? K_HOME_TRACK(index) + 1 : 0; }   /* the track an item is for, 1.., or 0 (the macros: the track in hand's, no number) */
GIcon page_icon(KPage p)
{
    switch (p) {
    case K_PAGE_HOME: case K_PAGE_TRACKS: return GI_PAGE_MIXER;
    case K_PAGE_STEP: return GI_PAGE_PATTERN;
    case K_PAGE_DRUM: return GI_PAGE_DRUM;
    case K_PAGE_ROLL: return GI_PAGE_SCL;   /* the piano */
    case K_PAGE_SONG: return GI_PAGE_SONG;
    case K_PAGE_SOUND: return GI_P_CUTOFF;   /* SEL: a low-pass filter */
    case K_PAGE_EDIT: return GI_P_GENERIC;   /* EDIT: a knob */
    case K_PAGE_FX: return GI_PAGE_FX;
    case K_PAGE_MIXER: return GI_PAGE_MIXER;
    case K_PAGE_BUS1: case K_PAGE_BUS2: return GI_PAGE_BUS;
    case K_PAGE_LFO: return GI_PAGE_SOUND;   /* a sine wave */
    case K_PAGE_ARP: return GI_PAGE_ARP;
    case K_PAGE_SCL: return GI_PAGE_SCL;
    default: return GI_PAGE_ENV;
    }
}
