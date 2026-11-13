/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Controller profiles (kreisel_ctl.h). */
#include "kreisel_ctl.h"
#include <stdio.h>
#include <string.h>

/* the built-in profiles: the same text a user writes (docs/CONTROLLERS.md) */
static const char NANOKONTROL2[] =
    "# KORG nanoKONTROL2, its factory CC mode\n"
    "name = nanoKONTROL2\n"
    "match = nanoKONTROL2\n"
    "cc 0 = level 1\ncc 1 = level 2\ncc 2 = level 3\ncc 3 = level 4\n"
    "cc 4 = level 5\ncc 5 = level 6\ncc 6 = level 7\ncc 7 = level 8\n"
    "cc 16 = knob 1\ncc 17 = knob 2\ncc 18 = knob 3\ncc 19 = knob 4\n"
    "cc 20 = knob 5\ncc 21 = knob 6\ncc 22 = knob 7\ncc 23 = knob 8\n"
    "cc 32 = page seq\ncc 33 = page synth\ncc 34 = page env\ncc 35 = page fx\n"
    "cc 36 = page lfo\ncc 37 = page arp\ncc 38 = page edit\ncc 39 = page glo\n"
    "cc 48 = home\ncc 64 = undo\n"
    "cc 41 = play\ncc 45 = rec\ncc 46 = tools\n"
    "cc 43 = select-\ncc 44 = select+\n"
    "cc 58 = track-\ncc 59 = track+\n"
    "cc 61 = pattern-\ncc 62 = pattern+\n";
