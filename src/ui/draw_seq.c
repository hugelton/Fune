/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The sequencer's pictures: STEP, DRUM's lanes, ROLL (the piano roll, and its touch) and the tracker's rows. */
#include "draw_int.h"

/* ------------------------------------------------------------- pictures --- */

/* the cells of a step row are 16 in four groups of four, the remainder spread so the row ends at the margin */
static KRect step_cell(int x0, int w, int i, int y, int h)
{
    int cw = w - 15 * 2 - 3 * 3;
    return (KRect){x0 + cw * i / K_STEPS + i * 2 + (i / 4) * 3, y, cw * (i + 1) / K_STEPS - cw * i / K_STEPS, h};
}
/* STEP: the notes of the track like a piano roll (a bar for each step that sounds: its pitch up, its length across,
 * its velocity in the brightness), the step in hand outlined, the playhead a tint; under it the row of the 16 steps */
/* the notes of the track in hand's pattern (KApp.roll): k the slot, n the note (KNote.at & 15 its step) */
#define FOR_PAT_NOTES(a, t, k, n) for ((k) = 0; (k) < K_ROLL_NOTES; (k)++) if (((n) = &(a)->roll[t][k])->len && ((n)->at >> 4) == (a)->pattern[t])
/* a step that sounds only by chance (KApp.chance): its notes or hits fainter, a fifth toward the background for each 25 % less */
static uint32_t by_chance(const KApp *a, int t, int st, uint32_t col)
{
    const int c = k_chance(a, t, a->pattern[t], st);
    return c ? k_mix(th.bg, col, 100 - 20 * c) : col;
}
int step_top(const KApp *a, int t, int pat, int st)   /* the highest note starting at step st of pattern pat (a miniature's mark), -1 none */
{
    int k, top = -1;
    for (k = 0; k < K_ROLL_NOTES; k++) { const KNote *n = &a->roll[t][k]; if (n->len && n->at == (uint8_t)(pat << 4 | st) && n->note > top) top = n->note; }
    return top;
}
static int ghost_at(const KApp *a, int t) { return !k_roll_count(a, t, a->pattern[t], a->cur[t]); }   /* the step in hand empty: its ghost (the next note) shows */
void pic_step(KApp *a)
{
    int t = a->track, i, strip = 10, roll_h = pic.h - strip - GAP - 2 * PIC_PAD, x0 = pic.x + PIC_PAD, w = pic.w - 2 * PIC_PAD;
    int lo = 127, hi = 0, top, rows = 24, rh, ry = pic.y + PIC_PAD, sy = pic.y + pic.h - PIC_PAD - strip, cur = a->cur[t];
    uint16_t bits = a->steps[t][a->pattern[t]];
    {   const KNote *n; int k;
        FOR_PAT_NOTES(a, t, k, n) { if (n->note < lo) lo = n->note; if (n->note > hi) hi = n->note; }
        if (ghost_at(a, t) || hi < lo) { if (a->rnote[t] < lo) lo = a->rnote[t]; if (a->rnote[t] > hi) hi = a->rnote[t]; } }
    {   /* a window of two octaves on whole octaves (it holds still while a note moves within one), the lowest note's octave up */
        int base = lo / 12 * 12;
        if (hi - base >= rows) base = hi / 12 * 12 - 12;
        if (base < 0) base = 0;
        top = base + rows;
    }
    rh = roll_h / rows; if (rh < 2) rh = 2;
    for (i = 0; i < K_STEPS; i++) {   /* the columns */
        KRect c = step_cell(x0, w, i, ry, roll_h);
        int play = a->playing && a->step == i;
        g_rrect(&cv, c.x, c.y, c.w, c.h, 2, play ? k_mix(th.surf, th.theme, 30) : i / 4 % 2 ? k_mix(th.surf, th.text, 5) : k_mix(th.surf, th.text, 9));
    }
    for (i = top - rows; i < top; i++)   /* a line at each C, with the octave's number only in the mind */
        if (i % 12 == 0) g_fill(&cv, x0, ry + (top - i) * roll_h / rows - 1, w, 1, th.line);
    {   /* the notes: a bar each (a chord: one over another), the ghost of the next note on an empty step in hand */
        const KNote *n; int k;
        FOR_PAT_NOTES(a, t, k, n) {
            const int st = n->at & 15, j = st + n->len - 1 > K_STEPS - 1 ? K_STEPS - 1 : st + n->len - 1;
            const KRect c = step_cell(x0, w, st, ry, roll_h), e = step_cell(x0, w, j, ry, roll_h);
            int y = ry + (top - n->note) * roll_h / rows;
            if (y < ry) y = ry;
            if (y > ry + roll_h - rh) y = ry + roll_h - rh;
            face(c.x, y, e.x + e.w - c.x, rh, 1, by_chance(a, t, st, k_mix(th.dim, th.theme, n->vel * 100 / 127)), 16, 16);
        }
        if (ghost_at(a, t)) {
            const KRect c = step_cell(x0, w, cur, ry, roll_h);
            int y = ry + (top - a->rnote[t]) * roll_h / rows;
            if (y < ry) y = ry;
            if (y > ry + roll_h - rh) y = ry + roll_h - rh;
            g_rrect(&cv, c.x, y, c.w, rh, 1, k_mix(th.surf, th.text, 25));   /* a note not switched on, as a ghost */
        }
    }
    for (i = 0; i < K_STEPS; i++) {
        KRect c = step_cell(x0, w, i, ry, roll_h), s = step_cell(x0, w, i, sy, strip);
        int on = (bits >> i) & 1;
        if (prof->input & (K_IN_MOUSE | K_IN_TOUCH)) k_set_bounds(a, K_NODE_STEP, i, (KRect){s.x, c.y, s.w, (int16_t)(s.y + s.h - c.y)});   /* a pointer: the
                                                 * step's whole column over its strip (the strip alone was 10 high: the a11y audit) */
        else k_set_bounds(a, K_NODE_STEP, i, s);
        if (on) g_rrect(&cv, s.x, s.y, s.w, s.h, 3, th.theme);   /* flat (no bevel, no glow, no well) */
        else g_rrect(&cv, s.x, s.y, s.w, s.h, 3, k_mix(th.bg, th.text, 10));
        if (i == cur) {   /* the step in hand */
            g_rrect(&cv, c.x - 1, ry - 1, c.w + 2, 1, 0, th.accent);
            g_rrect(&cv, s.x - 1, s.y - 1, s.w + 2, 1, 0, th.accent);
            g_rrect(&cv, s.x - 1, s.y + s.h, s.w + 2, 1, 0, th.accent);
        }
    }
}
/* DRUM: a row of 16 cells for each lane, coloured by what the lane is (kick and snare, hats, claps and cymbals, toms), a switched-on
 * cell in the colour and an off one in a dark tint of it; the lane in hand has its name in a chip, the playhead lights its column.
 * A bracket under the grid marks the length of the pattern. */
/* a drum lane's name: its piece's (Kreisel's DRUM: "T8 TOM" -> "TM"), else the engine's lane names */
const char *lane_label(const KApp *a, int t, int l)
{
    const KEngine *e = k_engine_at(a->sound[t]);
    static char b[K_LANES][3];
    const char *nm, *sp;
    if (e->uid != K_UID('D', 'R', 'U', 'M')) return e->lanes[l] ? e->lanes[l] : "-";
    nm = e->edit[0].names[k_param_clamp(&e->edit[0], a->lane_p[t][l][0])];
    sp = strchr(nm, ' ');
    sp = sp ? sp + 1 : nm;
    b[l][0] = sp[0];
    b[l][1] = !strcmp(sp, "TOM") ? 'M' : !strcmp(sp, "CNG") ? 'G' : sp[1];   /* TM, CG; SHA: SH */
    if (!strcmp(sp, "TOM")) {   /* two toms or more: LT, (MT,) HT by their order, not TM twice (the a11y audit 2026-11-12) */
        int k, n = 0, j = 0;
        for (k = 0; k < K_LANES; k++) {
            const char *o = e->edit[0].names[k_param_clamp(&e->edit[0], a->lane_p[t][k][0])], *os = strchr(o, ' ');
            if (!strcmp(os ? os + 1 : o, "TOM")) { if (k < l) j++; n++; }
        }
        if (n > 1) { b[l][0] = j == 0 ? 'L' : j == 1 && n > 2 ? 'M' : 'H'; b[l][1] = 'T'; }
    }
    b[l][2] = 0;
    return b[l];
}
static uint32_t lane_hue(const KApp *a, int t, int l)   /* by what the lane plays: kick and snare pink, metal orange, toms cyan, the rest purple */
{
    const char *n = lane_label(a, t, l);
    if (!strcmp(n, "BD") || !strcmp(n, "SD")) return 0xD030B0;
    if (!strcmp(n, "CH") || !strcmp(n, "OH") || !strcmp(n, "CY")) return 0xD98A2B;
    if (!strcmp(n, "TM") || !strcmp(n, "CG")) return 0x5CCBCF;
    if (!strcmp(n, "CP")) return 0xA8CC40;
    return 0x8A44E0;
}
void pic_drum(KApp *a)
{
    int t = a->track, l, i, lab = 20, gap = 2, br = 5, rh = (pic.h - 2 * PIC_PAD - br - (K_LANES - 1) * gap) / K_LANES, y0, x0 = pic.x + PIC_PAD + lab + 4, w = pic.w - 2 * PIC_PAD - lab - 4;
    y0 = pic.y + PIC_PAD;
    for (l = 0; l < K_LANES; l++) {
        int y = y0 + l * (rh + gap), cur = l == a->cur_lane[t];
        uint32_t hue = lane_hue(a, t, l);
        lane_lab[l] = (KRect){(int16_t)(pic.x + PIC_PAD), (int16_t)y, (int16_t)lab, (int16_t)rh};
        if (rh >= 9) g_text_box(&cv, pic.x + PIC_PAD, y, lab, rh, GF_S, lane_label(a, t, l), cur ? th.accent : th.dim, 0);
        for (i = 0; i < K_STEPS; i++) {
            KRect r = step_cell(x0, w, i, y, rh);
            int on = (a->lane_on[t][l] >> i) & 1, play = a->playing && a->step == i;
            uint32_t hot = play ? k_mix(hue, 0xFFFFFF, 35) : hue;
            k_set_bounds(a, K_NODE_STEP, l * K_STEPS + i, r);
            if (on) g_rrect(&cv, r.x, r.y, r.w, r.h, 2, by_chance(a, t, i, hot));   /* flat */
            else g_rrect(&cv, r.x, r.y, r.w, r.h, 2, k_mix(th.bg, hue, play ? 26 : 14));
        }
        if (cur && k_selected(a, K_SEL_LANE) >= 0 && (prof->input & K_IN_MOUSE)) {   /* selected by its name: the whole lane veiled and framed */
            g_rrect_a(&cv, pic.x + PIC_PAD - 2, y - 1, pic.w - 2 * PIC_PAD + 4, rh + 2, 4, th.sel, 40);
            g_fill(&cv, pic.x + PIC_PAD - 2, y - 1, pic.w - 2 * PIC_PAD + 4, 1, th.sel); g_fill(&cv, pic.x + PIC_PAD - 2, y + rh, pic.w - 2 * PIC_PAD + 4, 1, th.sel);
        }
    }
    {   /* the bracket: the pattern's 16 steps */
        int by = pic.y + pic.h - PIC_PAD - br + 1;
        g_fill(&cv, x0, by, w, 1, th.mid);
        g_fill(&cv, x0, by - 2, 1, 3, th.mid);
        g_fill(&cv, x0 + w - 1, by - 2, 1, 3, th.mid);
    }
}