const char *const k_ctl_builtin[] = {NANOKONTROL2};
const int k_ctl_builtin_n = (int)(sizeof k_ctl_builtin / sizeof k_ctl_builtin[0]);

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int word(const char **s, char *w, int n)   /* the next word (lower case), 0 at the line's end */
{
    int k = 0;
    while (**s == ' ' || **s == '\t') (*s)++;
    if (!**s || **s == '\n' || **s == '#') return 0;
    while (**s && **s != ' ' && **s != '\t' && **s != '\n' && **s != '#' && **s != '=') { if (k < n - 1) w[k++] = (char)lower(**s); (*s)++; }
    if (!k && **s == '=') { w[k++] = '='; (*s)++; }
    w[k] = 0;
    return k;
}
static int num(const char *w, int lo, int hi, int *v)
{
    int x = 0, k = 0;
    if (!w[0]) return 0;
    for (; w[k]; k++) { if (w[k] < '0' || w[k] > '9' || k > 4) return 0; x = x * 10 + w[k] - '0'; }
    if (x < lo || x > hi) return 0;
    *v = x;
    return 1;
}
static int key_code(const char *w)   /* a key's name: a letter or a digit, or up down left right pgup pgdn home enter f1 .. f12 */
{
    static const struct { const char *n; int c; } K[] = {{"up", KIO_K_UP}, {"down", KIO_K_DOWN}, {"left", KIO_K_LEFT}, {"right", KIO_K_RIGHT},
        {"pgup", KIO_K_PGUP}, {"pgdn", KIO_K_PGDN}, {"home", KIO_K_HOME}, {"enter", '\r'}, {"space", ' '}, {"comma", ','}, {"period", '.'}};
    int i, f;
    if (w[0] && !w[1]) return (unsigned char)w[0];
    for (i = 0; i < (int)(sizeof K / sizeof K[0]); i++) if (!strcmp(w, K[i].n)) return K[i].c;
    if (w[0] == 'f' && num(w + 1, 1, 12, &f)) return KIO_K_F1 + f - 1;
    return -1;
}
/* what a row does (the words after '='): into m (its act, a, b, repeat); 0 when unknown */
static int what(const char **s, KIOMap *m, int note_row)
{
    static const struct { const char *n; int act, b; } BTN[] = {
        {"play", K_ACT_PLAY, 0}, {"rec", K_ACT_REC, 0}, {"undo", K_ACT_UNDO, 0}, {"tools", K_ACT_TOOLS, 0}, {"ok", K_ACT_ACCEPT, 0},
        {"back", K_ACT_BACK, 0}, {"home", K_ACT_BACK, 0}, {"menu", K_ACT_PAGE_MENU, 0}, {"save", K_ACT_SAVE, 0}, {"settings", K_ACT_SETTINGS, 0},
        {"track+", K_ACT_TRACK_DELTA, 1}, {"track-", K_ACT_TRACK_DELTA, -1}, {"select+", K_ACT_SELECT_DELTA, 1}, {"select-", K_ACT_SELECT_DELTA, -1},
        {"pattern+", K_ACT_TYPE_DELTA, 1}, {"pattern-", K_ACT_TYPE_DELTA, -1}, {"value+", K_ACT_VALUE_DELTA, 1}, {"value-", K_ACT_VALUE_DELTA, -1},
        {"octave+", K_ACT_OCTAVE_DELTA, 1}, {"octave-", K_ACT_OCTAVE_DELTA, -1}, {"up", K_ACT_UP, 0}, {"down", K_ACT_DOWN, 0},
        {"left", K_ACT_LEFT, 0}, {"right", K_ACT_RIGHT, 0}};
    static const struct { const char *n; int g; } PG[] = {{"seq", K_GROUP_PATTERN}, {"glo", K_GROUP_GLO}, {"synth", K_GROUP_SOUND},
        {"edit", K_GROUP_EDIT}, {"env", K_GROUP_ENV}, {"lfo", K_GROUP_LFO}, {"fx", K_GROUP_FX}, {"arp", K_GROUP_ARP}};
    char w[24], x[24];
    int i, v;
    if (!word(s, w, sizeof w)) return 0;
    m->repeat = 0; m->a = 0; m->b = 0;
    if (!strcmp(w, "note")) { if (!note_row) return 0; m->act = KIO_DO_NOTE; return !word(s, x, sizeof x); }
    if (!strcmp(w, "knob") || !strcmp(w, "rel")) {
        if (!word(s, x, sizeof x) || !num(x, 1, 8, &v)) return 0;
        m->act = w[0] == 'k' ? KIO_DO_KNOB : KIO_DO_REL; m->a = (int16_t)(v - 1);
        return !word(s, x, sizeof x);
    }
    if (!strcmp(w, "level")) {
        m->act = KIO_DO_LEVEL; m->a = -1;
        if (word(s, x, sizeof x)) { if (!num(x, 1, 16, &v)) return 0; m->a = (int16_t)(v - 1); }
        return !word(s, x, sizeof x);
    }
    if (!strcmp(w, "pan")) { m->act = KIO_DO_CC; m->a = 10; return !word(s, x, sizeof x); }   /* (the core's CCs: k_cc_in) */
    if (!strcmp(w, "macro")) {
        if (!word(s, x, sizeof x) || !num(x, 1, 4, &v)) return 0;
        m->act = KIO_DO_CC; m->a = (int16_t)(15 + v);
        return !word(s, x, sizeof x);
    }
    if (!strcmp(w, "page")) {
        if (!word(s, x, sizeof x)) return 0;
        for (i = 0; i < (int)(sizeof PG / sizeof PG[0]); i++)
            if (!strcmp(x, PG[i].n)) { m->act = KIO_DO_GROUP; m->a = (int16_t)PG[i].g; m->repeat = 1; return !word(s, x, sizeof x); }
        return 0;
    }
    for (i = 0; i < (int)(sizeof BTN / sizeof BTN[0]); i++)
        if (!strcmp(w, BTN[i].n)) { m->act = KIO_DO_ACTION; m->a = (int16_t)BTN[i].act; m->b = (int16_t)BTN[i].b; m->repeat = 1; return !word(s, x, sizeof x); }
    return 0;
}
static void copy_rest(const char *s, char *out, int n)   /* "name = Some Thing  # x": the value as written, trimmed */
{
    int k = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s && *s != '\n' && *s != '#' && k < n - 1) out[k++] = *s++;
    while (k && (out[k - 1] == ' ' || out[k - 1] == '\t' || out[k - 1] == '\r')) k--;
    out[k] = 0;
}
int k_ctl_parse(const char *text, KCtlProfile *p, char *err, int err_n)
{
    const char *s = text;
    int line = 0;
    memset(p, 0, sizeof *p);
    if (err && err_n) err[0] = 0;
    while (s && *s) {
        const char *ln = s, *t;
        char w[24], x[24];
        KIOMap m;
        int bad = 0, v;
        line++;
        while (*s && *s != '\n') s++;
        if (*s) s++;
        t = ln;
        memset(&m, 0, sizeof m);
        if (!word(&t, w, sizeof w)) continue;   /* (empty, a comment) */
        if (!strcmp(w, "name") || !strcmp(w, "match")) {
            if (!word(&t, x, sizeof x) || strcmp(x, "=")) bad = 1;
            else copy_rest(t, w[0] == 'n' ? p->name : p->match, sizeof p->name);
        } else if (!strcmp(w, "cc") || !strcmp(w, "note") || !strcmp(w, "key")) {
            const int is_key = w[0] == 'k', is_note = w[0] == 'n';
            m.in = (uint8_t)(is_key ? KIO_KEY : is_note ? KIO_NOTE : KIO_CC);
            m.num = -1;
            if (!word(&t, x, sizeof x)) bad = 1;
            else if (is_key) { m.num = (int16_t)key_code(x); bad = m.num < 0; }
            else if (is_note && !strcmp(x, "any")) m.num = -1;
            else if (num(x, 0, 127, &v)) m.num = (int16_t)v;
            else bad = 1;
            if (!bad && !word(&t, x, sizeof x)) bad = 1;
            if (!bad && !strcmp(x, "ch")) {   /* a channel: only that one's */
                if (!word(&t, x, sizeof x) || !num(x, 1, 16, &v)) bad = 1; else m.ch = (uint8_t)v;
                if (!bad && !word(&t, x, sizeof x)) bad = 1;
            }
            if (!bad && strcmp(x, "=")) bad = 1;
            if (!bad && !what(&t, &m, is_note)) bad = 1;
            if (!bad && is_key && (m.act == KIO_DO_KNOB || m.act == KIO_DO_REL || m.act == KIO_DO_LEVEL || m.act == KIO_DO_CC || m.act == KIO_DO_NOTE)) bad = 1;   /* (a key has no value) */
            if (!bad && is_key) { if (m.act == KIO_DO_ACTION && m.a != K_ACT_PLAY && m.a != K_ACT_REC) m.repeat = 1; else m.repeat = 0; }   /* (a key held: the moves repeat) */
            if (!bad) {
                KIOMap *rows = is_key ? p->keys : p->midi;
                int *cnt = is_key ? &p->n_keys : &p->n_midi;
                if (*cnt >= K_CTL_ROWS) { if (err && err_n) snprintf(err, (size_t)err_n, "line %d: more than %d rows", line, K_CTL_ROWS); return -line; }
                rows[(*cnt)++] = m;
            }
        } else bad = 1;
        if (bad) { if (err && err_n) snprintf(err, (size_t)err_n, "line %d: not understood", line); return -line; }
    }
    if (!p->name[0]) { if (err && err_n) snprintf(err, (size_t)err_n, "no name"); return -(line ? line : 1); }
    return 0;
}
int k_ctl_matches(const KCtlProfile *p, const char *name)
{
    size_t i, j, n = strlen(p->match);
    if (!n || !name) return 0;
    for (i = 0; name[i]; i++) {
        for (j = 0; j < n && name[i + j]; j++) if (lower((unsigned char)name[i + j]) != lower((unsigned char)p->match[j])) break;
        if (j == n) return 1;
    }
    return 0;
}
/* MIDI learn's row into a profile's text (k_ctl_learn_text) */
static int row_input(const char *line, KIOMap *m)   /* a cc / note line's input (in, num, ch): 1 when it is one */
{
    static KCtlProfile p;
    char buf[160];
    int k = 0;
    while (line[k] && line[k] != '\n' && k < 120) k++;
    snprintf(buf, sizeof buf, "name = x\n%.*s\n", k, line);
    if (k_ctl_parse(buf, &p, 0, 0) != 0 || p.n_midi != 1) return 0;
    *m = p.midi[0];
    return 1;
}
int k_ctl_learn_text(const char *old, const char *name, const char *match, const char *row, char *out, int n)
{
    KIOMap r, m;
    int k = 0, len;
    const char *s = old;
    if (!row_input(row, &r) || !out || n < 2) return -1;
    out[0] = 0;
    if (!old || !old[0]) k = snprintf(out, (size_t)n, "# made by MIDI learn (docs/CONTROLLERS.md)\nname = %s\nmatch = %s\n", name ? name : "", match ? match : "");
    else while (*s) {   /* the old rows, but one of the same input (the same CC or note on the same channel) */
        const char *e = s;
        while (*e && *e != '\n') e++;
        len = (int)(e - s) + (*e ? 1 : 0);
        if (!(row_input(s, &m) && m.in == r.in && m.num == r.num && m.ch == r.ch)) {
            if (k + len + 1 >= n) return -1;
            memcpy(out + k, s, (size_t)len); k += len;
            if (!*e) out[k++] = '\n';   /* (its last line ended) */
            out[k] = 0;
        }
        s = e + (*e ? 1 : 0);
    }
    if (k < 0 || k >= n) return -1;
    len = snprintf(out + k, (size_t)(n - k), "%s\n", row);
    return len < 0 || k + len >= n ? -1 : 0;
}