/* a value as the values' table holds it for a picture: k_param_get kept within the range the table gives it now (what its card shows),
 * so a picture never indexes or draws past it (a value stored before its range was narrowed) */
int table_val(const KApp *a, KPage page, int i)
{
    KParamProps p;
    const int v = k_param_get(a, page, i);
    if (!k_param_props(a, k_param_ref(a, page, i), &p) || p.hi < p.lo) return v;
    return v < p.lo ? p.lo : v > p.hi ? p.hi : v;
}
/* ROLL: the piano roll, after Felucca 2.0's: 21 semitone rows, the highest on top, the grid across the whole picture (the C rows
 * are named faintly over its left edge); rows in the track's scale (SCL: root and scale) are tinted, a C row closes with a line;
 * a line between the steps, the beats brighter; a note is a bar on its row as long as its length (velocity in its brightness,
 * the note in hand in the accent); a note out of view leaves a 1 px mark on the edge; the cursor a frame round its column, the
 * playhead a 1 px accent line. The view fits the notes of the page, else follows the cursor's note, a third of the way a frame. */
#define ROLL_ROWS 21
int roll_lo, roll_trk = -1, roll_moving, roll_manual;   /* roll_manual: the finger scrolled it (held until a pad's input) */
static struct { int on, x0, gw, top, rh, rows; } rg;   /* the grid as last drawn: the finger's cells */
static int roll_man_cur, roll_man_note;   /* the step and note in hand when it was scrolled by hand */
static void roll_hand(const KApp *a) { roll_manual = 1; roll_trk = a->track; roll_man_cur = a->cur[a->track]; roll_man_note = a->rnote[a->track]; }
/* ROLL's rows moved by hand (a stick, a wheel): n semitones up (more of the high notes), kept until the step or note in hand moves */
void ui_roll_scroll(const KApp *a, int n)
{
    int lo = roll_lo + n;
    roll_lo = lo < 0 ? 0 : lo > 128 - ROLL_ROWS ? 128 - ROLL_ROWS : lo;
    roll_hand(a);
}
static void roll_follow(const KApp *a)
{
    int t = a->track, k, lo = 127, hi = -1, tgt, d, cur = roll_lo, snap = roll_trk != t;
    const KNote *n;
    if (roll_manual && roll_trk == t && roll_man_cur == a->cur[t] && roll_man_note == a->rnote[t]) { roll_moving = 0; return; }   /* (scrolled by hand: kept
                                                 * until the track, the step or the note in hand moves) */
    roll_manual = 0;
    FOR_PAT_NOTES(a, t, k, n) { if (n->note < lo) lo = n->note; if (n->note > hi) hi = n->note; }
    if (hi < 0) lo = hi = a->rnote[t];
    if (hi - lo <= ROLL_ROWS - 1) tgt = !snap && !roll_moving && lo >= cur && hi <= cur + ROLL_ROWS - 1 ? cur : (lo + hi + 1) / 2 - ROLL_ROWS / 2;   /* all fit: centred, then kept */
    else tgt = a->rnote[t] - ROLL_ROWS / 2;   /* too wide: the note in hand in the middle */
    if (tgt < 0) tgt = 0;
    if (tgt > 128 - ROLL_ROWS) tgt = 128 - ROLL_ROWS;
    d = tgt - cur;
    roll_lo = snap ? tgt : cur + (d / 3 ? d / 3 : d > 0 ? 1 : d < 0 ? -1 : 0);
    roll_moving = roll_lo != tgt;
    roll_trk = t;
}
/* the roll as cells under a cursor (K_LAY_ROLL_CELLS: the Zero): a second header with what is in
 * hand (the pitch, the step, the length, the velocity, the pattern), a keyboard down the left (its rows' names inside), a note a block
 * the height of its row, the cursor (the app's) on the cell; down the right a minimap of the whole pitch range:
 * each step's notes a dot at their pitch, the rows shown framed, the rest dim. No cards: its keys are the arrows, OK and K L */
static int cells_top = 64;   /* the highest pitch shown (follows the cursor) */
static void pic_roll_cells(KApp *a)
{
    static const char *const NM[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const int BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    const int t = a->track, cur = a->cur[t], rn = a->rnote[t] < 0 ? 60 : a->rnote[t] > 127 ? 127 : a->rnote[t];
    const int sh = g_text_h(GF_S) + 5, kw = 20, mw = 34;
    const int gy = pic.y + sh + 3, rows_h = pic.y + pic.h - 2 - gy, rh = 12, rows = rows_h / rh < 1 ? 1 : rows_h / rh;
    const int kx = pic.x + 2, gx = kx + kw + 2, cw = (pic.x + pic.w - 2 - mw - 4 - gx) / K_STEPS, gwu = cw * K_STEPS, mx = gx + gwu + 4, mwr = pic.x + pic.w - 2 - mx;   /* (the minimap takes what the cells leave) */
    const uint32_t hue = track_hue(t);
    const KNote *n;
    char b[24];
    int i, k, x;
    if (rn > cells_top) cells_top = rn;   /* (the cursor in view) */
    if (rn < cells_top - rows + 1) cells_top = rn + rows - 1;
    if (cells_top > 127) cells_top = 127;
    if (cells_top < rows - 1) cells_top = rows - 1;
    {   /* the second header: what is in hand */
        const int sl = k_roll_at(a, t, a->pattern[t], cur, rn);   /* (the note under the cursor) */
        const int len = sl >= 0 ? a->roll[t][sl].len : a->rlen[t] ? a->rlen[t] : 1, vel = sl >= 0 ? a->roll[t][sl].vel : a->rvel[t] ? a->rvel[t] : 100;
        const struct { const char *k; int v; } it[4] = {{"Step", cur + 1}, {"Len", len}, {"Vel", vel}, {"Pattern", -1}};
        g_fill(&cv, pic.x, pic.y, pic.w, sh, k_mix(th.bg, th.text, 5));
        snprintf(b, sizeof b, "%s%d", NM[rn % 12], rn / 12 - 1);
        x = g_text(&cv, pic.x + 6, pic.y + (sh - g_text_h(GF_M)) / 2, GF_M, b, th.text) + 10;
        for (i = 0; i < 4; i++) {
            x = g_text(&cv, x, pic.y + (sh - g_text_h(GF_S)) / 2, GF_S, it[i].k, th.dim) + 3;
            if (it[i].v >= 0) snprintf(b, sizeof b, "%d", it[i].v); else { b[0] = (char)('A' + a->pattern[t]); b[1] = 0; }
            x = g_text(&cv, x, pic.y + (sh - g_text_h(GF_S)) / 2, GF_S, b, th.text) + 10;
        }
    }
    for (k = 0; k < rows; k++) {   /* the keyboard, the rows */
        const int nn = cells_top - k, y = gy + k * rh, blk = BLACK[nn % 12];
        g_fill(&cv, kx, y, kw, rh - 1, blk ? k_mix(th.bg, th.text, 14) : k_mix(th.bg, th.text, 70));
        if (nn == rn || nn % 12 == 0) {
            snprintf(b, sizeof b, "%s%d", NM[nn % 12], nn / 12 - 1);
            g_text_box(&cv, kx, y, kw, rh - 1, GF_S, b, blk ? th.text : th.bg, 0);
        }
        g_fill(&cv, gx, y, gwu, rh - 1, k_mix(th.bg, th.text, nn == rn ? (blk ? 10 : 14) : blk ? 4 : 8));
    }
    for (i = 0; i <= K_STEPS; i++) g_fill(&cv, gx + i * cw, gy, 1, rows * rh - 1, k_mix(th.bg, th.text, i % 4 ? 8 : 22));
    for (i = 0; i < K_STEPS; i++) k_set_bounds(a, K_NODE_STEP, i, (KRect){gx + i * cw, gy, cw, rows * rh});
    FOR_PAT_NOTES(a, t, k, n) {   /* the notes: blocks the height of their rows */
        const int st = n->at & 15, ln = st + n->len > K_STEPS ? K_STEPS - st : n->len, r = cells_top - n->note, hot = n->note == rn && cur >= st && cur < st + n->len;
        if (r < 0 || r >= rows) continue;
        g_rrect(&cv, gx + st * cw + 1, gy + r * rh + 1, ln * cw - 2, rh - 3, 2, hot ? th.text : hue);
    }
    if (a->playing && a->step >= 0) g_fill(&cv, gx + (a->step % K_STEPS) * cw + cw / 2, gy, 1, rows * rh - 1, th.text);   /* the playhead */
    {   /* the cursor's target: the cell (the app's cursor glides to it) */
        const int r = cells_top - rn;
        if (r >= 0 && r < rows) k_set_bounds(a, K_NODE_STEP, cur, (KRect){gx + cur * cw, gy + r * rh, cw + 1, rh});
    }
    {   /* the minimap: the whole range, 127 at the top */
        const int my = gy, mh = rows * rh - 1;
#define MP(nn) (my + 1 + (127 - (nn)) * (mh - 4) / 127)
        const int w0 = MP(cells_top) - 1, w1 = MP(cells_top - rows + 1) + 2;
        g_fill(&cv, mx, my, mwr, mh, k_mix(th.bg, th.text, 3));
        g_fill(&cv, mx, w0, mwr, w1 - w0, k_mix(th.bg, th.text, 7));
        FOR_PAT_NOTES(a, t, k, n) {
            const int st = n->at & 15, y = MP(n->note), in = y >= w0 && y <= w1;
            for (i = st; i < st + n->len && i < K_STEPS; i++) g_fill(&cv, mx + 1 + i * (mwr - 2) / K_STEPS, y, 2, 2, in ? hue : k_mix(th.bg, hue, 45));
        }
        g_fill(&cv, mx, w0, mwr, 1, th.text); g_fill(&cv, mx, w1, mwr, 1, th.text);
        g_fill(&cv, mx, w0, 1, w1 - w0 + 1, th.text); g_fill(&cv, mx + mwr - 1, w0, 1, w1 - w0 + 1, th.text);
#undef MP
    }
}
void pic_roll(KApp *a)
{
    if (a->lay & K_LAY_ROLL_CELLS) { pic_roll_cells(a); return; }
    int t = a->track, i, k, rows = ROLL_ROWS, rh_all = pic.h - 2 * PIC_PAD, rh = rh_all / rows, top = pic.y + PIC_PAD + (rh_all - rh * rows) / 2;
    int x0 = pic.x + PIC_PAD, gw = pic.w - 2 * PIC_PAD, ybot = top + rows * rh, cur = a->cur[t];
    int root = table_val(a, K_PAGE_SCL, 0), scl = table_val(a, K_PAGE_SCL, 1);   /* (the track in hand's: t) */
#define RX(i) (x0 + gw * (i) / K_STEPS)   /* the columns share the whole width, the remainder spread */
    static const int BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint16_t bits = a->steps[t][a->pattern[t]];
    roll_follow(a);
    rg.on = 1; rg.x0 = x0; rg.gw = gw; rg.top = top; rg.rh = rh; rg.rows = rows;
    for (k = 0; k < rows; k++) {   /* the rows: tinted in the scale, the keys, the C names */
        int n = roll_lo + rows - 1 - k, y = top + k * rh, pc = n % 12, in = (MASK[scl & 15] >> ((n - root + 120) % 12)) & 1;
        if (scl == 0) in = !BLACK[pc];   /* chromatic: the white keys */
        g_fill(&cv, x0, y, gw, rh, in ? k_mix(th.bg, th.text, 6) : k_mix(th.bg, 0x000000, 25));
        if (pc == 0) g_fill(&cv, x0, y + rh - 1, gw, 1, th.line);
    }
    g_hline_a(&cv, x0, top, gw, 0x000000, 140); g_hline_a(&cv, x0, top + 1, gw, 0x000000, 60);   /* the recess: shaded under its top edge */
    for (i = 0; i <= K_STEPS; i++)   /* the step lines, the beats brighter */
        g_fill(&cv, RX(i) - (i == K_STEPS), top, 1, ybot - top, i % 4 ? k_mix(th.bg, th.text, 6) : th.line);
    for (k = 0; k < rows; k++) {   /* the C rows named, faintly, over the grid at its left edge (under the notes) */
        int n = roll_lo + rows - 1 - k;
        if (n % 12 == 0) {
            char b[6];
            snprintf(b, sizeof b, "C%d", n / 12 - 1);
            g_text(&cv, x0 + 3, top + k * rh + rh / 2 - g_text_h(GF_S) / 2, GF_S, b, k_mix(th.bg, th.text, 30));
        }
    }
    (void)bits;
    for (i = 0; i < K_STEPS; i++) {   /* the columns: the targets of the steps, the playhead */
        int x = RX(i), cw = RX(i + 1) - x;
        KRect col = {x, top, cw, ybot - top};
        k_set_bounds(a, K_NODE_STEP, i, col);
        if (a->playing && a->step == i) g_fill(&cv, x + cw / 2, top, 1, ybot - top, th.accent);
    }
    {   /* the notes: a bar each on its row (a chord, bars over each other), the note in hand in the accent; out of view a mark on
         * the edge; the step in hand empty, the next note's ghost */
        const KNote *n; int k;
        FOR_PAT_NOTES(a, t, k, n) {
            const int st = n->at & 15, j = st + n->len - 1 > K_STEPS - 1 ? K_STEPS - 1 : st + n->len - 1, x = RX(st), bw = RX(j + 1) - x - 1;
            const int r = roll_lo + rows - 1 - n->note, hot = k == a->rsel[t];
            if (n->note >= roll_lo + rows) { g_fill(&cv, x + 1, top, bw, 1, th.mid); continue; }
            if (n->note < roll_lo) { g_fill(&cv, x + 1, ybot - 1, bw, 1, th.mid); continue; }
            face(x + 1, top + r * rh + 1, bw, rh - 2, 1, hot ? th.accent : by_chance(a, t, st, k_mix(th.dim, th.theme, n->vel * 100 / 127)), 16, 16);
        }
        if (ghost_at(a, t) && a->rnote[t] >= roll_lo && a->rnote[t] < roll_lo + rows) {
            const int x = RX(cur), cw = RX(cur + 1) - x, r = roll_lo + rows - 1 - a->rnote[t];
            g_rrect(&cv, x + 1, top + r * rh + 1, cw - 1, rh - 2, 1, k_mix(th.bg, th.text, 22));   /* the note in hand, not switched on: a ghost */
        }
    }
    {   /* the cursor: a frame round its column */
        int fx = RX(cur) - 1, fw = RX(cur + 1) - RX(cur) + 2, fy = top - 2, fh = ybot - top + 4;
        g_fill(&cv, fx, fy, fw, 1, th.text); g_fill(&cv, fx, fy + fh - 1, fw, 1, th.text);
        g_fill(&cv, fx, fy, 1, fh, th.text); g_fill(&cv, fx + fw - 1, fy, 1, fh, th.text);
    }
    if (prof->input & (K_IN_TOUCH | K_IN_PAD)) {   /* where the rows are in the whole range: a thin bar down the right edge (the left stick,
                                                 * a finger on the left edge, scroll it); the top is the highest note */
        const int bx = x0 + gw - 3, bh = ybot - top, th_ = bh * rows / 128 < 6 ? 6 : bh * rows / 128;
        const int ty = top + (bh - th_) * (128 - rows - roll_lo) / (128 - rows);
        g_fill(&cv, bx, top, 2, bh, k_mix(th.bg, th.text, 10));
        g_rrect(&cv, bx, ty, 2, th_, 1, k_mix(th.bg, th.text, 45));
    }
}
/* SEQ as a tracker (KApp.rows_view, the SEQ tools' Tracker, mockups/2026-11-12_tracker_view.png): a row a step,
 * a column a track (as many as fit, the track in hand's among them), each column the pattern its track plays now (the scene's). A
 * cell: a note and its velocity ("C-4 100", the velocity left out where it does not fit), a chord its highest note and how many
 * more ("E-3+2"), a held note "|", a drum track the pieces of its lanes ("BD HH", "BD+2"), else "---". The track in hand's name
 * filled in its colour, the beats' rows brighter, the playhead's row lit, the cursor a frame on the cell. */
static int rows_top;   /* the first row shown */
static int rows_sc(void) { return cv.w >= 600 ? 2 : 1; }   /* the tracker's text: the 8 x 8 pixel font (g_pix8: monospace), doubled on a big screen */
static void rows_text(int x, int y, const char *s, uint32_t col) { g_pix8(&cv, x, y, s, col, rows_sc()); }
static void rows_cell(const KApp *a, int t, int s, char *b, int n, int *on)
{
    static const char *const NM[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    int k, cnt = 0, top = -1, vel = 0, held = 0;
    *on = 1;
    if (k_is_drum(a, t)) {
        int l, m = 0;
        b[0] = 0;
        for (l = 0; l < K_LANES; l++) if ((a->lane_on[t][l] >> s) & 1) {
            if (m < 2) snprintf(b + strlen(b), (size_t)(n - (int)strlen(b)), "%s%s", m ? " " : "", lane_label(a, t, l));
            m++;
        }
        if (m > 2) { char *sp = strchr(b, ' '); if (sp) snprintf(sp, (size_t)(n - (sp - b)), "+%d", m - 1); }
        if (!m) { snprintf(b, (size_t)n, "---"); *on = 0; }
        return;
    }
    for (k = 0; k < K_ROLL_NOTES; k++) {
        const KNote *x = &a->roll[t][k];
        if (!x->len || (x->at >> 4) != a->pattern[t]) continue;
        if ((x->at & 15) == s) { cnt++; if (x->note > top) { top = x->note; vel = x->vel; } }
        else if ((x->at & 15) < s && (x->at & 15) + x->len > s) held = 1;
    }
    if (!cnt) { snprintf(b, (size_t)n, held ? "|" : "---"); *on = held ? 2 : 0; return; }
    if (cnt > 1) snprintf(b, (size_t)n, "%s%d+%d", NM[top % 12], top / 12 - 1 < 0 ? 0 : top / 12 - 1, cnt - 1);
    else snprintf(b, (size_t)n, "%s%d %d", NM[top % 12], top / 12 - 1 < 0 ? 0 : top / 12 - 1, vel);
}
void pic_rows(KApp *a)
{
    const int pt = 8 * rows_sc(), fh = pt, rh = fh + 3 * rows_sc(), hh = g_text_h(GF_S) + 3, x0 = pic.x + PIC_PAD, w = pic.w - 2 * PIC_PAD;
    const int numw = 2 * pt + 4, minw = 6 * pt + 4;
    const int y0 = pic.y + PIC_PAD, gy = y0 + hh + 2, rows = (pic.y + pic.h - PIC_PAD - gy) / rh < 1 ? 1 : (pic.y + pic.h - PIC_PAD - gy) / rh;
    const int vis = rows > K_STEPS ? K_STEPS : rows, t = a->track, cur = a->cur[t];
    int nc = (w - numw) / minw, c0, c, r, i;
    char b[24];
    if (nc > K_TRACKS) nc = K_TRACKS;
    if (nc < 1) nc = 1;
    c0 = t / nc * nc;
    if (c0 + nc > K_TRACKS) c0 = K_TRACKS - nc;
    if (cur < rows_top) rows_top = cur;
    if (cur > rows_top + vis - 1) rows_top = cur - vis + 1;
    if (rows_top > K_STEPS - vis) rows_top = K_STEPS - vis;
    if (rows_top < 0) rows_top = 0;
    {
        const int cw = (w - numw) / nc;
        for (c = 0; c < nc; c++) {   /* the heads: the track's number and its sound's name */
            const int tc = c0 + c, cx = x0 + numw + c * cw, me = tc == t;
            const char *nm = trk_preset(a, tc);
            int len;
            g_rrect(&cv, cx + 1, y0, cw - 2, hh, 2, me ? track_hue(tc) : k_mix(th.bg, th.text, 10));
            snprintf(b, sizeof b, "%d %s", tc + 1, nm);
            for (len = (int)strlen(b); len > 1 && g_text_w(GF_S, b) > cw - 6; ) b[--len] = 0;   /* (the names in the UI's font) */
            g_text(&cv, cx + 4, y0 + (hh - g_text_h(GF_S)) / 2, GF_S, b, me ? th.text : th.dim);
        }
        for (i = 0; i < K_STEPS * K_LANES; i++) k_set_bounds(a, K_NODE_STEP, i, (KRect){0, 0, 0, 0});
        for (r = 0; r < vis; r++) {
            const int s = rows_top + r, y = gy + r * rh, play = a->playing && a->step == s;
            g_fill(&cv, x0, y, w, rh - 1, play ? k_mix(th.bg, th.text, 16) : k_mix(th.bg, th.text, s % 4 ? 4 : 9));
            snprintf(b, sizeof b, "%02d", s);
            rows_text(x0 + 2, y + (rh - fh) / 2, b, s % 4 ? th.dim : th.text);
            for (c = 0; c < nc; c++) {
                const int tc = c0 + c, cx = x0 + numw + c * cw;
                int on;
                rows_cell(a, tc, s, b, (int)sizeof b, &on);
                if (on == 1 && (int)strlen(b) * pt > cw - 4) { char *sp = strchr(b, ' '); if (sp && !k_is_drum(a, tc)) *sp = 0; }
                if (on == 2) g_fill(&cv, cx + 3 + pt / 2, y, rows_sc(), rh - 1, th.mid);
                else rows_text(cx + 3, y + (rh - fh) / 2, b, on ? th.text : k_mix(th.bg, th.text, 30));
                if (tc == t) {
                    const KRect cell = {cx + 1, y - 1, cw - 2, rh + 1};
                    if (k_is_drum(a, tc)) { int l; for (l = 0; l < K_LANES; l++) k_set_bounds(a, K_NODE_STEP, l * K_STEPS + s, cell); }
                    else k_set_bounds(a, K_NODE_STEP, s, cell);
                    if (s == cur && !k_profile_cursor(prof)) {   /* (a device with a cursor: its corners glide to the cell) */
                        g_fill(&cv, cell.x, cell.y, cell.w, 1, th.text); g_fill(&cv, cell.x, cell.y + cell.h - 1, cell.w, 1, th.text);
                        g_fill(&cv, cell.x, cell.y, 1, cell.h, th.text); g_fill(&cv, cell.x + cell.w - 1, cell.y, 1, cell.h, th.text);
                    }
                }
            }
        }
        for (c = 0; c <= nc; c++) g_fill(&cv, x0 + numw + c * cw - (c == nc), gy, 1, vis * rh - 1, th.line);
    }
}
struct rd_s rd;   /* (its fields: draw_int.h) */
static int roll_cell(int x, int y, int *st, int *n, int clampit)
{
    int s, r;
    if (!rg.on || rg.gw <= 0 || rg.rh <= 0) return 0;
    if (!clampit && (x < rg.x0 || x >= rg.x0 + rg.gw || y < rg.top || y >= rg.top + rg.rows * rg.rh)) return 0;
    s = (x - rg.x0) * K_STEPS / rg.gw; r = (y - rg.top) / rg.rh;
    if (x < rg.x0) s = 0;
    if (y < rg.top) r = 0;
    *st = s < 0 ? 0 : s > K_STEPS - 1 ? K_STEPS - 1 : s;
    r = r < 0 ? 0 : r > rg.rows - 1 ? rg.rows - 1 : r;
    *n = roll_lo + rg.rows - 1 - r;
    return 1;
}
static void roll_hear(KApp *a, int note) { if (note != rd.note) { if (rd.note >= 0) k_preview(a, rd.note, 0); if (note >= 0) k_preview(a, note, 1); rd.note = note; } }
int roll_touch(KApp *a, int phase, int x, int y, uint32_t ms)
{
    const int t = a->track, p = a->pattern[t];
    int st, n;
    (void)ms;
    if (phase <= 0) {
        if (a->page != K_PAGE_ROLL || a->rows_view || k_is_drum(a, t) || a->menu_open || a->settings_open || a->save_open || a->save_ask || a->tools_open || !roll_cell(x, y, &st, &n, 0)) return 0;
        if (phase < 0) {   /* a tap alone */
            const int hit = k_roll_at(a, t, p, st, n);
            if (hit >= 0) k_roll_remove(a, t, hit);
            else k_roll_select(a, t, k_roll_add(a, t, p, st, n, a->rlen[t], a->rvel[t]));
            return 1;
        }
        rd.moved = 0; rd.note = -1;
        if (x < rg.x0 + 14) { rd.mode = 4; rd.y0 = y; rd.lo0 = roll_lo; return 1; }   /* the left edge: the rows scroll */
        rd.slot = k_roll_at(a, t, p, st, n);
        if (rd.slot >= 0) {
            const KNote *k = &a->roll[t][rd.slot];
            const int s0 = k->at & 15, end = s0 + k->len - 1, xr = rg.x0 + rg.gw * (end + 1) / K_STEPS, cw = rg.gw / K_STEPS;
            rd.start = s0; rd.grab = st - s0;
            rd.mode = st == end && x >= xr - (cw / 3 > 4 ? cw / 3 : 4) ? 2 : 1;   /* (its right end, a third of its last cell: sizing) */
        } else {
            rd.slot = k_roll_add(a, t, p, st, n, a->rlen[t], a->rvel[t]);
            if (rd.slot < 0) { rd.mode = 0; return 1; }   /* (the track is full: nothing) */
            rd.start = st; rd.grab = 0; rd.mode = 3;
        }
        k_roll_select(a, t, rd.slot);
        roll_hear(a, a->roll[t][rd.slot].note);
        return 1;
    }
    if (!rd.mode) return 0;
    if (phase == 1) {
        if (rd.mode == 4) {
            const int lo = rd.lo0 + (y - rd.y0) / (rg.rh > 0 ? rg.rh : 1);
            roll_lo = lo < 0 ? 0 : lo > 128 - rg.rows ? 128 - rg.rows : lo;
            roll_hand(a);
            return 1;
        }
        if (rd.slot < 0 || rd.slot >= K_ROLL_NOTES || !a->roll[t][rd.slot].len) { rd.mode = 0; return 1; }
        roll_cell(x, y, &st, &n, 1);
        {   const KNote *k = &a->roll[t][rd.slot];
            const int s0 = k->at & 15, len = k->len, note = k->note;
            if (rd.mode == 1) {
                const int ns = st - rd.grab < 0 ? 0 : st - rd.grab;
                if (ns != s0 || n != note) { k_roll_set(a, t, rd.slot, ns, n, len); rd.moved = 1; roll_hear(a, n); }
            } else {
                const int nl = st - s0 + 1 < 1 ? 1 : st - s0 + 1;
                if (nl != len) { k_roll_set(a, t, rd.slot, s0, note, nl); rd.moved = 1; }
            }
        }
        k_roll_select(a, t, rd.slot);
        return 1;
    }
    if (rd.mode == 1 && !rd.moved) k_roll_remove(a, t, rd.slot);   /* a tap on a note: gone */
    roll_hear(a, -1);
    rd.mode = 0;
    return 1;
}
